# 01 — Binary reconnaissance

This document records what was recovered from the shipping After Effects
plug-in and how. It is the evidence base for every other document in this
folder: the shader sources, the parameter tables and the pass-list constants
referenced here were machine-extracted, not transcribed by hand.

---

## 1. Target identity

| Property | Value |
|---|---|
| File | `C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex` |
| Size | 4 516 352 bytes |
| SHA-256 | `b5099e20456b5dee53a3437551a3717c94d7c2b77a6ed1c0cd0cf0b5c6496107` |
| Format | PE32+ (x86-64), DLL |
| Image base | `0x180000000` |
| Entry point | RVA `0x36d9e8` |
| Link timestamp | `1748685085` → 2025-05-31 (UTC) |
| DLL characteristics | `0x2022` (high-entropy VA, dynamic base, NX, no SEH) |
| Subsystem | 2 (GUI) |
| Exports | 337 |

All RVAs in these documents are **file-relative virtual addresses**; add the
image base for a runtime address in a debugger.

### Sections

| Name | RVA | Virtual size | Raw size | Characteristics |
|---|---|---|---|---|
| `.text` | `0x001000` | `0x3d0598` | `0x3d0600` | code, execute/read |
| `.rdata` | `0x3d2000` | `0x06666e` | `0x066800` | read-only data (shaders, strings, tables) |
| `.data` | `0x439000` | `0x00bfb0` | `0x004c00` | read/write data |
| `.pdata` | `0x445000` | `0x010eb4` | `0x011000` | exception unwind (function table) |
| `.rsrc` | `0x456000` | `0x000390` | `0x000400` | resources |
| `.reloc` | `0x457000` | `0x0016f0` | `0x001800` | base relocations |

---

## 2. Method

### Why not a decompiler

A Ghidra session through the REA tooling was attempted first. The mandatory
deep analysis of a 4.5 MB MSVC binary with statically linked Cairo did not
finish inside the tool budget (330 s per attempt), and Rizin was not available
in the environment. The port therefore uses a purpose-built static-analysis
toolchain that targets exactly the questions this port needs:

* which functions exist and where do they start (`.pdata`),
* what does a given function call and which constants/strings does it touch
  (instruction-level xref index),
* what are the exact float/double values behind `rip`-relative loads,
* which byte ranges are GLSL sources.

This gives an exact *address → behaviour* map without a full decompilation.
The trade-off is that control flow is read as annotated assembly rather than
pseudocode; every non-obvious conclusion below is backed by an address and,
where useful, by the extracted shader that implements the same step.

### Tooling (`tools/`)

| Tool | Purpose | Output |
|---|---|---|
| `pe_recon.py` | PE headers, sections, exports, imports, hashes | `work/recon_dg2.json` |
| `parse_pdata.py` | `.pdata` `RUNTIME_FUNCTION` table → exact function ranges | `work/funcs.json` |
| `index_binary.py` | Capstone pass over `.text`; per-instruction xrefs, call graph, string refs | `work/analysis.json` |
| `dump_strings.py` | ASCII + UTF-16 string extraction with RVAs | `work/strings_dg2.txt` |
| `extract_shaders.py` | Byte-exact slicing of `// begin shader` … `// end shader` blocks | `extracted/shaders/`, `extracted/shaders.json` |
| `paramtable.py` | Decodes the `PF_ParamDef` blocks written by the custom UI builder | `work/paramtable.txt` |
| `uiparams.py` | Decodes label/type/range/choice stores per UI row | `work/uiparams.txt` |
| `ada.py` | `dis` / `xref` / `callers` / `str` / `find` queries | stdout |
| `flow.py`, `fnflow.py` | Linear flow views with resolved constants and call targets | stdout |
| `fnrange.py` | List functions starting in an RVA range | stdout |
| `fnstr.py`, `funcmap.py`, `where.py` | Attribute strings/xrefs to containing functions | stdout |

Analysis output is kept under `work/` and `extracted/`. Those directories are
**evidence**, not build inputs — the port does not read them at run time.

### Reproducing

```powershell
python tools\pe_recon.py    "C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
python tools\parse_pdata.py "C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
python tools\index_binary.py            # ~1 min, builds analysis.json
python tools\dump_strings.py "C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
python tools\extract_shaders.py
python tools\paramtable.py
python tools\uiparams.py

python tools\ada.py dis 0x28a2e0 64
python tools\flow.py 0x000e600 0x000f014 --all
```

---

## 3. What the binary is

The imports and embedded strings identify it unambiguously as a GPU-only
After Effects effect with a fully custom (non-standard) UI:

* **`OPENGL32.dll`** — 26 entry points (`wglCreateContext`, `glTexImage2D`,
  `glTexSubImage2D`, `glReadPixels`, `glGenTextures`, …). All image processing
  is OpenGL.
* **Export table contains the whole Cairo 2D library** (`cairo_*`, 100+
  exports). Cairo is linked statically and used to draw the custom ECW effect
  controls; `USER32`/`GDI32` provide the window and font plumbing.
* **`aescripts*` exports** and `WININET`/`WS2_32`/`IPHLPAPI` imports — the
  AEScripts licensing/activation client (network + machine-id based).
* **`MSVCP140`/`VCRUNTIME140`** — MSVC C++ runtime, streams and `std::cout`
  debug logging.
* **`EffectMain`** (RVA `0x2f340`) and **`PluginDataEntryFunction2`**
  (`0x2f6f0`) — the After Effects plug-in entry points.

Source paths embedded in `.rdata` confirm the internal layout:

```
c:\users\debug\dropbox\dev\deepglow2\deepglow2\classes\fbo.cpp
/Users/jameswhiffin/Dropbox/Dev/DeepGlow2/DeepGlow2/Shaders/debug/
C:\Users\James Whiffin\Dropbox\Dev\DeepGlow2\DeepGlow2\Shaders\debug\
```

Consequences for the port:

1. There is **no CPU pixel path to lift**. The visual result is defined by the
   ten GLSL programs, so the port implements those programs on the CPU and
   reproduces the host-side GL orchestration in `DGPipeline.cpp`.
2. The AE custom UI is drawn with Cairo. It is not portable to OFX and is
   replaced by a regular OFX parameter page (see `05-ofx-mapping.md`).
3. Licensing, activation, error-log upload and the "About & Support" tab are
   not part of the rendering behaviour and are not ported.

---

## 4. Address map

Function boundaries below come from `work/funcs.json` (`.pdata`), so they are
exact. "Confidence" says how much of the function's behaviour was actually
read, not whether the function exists.

### Entry points and dispatch

| RVA | Size | Role | Confidence |
|---|---|---|---|
| `0x2f340` | — | `EffectMain` (exported); AE command dispatcher | high |
| `0x2f6f0` | — | `PluginDataEntryFunction2` (exported); AE plug-in registration | high |
| `0x1c490` | `0x80e` | Per-frame command orchestrator: sequence setup → render → post → composite | high |
| `0x1bb50` | `0x931` | Final composite of glow over source (Screen / Add), alpha handling | high |

### Render

| RVA | Size | Role | Confidence |
|---|---|---|---|
| `0x15140` | `0x170a` | Main render: GL context, shader compile/link, FBO allocation, pass loop, readback | high |
| `0x1a240` | `0xece` | Per-pass draw helper (bind FBO, set uniforms, draw quad) | medium |
| `0x1b110` | `0xa31` | `postProcess` — binds and runs shader 05 | high |
| `0x1d430` | `0x2ac` | Texture/FBO creation helper | medium |
| `0x1d8d0` | `0x513` | Texture upload (`glTexSubImage2D`) helper | medium |
| `0x1ee30` | `0xe3e` | FBO/draw utilities | low |
| `0x20130` | `0x41b` | GL state / quad VAO helpers | low |

### Setup and pass construction

| RVA | Size | Role | Confidence |
|---|---|---|---|
| `0x28910` | `0x473` | Frame setup: comp aspect, resolution, quality table, iteration count, aberration flags | high |
| `0x28a229`…`0x28a2fb` | — | Quality-preset switch (the constant table) | exact |
| `0x0e600` | `0xa14` | `buildPasses`: octave sizes, weights, fades, buffer dims, tap counts, UV strides | high |
| `0x0f020` | `0x33a` | Pass-builder continuation / list post-processing | medium |

### Shader sources (`.rdata`)

| RVA | File | Program |
|---|---|---|
| `0x3d58e1` | `shader_00_3d58e1.glsl` | centred blit, optional small-value cull |
| `0x3d5d01` | `shader_01_3d5d01.glsl` | integer-offset blit into a larger buffer |
| `0x3d6221` | `shader_02_3d6221.glsl` | separable Gaussian, per-channel (aberration) |
| `0x3d7151` | `shader_03_3d7151.glsl` | 13-tap energy-preserving downsample |
| `0x3d7a01` | `shader_04_3d7a01.glsl` | plain copy |
| `0x3d7ac1` | `shader_05_3d7ac1.glsl` | **postProcess**: exposure, tint, tonemap, gamma |
| `0x3d9da1` | `shader_06_3d9da1.glsl` | **preProcess**: aberration, threshold, exposure, gamma |
| `0x3db361` | `shader_07_3db361.glsl` | 3×3 tent upsample with `in_res`/`out_res` |
| `0x3db981` | `shader_08_3db981.glsl` | vertex shader (with transform) |
| `0x3dba51` | `shader_09_3dba51.glsl` | vertex shader (no transform) |

The extracted files are byte-exact copies of the strings the plug-in passes to
`glShaderSource`; `extracted/shaders.json` keeps the same bytes plus RVAs.

---

## 5. Recovered structures

### Effect state

`rbx` is the effect instance throughout `0x28910`/`0x0e600`. Recovered layout
(offsets are instance-relative, not ABI):

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| `+0x000` | ptr | AE render context / world struct (width at `+0x11c`, height at `+0x120`) | `0x289cd1` |
| `+0x700c` | float | comp aspect `width / height` | `0x289cec` |
| `+0x7040` | float | resolution fraction (Resolution % × 0.01) | `0x289f33` |
| `+0x7064` | float | initial blur size `radius × compAspect` | `0x289d06` |
| `+0x6fa8` | float | steps multiplier (preset ∕ compAspect) | `0x28a2fb` |
| `+0x6fac` | int | pass count (`max(1, glowIterations)`) | `0x289f13` |
| `+0x70a8` | float | per-octave resolution weight | `0x28a2d8` |
| `+0x7080` | float | aspect-ratio parameter | verified |
| `+0x7088` | float | `abs(2 − aspectRatio)` | verified |
| `+0x70ac` | bool | "glow aberration active" | `0x28a387` |
| `+0x70b0/b4/b8` | float | red/green/blue radius multipliers (× 0.01) | `0x28a335` |
| `+0x7010` | vector<float> | per-pass size list | `0x289f3b` |
| `+0x7028` | vector<float> | per-pass weight/fade list | `0x289f50` |

The instance also holds the raw AE parameter values as doubles in a
`PF_ParamDef` array with the standard `0xb0` stride (e.g. `+0x4b40` and
`+0x4bf0` are two adjacent parameters). That stride is what made the parameter
table decodable.

### `PassInfo` (record size `0x38`)

| Offset | Field |
|---|---|
| `+0x00` | float weight |
| `+0x04` | int buffer width |
| `+0x08` | int buffer height |
| `+0x0c` | int steps X |
| `+0x10` | int steps Y |
| `+0x14` | float stride X (UV) |
| `+0x18` | float stride Y (UV) |
| `+0x1c` | float size (buffer pixels) |
| `+0x20` | double span X |
| `+0x28` | double span Y |
| `+0x30` | float fade |

---

## 6. Recovered constant tables

### Quality presets (`0x28a229`…`0x28a2b1`)

| Preset | Steps multiplier | Octave weight |
|---|---|---|
| Draft | `0.25` | `0.80` |
| Low | `0.25` | `0.85` |
| Medium | `0.33` (`0x3ea8f5c3`) | `0.90` |
| Standard | `0.50` | `0.90` |
| High | `0.50` | `0.97` |
| Extreme | `1.00` | `1.00` |
| Custom | steps param `+0x4bf0` | `customWeight param +0x4b40 × 0.01` |

After the switch, the steps value is multiplied by `1 / compAspect`
(`0x28a2e0`: `xmm2` still holds the aspect ratio from `0x289e1d`). This makes
the tap count for a given Radius independent of the comp shape.

### Fade ramps (`0x0e93d`…`0x0eb30`)

`(lo, span)` used as `fade = clamp((radius − lo) / span, 0, 1)` on the width
pass of octave `i ≥ 1` (ramp index `i − 1`):

| i−1 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| lo | 0 | 0 | 5 | 10 | 20 | 35 | 60 | 90 | 110 | 130 |
| span | 20 | 40 | 55 | 70 | 100 | 105 | 100 | 90 | 110 | 150 |

Pass 0 always has fade 1; Extreme and Custom always have fade 1 (Extreme uses
`min(1, 0.2 · max(size·aspectX, size·aspectY))` instead).

---

## 7. Parameter evidence

`tools/paramtable.py` walks the custom-UI builder and decodes each
`PF_ParamDef` immediately after its name is copied into the struct. Output
(`work/paramtable.txt`) is one row per parameter:

```
id=0x11 type=float 'Radius' default=1000 valid=[None,2000] slider=[None,500]
id=0x30 type=None 'Quality Preset' items='Draft|Low|Medium|Standard|High|Extreme|(-|Custom'
```

* `id` is the AE parameter ID (`PF_ParamIndex`), which explains the holes
  (groups and deprecated rows were removed over the product's history).
* `type` is the `PF_ParamType` (float/slider, checkbox, popup, colour, layer,
  group, button).
* `items` uses `|` separators; `(-` marks a disabled separator row.
* `default`, `valid`, `slider` are the decoded `PF_ParamDef` fields; integers
  that were not recognisable as floats are printed as their raw bit patterns.

`tools/uiparams.py` decodes the UI row order and the exact 64-bit constants
loaded for each widget (`sys/ui` flags at `+0x30`, `+0x3c`, ordering at `+0x0c`).
The two files together give the complete AE page layout reproduced in
`05-ofx-mapping.md`.

---

## 8. What was not recovered

| Area | Status | Why it does not block the port |
|---|---|---|
| Lens Iris glow mode | **Out of scope** | Separate shader path; the mode is selectable but renders Exponential instead. |
| Exact GL state / FBO formats | Partially inferred | Only affects rounding; the CPU port uses float32 throughout. |
| Custom UI geometry and Cairo drawing | Not recovered | Replaced by native OFX controls. |
| Licensing / activation / log upload | Not recovered | Not rendering behaviour. |
| AEGP menu commands | Not recovered | Host integration only. |
| AE reference renders | **Missing** | Pixel-exact comparison is blocked; see `06-limitations.md`. |

---

## 9. Confidence summary

* **Exact**: shader text; quality table; fade ramp table; parameter defaults,
  ranges and choice lists; enum values; pass-builder structure.
* **High**: render order; pre/post process order; composite maths; alpha and
  premultiplication handling; RoI requirements.
* **Medium**: which blit shader is used at which point (00 vs 01 vs 04 differ
  only in edge handling); exact buffers used for each intermediate.
* **Low / out of scope**: Iris mode, licensing, AE UI rendering.
