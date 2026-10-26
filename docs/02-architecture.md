# 02 — Architecture

This document describes the original After Effects plug-in as reconstructed
from the binary, and the architecture chosen for the OFX port. The original is
**GPU-only**: there is no CPU pixel code to lift, so the port is a
re-implementation of the GLSL programs plus the host-side render recipe.

---

## 1. Original architecture (recovered)

```
┌─ After Effects ──────────────────────────────────────────────────────────┐
│                                                                          │
│  EffectMain (0x2f340)                                                    │
│      │                                                                   │
│      ├─ PF_Cmd_PARAMS_SETUP ──► custom ECW UI builder ──► Cairo widgets   │
│      ├─ PF_Cmd_SEQUENCE_SETUP / FRAME_SETUP                              │
│      ├─ PF_Cmd_RENDER ────────► orchestrator (0x1c490)                   │
│      └─ PF_Cmd_USER_CHANGED ──► UI state, licensing, menu commands        │
│                                                                          │
│  orchestrator (0x1c490)                                                  │
│      │                                                                   │
│      ├─ setup (0x28910)                                                  │
│      │     comp aspect, resolution, quality preset, pass list,           │
│      │     aberration flags, GL wrap mode                                │
│      │                                                                   │
│      ├─ render (0x15140)                                                 │
│      │     ├─ wglMakeCurrent / create FBOs (RGB16F or RGB32F)            │
│      │     ├─ compile + link 10 GLSL programs                            │
│      │     ├─ upload layer pixels  (glTexSubImage2D)                     │
│      │     ├─ preProcess  (shader 06)                                    │
│      │     ├─ for each octave: blit → blur W (shader 02) → blur H        │
│      │     ├─ additive tent upsample chain (shader 07)                   │
│      │     ├─ postProcess (shader 05)                                    │
│      │     ├─ lens dirt                                                   │
│      │     └─ glReadPixels into the output world                          │
│      │                                                                   │
│      └─ composite (0x1bb50)   screen / add, alpha, premultiply            │
│                                                                          │
│  licensing (aescripts), error reporting, AEGP menu items                 │
└──────────────────────────────────────────────────────────────────────────┘
```

### Components inferred from the binary

| Component | Evidence | Notes |
|---|---|---|
| AE plug-in shell | `EffectMain`, `PluginDataEntryFunction2`, PiPL strings | filter effect with custom UI |
| Custom UI toolkit | static Cairo, `USER32`/`GDI32`, 100+ `cairo_*` exports | drawn ECW, not native AE controls |
| GL renderer | `OPENGL32` imports, `fbo.cpp` path string, `glUseProgram` logging string | creates its own off-screen context if needed (`wglCreateContext`) |
| Shader manager | 10 `// begin shader` blocks, `Failed loading shader source` / `Unable to load shader` strings | shaders embedded in `.rdata` |
| Licensing | `aescripts*` exports, WinINET/Winsock | network activation, machine id |
| Debug/logging | `std::cout` imports, `Shaders/debug/` paths, "Deep Glow 2: " messages | optional shader dump / buffer logs |

### Render resources

* FBO chain of `RGB16F` (Half Float on) or `RGB32F` (off) colour textures.
* One texture per octave plus ping-pong buffers for the separable blur.
* No alpha channel in the working chain — the shaders write `.rgb` only; alpha
  is reattached at composite time.
* Wrap mode comes from the (deprecated) edge-behaviour parameter; the default
  is `GL_MIRRORED_REPEAT` (`0x8370`). Angle mode switches to
  `GL_CLAMP_TO_BORDER` (black border).

### Threading

Rendering happens on the AE render thread with the GL context made current on
that thread. `Shaders Per Frame` and the debug `cout` flags imply that shader
compilation is cached across frames, but no CPU worker pool is used.

---

## 2. Port architecture

```
src/
  DeepGlowOFX.cpp   OFX glue: factory, parameters, clips, image I/O, host callbacks
  DGPipeline.h/.cpp render orchestration (CPU equivalent of the GL FBO recipe)
  DGParams.h        parameter model, quality table, buildPasses()
  DGColor.h         preProcess / postProcess kernels (shaders 06 and 05)
  DGImage.h         float RGBA image, bilinear fetch, wrap modes, blit,
                    13-tap downsample, tent upsample, Gaussian primitives
tests/
  hostcheck.cpp     in-process mini OFX host (registration surface)
  smoke.cpp         headless pipeline test (invariants + PFM dumps)
```

### Data flow

```
OFX Image (float or half, 1–4 comps, premultiplied)
        │  loadImage()          unpremultiply, expand to RGBA float
        ▼
   dg::ImageF source ──────────────────────────────────────────────┐
        │                                                          │
        │ resample to working res (Resolution %)                   │
        ▼                                                          │
   preProcessImage()   aberration → clamp ≥0 → threshold → view     │
        │              → exposure → gamma                          │
        ▼                                                          │
   optional mask multiply                                          │
        ▼                                                          │
   buildPasses() ──► PassInfo[] (size, weight, fade, w/h, steps,    │
        │                          stride)                         │
        ▼                                                          │
   per octave: resample → blur H → blur V (+ fade, aberration)     │
        │                                                          │
        ▼                                                          │
   additive tent upsample back to octave 0                         │
        │                                                          │
        ▼                                                          │
   postProcessImage()  exposure → tint → tonemap → 1/gamma → min(1)│
        │                                                          │
        ▼                                                          │
   optional lens dirt                                              │
        ▼                                                          │
   composite: screen(1-(1-s·op)(1-g)) or add(s·op+g)  ◄────────────┘
        │  premultiply unless Unmult
        ▼
OFX Image (float or half, RGBA)
```

### Mapping original → port

| Original | Port | Notes |
|---|---|---|
| `EffectMain` dispatch | `DeepGlowFactory` + `ImageEffect` subclass | OFX owns the lifecycle |
| ECW/Cairo parameter page | `describeInContext` parameter table | 11 groups, 45 controls |
| `setup (0x28910)` | `DeepGlowEffect::buildParams` + `buildPasses` | units converted on the way in |
| GLSL shader 06 | `preProcessImage` (`DGColor.h`) | same operation order |
| GLSL shader 02 | `blurPass` / `blurAccel` (`DGPipeline.cpp`) | per-channel tap counts |
| GLSL shader 03 | `downsample13` (`DGImage.h`) | used by the mip path (`blurAccel`) |
| GLSL shader 07 | `upsampleTent` (`DGImage.h`) | same 3×3 tent weights |
| GLSL shader 00/01/04 | `resample` (`DGImage.h`); `blitCenter` kept as the literal 00 | octaves are full-frame rescales, not crops |
| GLSL shader 05 | `postProcessImage` (`DGColor.h`) | same order |
| `composite (0x1bb50)` | final loop in `renderGlow` | plus premultiplication |
| FBO/texture management | `dg::ImageF` values | plain `std::vector<float>` |
| `glReadPixels` | `storeImage` | float or half output |

### Key design decisions

| Decision | Rationale |
|---|---|
| **CPU, float32 throughout** | No GPU dependency, deterministic, debuggable. The original's half-float intermediate is a memory optimization only. |
| **Straight (unpremultiplied) RGBA internally** | The shaders never read alpha; AE hands the effect premultiplied pixels. Unpremultiplying on load and premultiplying on store reproduces the original's arithmetic. |
| **Radius in comp-height units** | Recovered: `size0 = Radius × compAspect / 2^iterations`. This is what makes "Radius 1000" behave identically at 1080p and 4K. |
| **AE UI units on the OFX page** | Threshold/Smooth/Gamma/Tint/Opacity/multipliers stay 0–100 (percent) exactly as in AE; `buildParams` applies the recovered `×0.01`. |
| **Quality presets change buffer size *and* tap density** | Both affect the look/perf trade-off; using only one would make Draft/Extreme nearly identical. |
| **Tiles disabled, RoI = full frame** | A multi-octave glow is non-local; tile-local rendering would show seams. |
| **`isIdentity` = false** | Even Radius 0 screens a (possibly zero) glow over the source and clamps. |
| **Optional Mask / Lens Dirt OFX clips** | OFX has no "layer" parameter; a second input is the idiomatic replacement. |
| **No multi-resolution, no temporal access** | The original is a single-frame spatial effect. |

### Image I/O details

`loadImage`:

* handles 1/2/3/4 components (`1` = alpha-only, `3` = RGB with alpha 1),
* handles `eBitDepthFloat` and `eBitDepthHalf`,
* unpremultiplies RGB by A when A > 1e-6, otherwise zeroes RGB,
* rows are read with `getPixelAddress(x1, y1 + y)` so non-zero image origins
  work.

`storeImage`:

* writes as many components as the output clip has,
* converts float32 ↔ float16 with round-to-nearest-even on the mantissa,
* clamps nothing — HDR values pass through (Screen output is already ≤ 1).

### Memory

Working set is `sum(pass.w × pass.h) × 16 bytes` for the octave pyramid plus
two full-resolution buffers (source and output). With the default Standard
preset and 75 % resolution this is roughly 1.3× the frame size at 1080p — well
below the original's FBO set because the pyramid is rebuilt one octave at a
time and the small levels dominate the count.

### Error handling

* OFX exceptions (`throwSuiteStatusException`) are used for host-visible
  failures; no exception escapes `render`.
* The pipeline is fail-safe: zero-size inputs return an empty result, the
  caller leaves the output untouched.
* An abort callback is polled between octaves and during the upsample chain,
  so `abort()` from the host cancels long renders.
