# 06 — Limitations, approximations and manual fix list

This port reproduces the *Exponential* glow of DeepGlow 2. The parameter block,
the pass list, the fade/weight tables and the GLSL bodies are transcriptions of
the shipped binary; everything that could not be proven from it is marked as an
interpretation below, together with the evidence and the place to look if it
turns out to be wrong.

Nothing in this document is a to-do list for the original plugin — it is the
honest boundary of what a CPU, OFX-side reconstruction can claim.

---

## 1. Verification status

| Area | Status | Evidence / note |
|---|---|---|
| Parameter list, ids, ranges, defaults, choice values | **byte-exact** | `work/paramtable.txt`, `docs/05-ofx-mapping.md`; 45 controls + 11 groups verified by `DGHostCheck` |
| Pass builder (sizes, weights, fades, steps, strides) | **byte-exact** | transcription of `0x00e600`; invariants asserted in `DGSmoke` §7 |
| Pass count = 10 in Exponential mode | **proven** | `0x28a303` writes `0x6fac = 10` on every preset |
| GLSL surfaces (all 10 shaders) | **byte-exact** | `extracted/shaders/*.glsl`, hashes in `docs/03-shaders.md` |
| CPU kernels (pre/post/blur/composite) | **faithful transcription** | `DGColor.h`, `DGPipeline.cpp`; no reference renders to diff against |
| Built artifact | **loads, 2 exports** | `LoadLibrary` OK; `dumpbin /exports` = `OfxGetNumberOfPlugins`, `OfxGetPlugin` |
| Glow footprint vs Radius / Resolution | **measured** | RMS radius test in `DGSmoke` §8 (32.97 px @100 %, 33.17 px @50 %, target 33) |
| "Resolution" semantics | **interpretation** | reading A, see `docs/04-pipeline.md` §8 |
| OFX choice-index mapping | **guarded by tests** | `dg::debugAxesFromChoice` + `DGHostCheck` option order + `DGSmoke` §9 |
| Glow on a transparent comp | **guarded by tests** | source premultiplied, glow additive, alpha unchanged; `DGSmoke` §10 |
| Lens dirt | **approximation** | built from the recovered uniforms, not from a working AE reference |
| Lens Iris mode | **not ported** | renders the Exponential path instead |
| Pixel-exact match to AE | **unverified** | Not every setting was tested against exact same Situation in After Effects |

The last row is the important one: the port is *algorithmically* faithful, but
no frame has been compared pixel-by-pixel against After Effects. Diffing a few
frames at Radius 400 / 1000 on the same comp is the single most valuable
follow-up (see §7, item 1).

Two practical lessons from the first visual comparison round, both of which can
make a *correct* build look wrong:

* **Check which binary is running.** Resolve keeps using the previously loaded
  `DeepGlow2.ofx` until it restarts, and the all-users install directory needs an
  elevated copy — a failed overwrite is silent from the host's point of view, so
  the render shows the old algorithm. Compare `Get-FileHash` of
  `build\Release\DeepGlow2.ofx` with the installed
  `Contents\Win64\DeepGlow2.ofx` before drawing conclusions (README, *Install*).
* **A missing axis looks like a wrong blur.** The Debug View default was once
  mapped onto the Height-only path, so every default render blurred only
  vertically and produced long vertical streaks. Any time the glow looks
  directional, render with Debug View = *Blur Width* / *Blur Height* and confirm
  that only the expected axis changes.
* **A glow that adds nothing at all is usually an alpha bug.** The first build
  premultiplied the whole composite by the source alpha, so on a title over a
  transparent background the halo was multiplied by `a = 0` and the render
  contained no glow whatsoever while still looking like a plausible "clean
  title". A histogram is the fastest tell: a working render has a continuous
  range of values between the background and the glyphs, a broken one has the
  background's grey levels and the glyphs and nothing in between. Fixed in the
  composite (`05-ofx-mapping.md` §5) and pinned by `DGSmoke` §10.

---

## 2. Performance

The original is GPU-only (OpenGL 3.3, RGB16F ping-pong FBOs). This port is
single-threaded scalar C++ on the CPU, so it is 1–2 orders of magnitude slower.
Measured on the development machine (Release x64, one frame, best of two):

### 1920 × 1080

| Setting | Work buffer | Time |
|---|---|---|
| **defaults** (Radius 1000, Standard, Res 75 %) | 1440 × 810 | **1.95 s** |
| Radius 400, Res 25 % | 480 × 270 | 0.25 s |
| Radius 400, Res 50 % | 960 × 540 | 0.84 s |
| Radius 400, Res 75 % | 1440 × 810 | 1.79 s |
| Radius 400, Res 100 % | 1920 × 1080 | 3.17 s |
| Radius 1000, Draft | 1440 × 810 | 0.44 s |
| Radius 1000, Low | 1440 × 810 | 0.72 s |
| Radius 1000, Medium | 1440 × 810 | 1.47 s |
| Radius 1000, Standard | 1440 × 810 | 1.97 s |
| Radius 1000, High | 1440 × 810 | 4.30 s |
| Radius 1000, Extreme | 1440 × 810 | 5.75 s |
| Radius 0 / 50 / 200 / 500 / 2000 (Standard) | 1440 × 810 | 0.15 / 0.94 / 1.40 / 1.60 / 2.71 s |

### 3840 × 2160

| Setting | Work buffer | Time |
|---|---|---|
| defaults | 2880 × 1620 | **8.0 s** |

Reproduce with `work/bench.cpp` (`work/build_bench.bat`).

**Practical guidance**

* Interactive work: keep Radius ≤ 500, use **Draft** or **Medium**, Resolution
  50 %. That is 0.25–0.7 s per frame — usable with Resolve's frame cache.
* The cost is dominated by the widest 2–3 octaves; below Radius ~200 the pass
  list truncates itself and the effect gets cheap fast.
* 4K/8K at default settings is a "render and wait" operation.
* Resolve caches the rendered frame; scrubbing re-renders. Set the timeline to
  half resolution (or use a proxy) when dialling the look in.

**Biggest available speed-up:** row-parallelism. Every kernel (`blurPass`,
`resample`, `upsampleTent`, `downsample13`, the composite loop) is a pure
per-output-pixel function of the input, so a `parallel_for` over `y` with
`std::thread::hardware_concurrency()` workers should give ~6–10× on a modern
8/16-core CPU with no change in output. This was left out deliberately: the
reference implementation is easier to audit single-threaded, and threading is a
local change that does not touch the recovered math.

---

## 3. Structural approximations

### 3.1 Octave levels are direct resamples (not a 13-tap mip chain)

The original builds one FBO per octave and fills it from the previous level with
the 13-tap downsample (shader 03). This port instead resamples the
pre-processed image directly to each octave's buffer size
(`resample(pre, workW, workH)`).

* Same footprint, same stride math, same weights.
* Difference: the original's deep octaves have passed through N generations of
  the 13-tap filter (each roughly a 1.4-texel Gaussian), which slightly smooths
  them. In this port the octave is a single bilinear sample of an already
  band-limited buffer, so it is marginally sharper before the blur.
* In practice that octave is then blurred by a kernel hundreds of pixels wide,
  so the prefilter is invisible. The choice was made because a *crop*
  (`blitCenter`) made deep octaves see only the frame centre — a real, visible
  bug.

If a difference ever shows up at very large radii, the fix is to build the
pyramid with `downsample13` again but from the previous octave's *full* buffer
rather than a crop.

### 3.2 `blurAccel`: wide kernels are evaluated on a downsampled copy

For more than 40 taps per side the buffer is halved (13-tap) until the tap
count is ≤ 32, the tap count is divided and the stride multiplied by the same
factor, and the result is resampled back.

* Kernel extent and Gaussian shape in UV space are unchanged.
* The kernel is sampled more coarsely, and the octave is prefiltered before the
  blur.
* Because the blur removes the high frequencies anyway, measured glow profiles
  are unaffected (RMS test), but this is not bit-identical to the original.

### 3.3 256 taps per side

`kMaxTapsPerSide = 256` in `blurPass`. Above it, the tap direction is stretched
by `raw/clamped`, so the Gaussian keeps its span and shape and only loses
sampling density. The clamp is reached by the wide presets: Extreme at
Radius 1000 already asks for 500 taps per side on the widest octave (Custom
with Steps 0.8 / Weight 0.75 asks for 300), while Standard peaks at 97 and
never hits it. The shape error is negligible because the normalised Gaussian is
smooth at the sampling interval; the clamp exists purely to bound CPU time and
memory.

### 3.4 No small-value cull on the octave path

Shader 00 zeroes channels with `|c| < 1e-5` when filling a buffer. `blitCenter`
implements that, but the hot path uses `resample`, which does not cull. Effect:
values below 1e-5 survive the additive chain instead of being flushed. In float
this is far below display precision (and below half-float precision in the
original), so it cannot be seen; it is listed for completeness. Add the same
test to `resample` if bit-level parity on black frames ever matters.

### 3.5 float32 vs half

The original's "Half Float" buffer switch is not exposed; the port is float32
throughout. This makes the port slightly *more* precise than the original at
every intermediate, which is the safe direction: no accumulated quantisation is
introduced that the original did not have.

### 3.6 Mirror-repeat can fold the halo at low Resolution

`GL_MIRRORED_REPEAT` is the recovered wrap mode (angle mode switches to
clamp-to-border). At 25 % Resolution the widest octave's kernel is wider than
the reduced buffer, so the mirrored copy of the frame is folded back into the
blur: the result gets brighter and wider. This is inherent to the recovered
algorithm with a reduced buffer, not a port bug — the original shows the same
behaviour at the same settings. Use ≥ 50 % Resolution for very large radii if
the halo looks wrong.

### 3.7 Glow aberration is tap-count based

The shader does `nf = max(STEPS * multiplier, 1.0)` and keeps the stride, so the
multiplier changes the *number of taps*, not the kernel width. Consequences the
port reproduces exactly:

* at multiplier 100 % all three channels are identical — glow aberration is a
  no-op unless at least one multiplier differs;
* small deviations (95–105 %) change the tap count by a few percent and are
  almost invisible; the control is meant for 50–150 % ranges;
* the loop bound is a float, so tap positions are fractional and the effective
  count is `floor(2·nf)+1`. Reproduced in `blurPass` with a float loop.

### 3.8 Lens dirt

The original renders a dirt/matte chain from an AE layer with its own exposure,
gamma, inherit and matte views. The port takes a second OFX input, builds the
matte as `luma(pow(dirt·2^exposure, gamma))`, mixes it with `inherit` and
applies `opacity`, and offers the three dirt view modes. The uniforms match the
recovered ones, but the pipeline around them is a reasonable reconstruction, not
a proven transcription. Expect the *character* to match, not the exact frame.

---

## 4. Interpreted behaviour (unproven, with fallback plan)

| Behaviour | Interpretation implemented | If it turns out wrong |
|---|---|---|
| **Resolution** slider | sampling quality: buffers shrink, strides/steps stay in full-comp units, footprint constant (reading A) | reading B (glow shrinks at low settings) — change `workW/workH` and derive `steps/stride` from them in `buildPasses` |
| **Fade ramp index** | pass `i>0` uses ramp entry `i-1`, applied on the width pass only | compare mid-radius falloff at Radius 300 / 600 / 900 against AE; the constants are at `0x00e93d` |
| **Extreme preset** | per-pass fade `clamp(0.2·max(spanX,spanY),0,1)`, no truncation, deepest pass forced to fade 1 / size ≥ 1, preset internally rewritten to Custom | re-read `0x00eb97`–`0x00ec57` |
| **Glow iterations** | no-op in Exponential mode (pass count is fixed at 10) | if AE's control visibly changes the exponential glow, the fixed write of `0x6fac` was misread |
| **Weight for pass 0** | Custom → `customWeight`; preset ≥ Standard → 1.0; else quality weight; deeper passes multiply | re-read `0x00e7xx`; visible as a global brightness shift of the glow |
| **Angle mode** | two full 2D directional passes (θ with `stepsX`/fade, θ+π/2 with `stepsY`/fade 1), stride `cos/(w·density)` / `sin/(h·density)`, wrap → clamp-to-border | compare an anisotropic angle render; code is in `DGPipeline.cpp`, `enableAngle` branch |
| **Pixel aberration offset** | isotropic in pixels, `offset / imageHeight` on both axes | shader takes a `vec2`; if AE stretches it with the comp aspect, divide x by width instead |
| **Inner/Outer bias** | not exposed: shader 05 has no bias uniform, it mixes with `min(colour,1)` | if a bias control is required, it has to be re-derived from the Cairo UI code, not the shader |

---

## 5. Features that are not implemented

| Feature | Consequence |
|---|---|
| **Lens Iris / Iris Blur SDK** (`glowMode = 2`, iris layer, iris sampling quality, iris iterations, radii easing, astigmatism group) | the control exists for UI parity; selecting it renders the Exponential path. The iris shader lives in a separate licensed SDK and was out of scope. |
| **AE layer pickers** (Mask Layer, Texture/Lens Dirt layer) | replaced by optional OFX input clips `Mask` and `LensDirt`. Without a clip, the parameters have no effect. |
| **AE masks / track mattes / parenting** | the host applies those around the effect; only the optional mask *input* is supported (alpha or luma, invertible). |
| **Cairo effect UI, AEGP menus, About/Support, aescripts licensing** | replaced by the native OFX parameter page. Nothing about the original UI (look or layout) is reproduced. |
| **Deprecated group** (Edge Behaviour, Renderer, Metal GPU Capture, old Gamma Correction, Lens View), Buffer Expansion Mode, Half Float, Repeat Edges, Simplified Exposure, developer diagnostics (`Show Buffers`, `Shaders Per Frame`, …) | not exposed. The port always mirrors, always uses float32, always allocates what it needs. |
| **Iris-only controls** (Iris Sample Quality, Iris Cull Small Values, Render Quality/Quantization) | not exposed. |
| **8/16-bit integer output** | `render()` throws `kOfxStatErrUnsupported`; float or half only. Resolve's float pipeline is the assumption. |
| **Temporal / multi-frame / field rendering** | single-frame spatial effect; motion blur and field order are the host's business. |
| **GPU acceleration, tiles, multi-resolution** | tiles are disabled and RoI is the full frame, because a multi-octave glow is non-local. Resolve therefore renders it on the CPU and without tiling. |

---

## 6. Host-behaviour caveats

* **Full-frame RoI**: a single output pixel can depend on every source pixel
  (mirror-repeat even pulls in the opposite edge), so the effect requests the
  whole region of definition. On large comps this is the reason memory and time
  scale with frame size.
* **No identity configuration**: even Radius 0 screens a zero-extent glow over
  the source, so `isIdentity()` always returns false. Disable/Bypass the node
  instead of expecting a pass-through.
* **Alpha**: the pipeline unpremultiplies on load (skipped when `a ≤ 1e-6`),
  works on straight colour, and premultiplies on store unless **Unmult** is on.
  On a fully transparent frame the glow is therefore empty unless Unmult is
  enabled — the AE behaviour the checkbox exists for.
* **Abort granularity**: `abort()` is polled between octaves and between
  upsample steps, not inside a kernel. A drag on a 4K Extreme render can take
  up to a few seconds to cancel.
* **Multi-threaded rendering**: the effect holds no mutable per-frame state
  (parameters are re-read per render), so multiple render threads per instance
  are safe. The kernels themselves are single-threaded (§2).

---

## 7. Manual fix list (priority order)

> **Already fixed once:** early builds mapped the Debug View choice index as if
> it were the 1-based AE enum, so the *default* ("None", index 2) selected the
> Height-only debug path. The default render dropped the X pass and looked
> vertically streaked. The indices now live next to the parameter model
> (`dg::kDebugView*` / `dg::debugAxesFromChoice`) and are pinned by `DGSmoke` §9
> and `DGHostCheck`. This is the class of defect that only an OFX-level render
> test catches — see item 2.

1. **Compare against After Effects.** Render the same 400×400 comp with a small
   white dot at Radius 50 / 200 / 1000 and at Resolution 25 / 50 / 100, then
   diff the glow profiles. This is the only missing piece of validation, and it
   decides items 3–6 below.
2. **End-to-end OFX render test.** `DGHostCheck` validates registration but
   never calls `render()`; `DGSmoke` calls the pipeline directly and so cannot
   see glue bugs (choice mapping, alpha, depth conversion). Extend the mini host
   with `clipGetImage` returning real float/half images and assert the rendered
   pixels. The debug-view regression shows why this is worth the effort.
3. **Thread the kernels** (`blurPass`, `resample`, `upsampleTent`,
   `downsample13`, composite). Pure row-parallel work, ~6–10× on typical CPUs,
   no change to the math. Highest practical value.
4. **Resolution semantics.** If AE's footprint changes with the slider, switch
   to reading B (`docs/04-pipeline.md` §8) by deriving `steps/stride` from the
   reduced buffers in `buildPasses`.
5. **Fade ramp / preset weights.** If the falloff at mid radii is off, the
   ramp index mapping (`i-1`) or the pass-0 weight rule is the suspect; the
   constants are byte-exact, the indexing is inferred.
6. **Lens Iris.** Port the iris path (separate shader set, `0x67`–`0x77`,
   `0x83`, `0x92`) if the full plugin is required. Out of scope here, and
   blocked on the same missing reference renders.
7. **Lens dirt fidelity.** Replace the reconstructed matte chain with a
   transcription of the original dirt passes once the iris work is done; the
   uniforms are already in `Params`.
8. **Small-value cull.** Add the `1e-5` flush to `resample` if bit-level parity
   on black frames matters (§3.4).
9. **8/16-bit output.** If a host other than Resolve needs integer output,
   either convert in `storeImage` or declare the effect as float-only through
   `ClipPreferences` instead of throwing.

---

## 8. What is *not* a limitation

For clarity, these are faithful and need no fixing:

* the exact parameter table (ids, ranges, defaults, choice value holes);
* the pass list for every preset at any comp aspect (bit-identical strides);
* the fixed 10-pass exponential structure and its halving rule;
* the GLSL math of all ten shaders, including the `.gbr`/`.brg` channel-slot
  oddity (the port works in true RGB, so R/G/B multipliers map to the real
  channels);
* the pre/post-process operation order, threshold modes, sat-bias, tonemap
  curves and gamma handling;
* the screen/add composite and the AE premultiplication contract.
