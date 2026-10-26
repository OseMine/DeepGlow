# DeepGlow 2 — OpenFX port

> [!WARNING]
> This project is provided **for educational and research purposes only**. It is
> a reverse-engineering study and does **not** condone or support piracy. The
> original Deep Glow 2 plug-in, its binaries, shaders and branding belong to
> their author; no original code or assets are redistributed here — only a
> re-implementation derived from static analysis of a legally obtained copy.

An OpenFX 1.4 re-implementation of the *Deep Glow 2* After Effects plug-in,
reconstructed from `DeepGlow2.aex` (PE x64, SHA-256
`b5099e20…6496107`, built 2025-05-31) with static reverse engineering. The
plugin runs in DaVinci Resolve and any other OFX 1.4 host on Windows x64, and
reproduces the **Exponential** glow path — all ten GLSL shaders, the pass
builder, the quality/fade tables and the full AE parameter surface — in portable
C++17 on the CPU.

---

## Status

| Area | State |
|---|---|
| Exponential glow (all 10 shader stages) | **implemented** |
| Pass builder: sizes, weights, fades, steps, strides | **byte-exact transcription** of `0x00e600` |
| AE parameters: 45 controls, 11 groups, ids/ranges/defaults | **byte-exact**, native OFX page |
| Quality presets, fade ramps, Extreme rule | **byte-exact constants** |
| Chromatic aberration (pixel + glow), threshold modes, tint, tonemap, gamma | **implemented** |
| Premultiplied AE ⇄ straight OFX alpha, Unmult | **implemented** |
| Optional `Mask` and `LensDirt` input clips | **implemented** |
| Lens Iris mode, iris SDK, iris-only controls | **not ported** (falls back to Exponential) |
| AE layer pickers, Cairo UI, AEGP menus, licensing | **not ported** (native OFX UI instead) |
| GPU acceleration | **not implemented** (CPU, float32, single-threaded) |
| Pixel-exact match against After Effects | **not yet verified** — no reference frames |

Validated by two headless tests (`DGSmoke`, `DGHostCheck`) and by measured glow
footprints; see [Tests](#tests) and [docs/06-limitations.md](docs/06-limitations.md).

---

## Layout

```
src/
  DeepGlowOFX.cpp      OFX glue: plugin factory, clips, 45 parameters, render entry
  DGPipeline.h/.cpp    recovered pass list → blur pyramid → composite (CPU)
  DGParams.h           Params block, quality table, buildPasses(), fade ramps
  DGColor.h            preProcess / postProcess / tint / tonemap kernels (shader 05/06)
  DGImage.h            ImageF, bilinear fetch, 13-tap downsample, tent upsample, resample
tests/
  smoke.cpp            headless pipeline + pass-invariant + RMS-footprint tests
  hostcheck.cpp        in-process mini OFX host: registration/clip/param validation
docs/
  01-recon.md          binary recon: addresses, exports, parameter table, evidence
  02-architecture.md   original vs port structure, data flow, design decisions
  03-shaders.md        shader-by-shader GLSL analysis and CPU mapping
  04-pipeline.md       pass list math, octave/weight/fade tables, worked examples
  05-ofx-mapping.md    parameter/clip/unit/alpha mapping spec + install
  06-limitations.md    approximations, interpreted behaviour, manual fix list
extracted/
  shaders/*.glsl       the 10 GLSL sources, extracted byte-exactly
  shaders.json         extraction report (VAs, sizes, hashes)
work/                  analysis scratch: annotated disassembly, param dumps, generators
CMakeLists.txt         plugin + DGSmoke + DGHostCheck
```

---

## Build

**Requirements**

* Windows 10/11 x64
* Visual Studio 2022 (Desktop development with C++) — validated with MSVC 14.44
* CMake ≥ 3.20
* Microsoft Visual C++ 2015–2022 x64 Redistributable (the plugin links the
  dynamic CRT; it is normally already installed with Resolve)
* DaVinci Resolve installed (it ships the OpenFX 1.4 headers and the OFX Support
  library used by the build)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If Resolve is installed elsewhere, point the build at its OFX SDK:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DOFX_ROOT="D:/Blackmagic/DaVinci Resolve/Support/Developer/OpenFX"
```

`OFX_ROOT` must contain `OpenFX-1.4/include` and `Support/{include,Library}`.
The default is `C:/ProgramData/Blackmagic Design/DaVinci Resolve/Support/Developer/OpenFX`.

**Artifacts**

* `build\Release\DeepGlow2.ofx` — the bare plugin
* `build\bundle\DeepGlow2.ofx.bundle\` — ready-to-install bundle
  (`Contents/Win64/DeepGlow2.ofx`)

---

## Install in DaVinci Resolve

1. Copy `DeepGlow2.ofx.bundle` to one of
   * `C:\Program Files\Common Files\OFX\Plugins\` (all users), or
   * `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\OFX\` (current user).
2. Restart Resolve.
3. Add **Filters → DeepGlow → Deep Glow 2** to a clip.

The all-users path needs an **elevated** copy, and copying over a previous
install fails with *"Access to the path … is denied"* from a normal shell:

```powershell
# from an Administrator PowerShell
Copy-Item X:\coding\DeepGlow\build\Release\DeepGlow2.ofx `
  "C:\Program Files\Common Files\OFX\Plugins\DeepGlow2.ofx.bundle\Contents\Win64\" -Force
```

`work/install_ofx.ps1` does the same thing and writes its result to
`%TEMP%\opencode\install_ofx.log`; run it with
`Start-Process powershell -Verb RunAs -ArgumentList '-File','work\install_ofx.ps1'`.
Before replacing anything, check that the file you are overwriting is really the
old build — the plugin silently keeps running the previous binary otherwise, and
a stale install is indistinguishable from a bad algorithm when you compare
renders. `Get-FileHash` on `build\Release\DeepGlow2.ofx` and the installed
`Contents\Win64\DeepGlow2.ofx` must match.

---

## Using it

The parameter page is the AE effect, renamed into OFX idioms: the same labels,
ranges, defaults and units (Threshold/Smooth/Gamma/Tint/multipliers stay in
0–100 percent, Radius in comp-height units, Resolution 25–100). A look can be
re-entered numerically from an AE screenshot; see
[docs/05-ofx-mapping.md](docs/05-ofx-mapping.md) for every control.

* **Glow Mode** — Exponential (implemented). The Lens Iris entry exists for UI
  parity and renders Exponential.
* **Glow Iterations** — a no-op in Exponential mode: the original forces ten
  passes there (proven at `0x28a303`), so the control only drives Lens Iris.
* **Unmult** — writes straight RGBA with `alpha = 1` and declares the output
  unpremultiplied. Off (default) the result is premultiplied by the source
  alpha, like AE.
* **Mask** / **LensDirt** — optional input clips replacing AE's layer pickers.
  Enable them in the *Input Masking* / *Lens Dirt* groups.
* **Debug views** — *View* selects Input / Dirt Input / Dirt Matte / Dirt Matted
  / Final; the *Debug* group adds width-only and height-only blur axes and a
  solid-fill pre-process.
* **Quality vs speed** — use Draft/Medium and Resolution 50 % while dialling in
  a look; Standard/Resolution 75 % is the AE default and costs ~2 s per 1080p
  frame on a modern CPU (see below).

---

## Tests

No host or GPU needed — both tests are plain console executables:

```bat
build\Release\DGSmoke.exe
build\Release\DGHostCheck.exe
```

**DGSmoke** exercises the pixel pipeline headlessly:

1. baseline: a zero glow leaves the source unchanged for screen and add
2. the glow never darkens values that are already in range
3. every quality preset renders all ten passes
4. feature matrix: threshold modes + sat-bias, aspect ratio, angle mode, pixel
   and glow aberration, tint modes/blends, all tonemap modes, gamma, exposure,
   view modes, debug width/height axes
5. mask multiply + lens dirt
6. abort callback
7. exact pass-builder invariants: `stepsX·strideX·compW == size` for resolution
   1.0/0.75/0.5/0.25 (bit-identical), monotonic growth, deep octaves smaller
8. end-to-end RMS glow radius vs Radius/Resolution (15 % tolerance)
9. Debug View choice mapping and axis semantics: the full render must be
   isotropic, width-only must spread along X, height-only along Y
10. glow survives a transparent background: non-zero emission outside the source
    alpha, and the source alpha is left alone

**DGHostCheck** embeds a minimal OFX host and verifies the registration
surface: one plugin, four clips (Source/Output/Mask/LensDirt), 45 controls plus
the page and 11 groups (57 descriptors), clip preferences and RoI. It fails on
OFX property errors. Pixel output is covered by `DGSmoke`, not by this test.

The built artifact itself was also load-tested: `LoadLibrary` on
`build\Release\DeepGlow2.ofx` succeeds and the binary exports exactly
`OfxGetNumberOfPlugins` and `OfxGetPlugin`.

---

## Performance

Single-threaded float32 CPU, measured Release x64 (best of two, 1920 × 1080):

| Setting | Work buffer | Time |
|---|---|---|
| **defaults** (Radius 1000, Standard, Res 75 %) | 1440 × 810 | **1.95 s** |
| Radius 1000, Draft / Medium / Extreme | 1440 × 810 | 0.44 / 1.47 / 5.75 s |
| Radius 400, Res 25 % / 50 % / 100 % | 480…1920 px wide | 0.25 / 0.84 / 3.17 s |
| defaults at 3840 × 2160 | 2880 × 1620 | **8.0 s** |

Reproduce with `work/bench.cpp` (`work/build_bench.bat`). Row-parallelising the
kernels is the obvious 6–10× speed-up and does not change the math — see
[docs/06-limitations.md](docs/06-limitations.md) §2 and §7.

---

## Documentation

| Document | Contents |
|---|---|
| [01-recon.md](docs/01-recon.md) | target binary, exports, recovered address map, parameter table, toolchain |
| [02-architecture.md](docs/02-architecture.md) | original GPU pipeline vs port, data flow, key design decisions |
| [03-shaders.md](docs/03-shaders.md) | all ten GLSL shaders: uniforms, math, CPU mapping, channel-slot oddity |
| [04-pipeline.md](docs/04-pipeline.md) | pass list, octave/weight/fade/quality tables, worked 1080p examples, Resolution analysis |
| [05-ofx-mapping.md](docs/05-ofx-mapping.md) | plugin identity, clips, 45-control parameter spec, units, alpha contract, install |
| [06-limitations.md](docs/06-limitations.md) | verification status, performance, approximations, unproven behaviour, manual fix list |

The extracted GLSL in `extracted/shaders/` is authoritative for the algorithm;
the docs quote it directly.

---

## Known limitations (short version)

* **CPU only, single-threaded** — 2 s per 1080p frame at defaults, 8 s at 4K.
* **Lens Iris is not ported**; selecting it renders the Exponential path.
* The **Resolution** slider is interpreted as sampling quality; the exact
  original role could not be proven from the binary (evidence and the fallback
  reading are in `04-pipeline.md` §8).
* Octave buffers are full-frame resamples rather than a 13-tap mip chain, wide
  kernels are evaluated on a downsampled copy, and taps are clamped at 256 per
  side — deliberate CPU trade-offs with matched glow footprints.
* Output depth is float/half only; 8/16-bit integer output is rejected.

Full list, with the manual fix priority order: [docs/06-limitations.md](docs/06-limitations.md).

---

## Legal

This repository contains **no original DeepGlow code, binaries, shaders or
artwork**. The GLSL files under `extracted/` are quotations of the analysed
target kept for documentation and research; the port in `src/` is an
independent C++ implementation of the same published algorithm. Deep Glow is a
trademark of its respective owner. No licence is granted for the original
plug-in, and this port is not affiliated with or endorsed by its author.

Use at your own risk; no warranty of any kind.
