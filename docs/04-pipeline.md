# 04 — The glow pipeline

This is the core document: how the original turns a comp into a glow, and how
the port reproduces it. Everything here comes from three places in the binary:

| Subject | RVA | Notes |
|---|---|---|
| pass-list builder | `0x00e600`–`0x00f014` | sizes, weights, fade, steps, strides |
| per-pass uniforms | `0x01a240`–`0x01b10e` | stride/angle/fade/aberration uniforms |
| setup | `0x028910`–`0x028d83` | presets, density, pass count, aberration flags |
| render | `0x015140` | loop structure (mip build, blur, upsample, composite) |
| RoI helper | `0x00f020` | `size0 · 0.7 · 0.7` (see §9) |

Source of truth for the pixel math is the GLSL in `extracted/shaders/`
(documented in `03-shaders.md`); this file is about the *orchestration*.

---

## 1. Render recipe

```
comp (W×H)
  │
  ├─ upload to a working texture at  W·res × H·res          (res = Resolution/100)
  ├─ preProcess (shader 06)                                 → Pre
  │
  ├─ loop A: for each octave i
  │     Src_i = downsample(Src_{i-1})      (13-tap, shader 03)   Src_0 = Pre
  ├─ loop B: for each octave i
  │     Buf_i = blurW(Src_i) ; Buf_i = blurH(Buf_i)              (shader 02)
  ├─ loop C: for i = n-1 … 1
  │     Buf_{i-1} += upsample(Buf_i)       (3×3 tent, shader 07)
  │
  ├─ postProcess (shader 05)                                 → Glow
  ├─ [lens dirt]
  └─ composite: out = screen(src·op, Glow) | add(src·op, Glow)
```

Three properties matter:

1. **The octaves are independent blurs of the same image**, not a progressive
   blur: each `Src_i` is a mip level of `Pre` and each `Buf_i` gets its own
   Gaussian. The octave *width* doubles every level, so the sum of ten
   Gaussians approximates one huge, soft falloff.
2. **The upsample chain is additive.** Level `i` is upsampled into level `i-1`
   and added, so energy accumulates geometrically towards the sharp levels.
3. **Only `.rgb` is ever blurred.** Alpha is reattached at composite time.

---

## 2. Where the radius lives

The setup function writes `ctx+0x7064 = radius · compAspect`, i.e. the radius
in **comp pixels** (Radius is authored in comp-*height* units). Then the pass
builder halves it:

```c
// 0x18000e647 .. 0x18000e68f
size = radius * compAspect;
count = 0;
while (count < requested) {
    count++;
    size *= 0.5;
    if (size <= 1.0) break;      // "we reached single-pixel blur, stop here"
}
```

`requested` is `ctx+0x6fac`. In **Exponential** mode the setup function writes
`0x6fac = 10` for every quality preset (`0x28a303`); only the Lens Iris branch
(`0x289f13`) copies "Glow Iterations" into it. That is why *Glow Iterations has
no effect on Exponential glows* — the port keeps the control for UI parity and
uses a hard-coded 10.

Then the per-pass loop **doubles** the size back up:

```
preSize(0) = size0
preSize(i) = preSize(i-1) * 2
```

so the widest pass is `size0 · 2^(count-1)`, which for the full ten passes is
**`radius · compAspect / 2`** pixels (radius 1000 in a 16:9 comp → 888.9 px, a
1778 px diameter). Small radii break out of the halving loop early, which
reduces the number of octaves — that is the only way the octave count changes.

> Worked: radius 30, 16:9 → `size0 = 53.3`; halving gives 26.7, 13.3, 6.7, 3.3,
> 1.7, 0.83 ≤ 1 → `count = 6` passes, widest span 26.7 px.

---

## 3. Pass list, phase 1: size, weight, fade

Record layout (stride `0x38`) is documented in `02-architecture.md`; the port's
`dg::PassInfo` mirrors it.

### Weight (the buffer scale of an octave)

| pass | weight |
|---|---|
| `i = 0` | Custom → `qualityWeight`; preset ≥ Standard (4) → `1.0`; else `qualityWeight` |
| `i > 0` | Custom → `qualityWeight`; else `qualityWeight · weight(i-1)` |

so for Standard (0.9) the octave weights are `1.0, 0.9, 0.81, 0.729, …`, and for
Extreme (1.0) they stay at `1.0`. `qualityWeight` is `ctx+0x70a8`, from the
table in §4; for Custom it is `customWeight/100` (`+0x4b40 × 0.01`).

### Size

`size(i) = preSize(i) · weight(i)`. The blur span in comp px is
`spanX = size · aspectX`, `spanY = size · |2 − aspect|`.

### Fade (per-octave attenuation)

| case | fade |
|---|---|
| Extreme preset | `clamp(0.2 · max(preSize·aspectX, preSize·aspectY), 0, 1)` |
| Custom preset, or `i == 0` | `1.0` |
| otherwise | `clamp((radius − lo[i−1]) / span[i−1], 0, 1)` |

with the ramp table (`0x18000e93d..`):

| `i−1` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| lo | 0 | 0 | 5 | 10 | 20 | 35 | 60 | 90 | 110 | 130 |
| span | 20 | 40 | 55 | 70 | 100 | 105 | 100 | 90 | 110 | 150 |

A pass whose fade hits **exactly 0** truncates the list **after itself**
(`0x18000eb7b`); the Extreme branch jumps over that test. In practice this is
the "small radius → fewer octaves" rule: with radius 5, pass 1 gets
`(5−0)/40 = 0.125` and pass 2 gets `(5−5)/55 = 0` → two passes.

### Extreme post-pass

`0x18000eb97..0x18000ec57`: the *last* pass gets `fade = 1.0` and
`size = max(1.0, size)` (applied **before** the phase-2 weight multiply), and
the preset index is rewritten to 8 (Custom) — i.e. Extreme is a one-shot preset,
afterwards the effect is in Custom mode.

---

## 4. Pass list, phase 2: buffers, density, steps, strides

### Density

`density = qualitySteps / compAspect` (`0x28a2e0`), where `qualitySteps` comes
from the quality table and, for Custom, is the raw **Steps Multiplier**
parameter (no `/100`):

| preset | density (before `/compAspect`) | weight |
|---|---|---|
| 1 Draft | 0.25 | 0.80 |
| 2 Low | 0.25 | 0.85 |
| 3 Medium | 0.33 (`0.3300000131`) | 0.90 |
| 4 Standard | 0.50 | 0.90 |
| 5 High | 0.50 | 0.97 |
| 6 Extreme | 1.00 | 1.00 |
| 8 Custom | `stepsMultiplier` | `customWeight/100` |

### Buffer dims, taps, strides

```
w(i)      = round(compW · weight(i))          // virtual, full-comp based
h(i)      = round(compH · weight(i))
stepsX(i) = max(1, round(density · spanX))
stepsY(i) = max(1, round(density · spanY))
strideX(i)= (spanX / stepsX) / w(i)           // UV units per tap
strideY(i)= (spanY / stepsY) / h(i)
```

Because `spanX = preSize · weight · aspectX` and `w = compW · weight`, the
weight cancels in the UV stride: `strideX = preSize·aspectX / (stepsX·compW)`.
**The blur footprint in comp pixels is `preSize·aspectX`, independent of the
quality weight** — the weight only changes how densely that footprint is
sampled.

---

## 5. Angle mode ("Enable Angle")

When the checkbox is on:

* the wrap mode becomes `GL_CLAMP_TO_BORDER` (black outside),
* the two separable passes become two **full 2D** directional passes:

| pass | direction | taps | fade |
|---|---|---|---|
| 1 (width pass) | `θ` | `stepsX` | `fade(i)` |
| 2 (height pass) | `θ + π/2` | `stepsY` | `1.0` |

with the strides taken from the *virtual* dims and the density:

```
du = cos(θ)   / (w · density)
dv = sin(θ)   / (h · density)
```

(helper `0x1a2f5..0x1a39a`; the `+π/2` rotation is the `1.5707963` at
`0x1803dc740`, added in the second branch). Both stride components stay
non-zero (`isAngleB`), and the shader only multiplies by the fade when
`isBlurWidthB`, which is exactly pass 1. Angle conversion:
`deg = fmod(param/65536, 360)`, `rad = deg·π/180`.

Note that the span no longer multiplies the stride — the extent is
`stepsX·du = spanX·cos θ / w`, i.e. the same footprint projected onto the
direction.

---

## 6. Worked example — 1920×1080, Radius 1000, Standard, Resolution 75 %

`compAspect = 1.7778`, `size0 = 1000·1.7778/2^10 = 1.736`, `count = 10`,
`density = 0.5/1.7778 = 0.28125`, `aspectX = aspectY = 1`.

| i | preSize | weight | size | fade | w | stepsX | strideX | workW×workH |
|---|---|---|---|---|---|---|---|---|
| 0 | 1.74 | 1.0000 | 1.736 | 1.000 | 1920 | 1 | 0.000904 | 1440×810 |
| 1 | 3.47 | 0.9000 | 3.125 | 1.000 | 1728 | 1 | 0.001808 | 1296×729 |
| 2 | 6.94 | 0.8100 | 5.625 | 1.000 | 1555 | 2 | 0.001809 | 1166×656 |
| 3 | 13.89 | 0.7290 | 10.125 | 1.000 | 1400 | 3 | 0.002411 | 1050×590 |
| 4 | 27.78 | 0.6561 | 18.225 | 1.000 | 1260 | 5 | 0.002893 | 945×531 |
| 5 | 55.56 | 0.5905 | 32.805 | 1.000 | 1134 | 9 | 0.003214 | 850×478 |
| 6 | 111.11 | 0.5314 | 59.049 | 1.000 | 1020 | 17 | 0.003405 | 765×430 |
| 7 | 222.22 | 0.4783 | 106.288 | 1.000 | 918 | 30 | 0.003859 | 689×387 |
| 8 | 444.44 | 0.4305 | 191.319 | 1.000 | 826 | 54 | 0.004289 | 620×349 |
| 9 | 888.89 | 0.3874 | 344.374 | 1.000 | 744 | 97 | 0.004772 | 558×314 |

Footprint `stepsX·strideX·1920` = 1.7, 3.5, 6.9, 13.9, 27.8, 55.5, 111.2, 222.3,
444.7, 888.7 px — the doubling chain, ending at `radius·compAspect/2`.

The same table with **Extreme** (`density 0.5625`, weight 1.0) has every pass at
1920×1080 and 500 taps on the last one — ~4× the pixels and ~5× the taps per
axis, which is why Extreme costs roughly 20× Standard.

**Custom** (`steps 0.8`, `weight 0.75`) gives constant weights and
`density = 0.45`, i.e. 1.6× the tap density of Standard at 44 % of the
resolution: sharper, band-free, slower.

**Aspect 1.5** doubles `spanX` and halves `spanY`
(`spanY = size·|2−aspect|`): `stepsX/stepsY` become `145/48` on the last pass and
the footprint is elliptical (1333×444 px).

Tables can be regenerated with `work/dbg_passes.cpp`.

---

## 7. The port's render loop

`dg::renderGlow` (`src/DGPipeline.cpp`):

```cpp
baseW/baseH   = round(comp·resolution)              // Resolution slider
compAspect    = compW/compH
working       = resample(source, baseW, baseH)
preProcessImage(working, pre, p, view == Glow Input)
passes        = buildPasses(p, compAspect, compW, compH)   // virtual dims
for each pass:
    buf  = resample(pre, pass.workW, pass.workH)     // full-frame octave level
    buf  = blurHoriz(buf, stepsX, strideX, fade)     // shader 02
    buf  = blurVert (buf, stepsY, strideY, 1.0)
    levels.push(buf)
for i = n-1 … 1: levels[i-1] += upsampleTent(levels[i], levels[i-1])
postProcessImage(levels[0])
[applyDirt]
composite with the *original* source (screen or add); premultiply the source
term only, keep the glow additive, output alpha = source alpha (1.0 if Unmult)
```

Differences from the original's resources, all deliberate:

| original | port | why |
|---|---|---|
| mip chain built by repeated 13-tap downsample of the previous level | each level is a direct bilinear resample of `Pre` | avoids ten generations of resampling blur; same footprint |
| one FBO per octave + ping-pong blur buffers | one `ImageF` per octave | simpler, same math |
| `w/h` = FBO size; `Resolution` = upload size | `workW/workH` = buffer size; strides from the **virtual** `w/h` | reproduces the recovered stride math exactly (see §8) |
| half-float intermediates (`Half Float`) | float32 only | no visual difference beyond quantisation |

`blurPass` is a literal transcription of shader 02, including the fractional
tap grid of the glow-aberration path; `blurAccel` evaluates very wide kernels on
a 13-tap-downsampled copy with the stride scaled by the same factor (CPU
performance, `06-limitations.md`).

`blitCenter` (shader 00) is no longer on the hot path: a "centre crop" would
make deep octaves see only the middle of the frame. The original never crops
either — its octave buffers are full-frame levels of the mip chain.

---

## 8. What "Resolution" does (interpretation)

The `Resolution` slider (25–100, default 75) is the one parameter whose exact
role could not be proven from the binary: `ctx+0x24/+0x28` (the dims used to
size the upload) could not be traced back to a writer. Two readings are possible:

* **A — sampling quality (implemented).** The working buffers are
  `comp · resolution · weight`, while steps and strides are derived from the
  *full-comp* `w/h`. The glow footprint therefore does not move; only the texel
  density of the blur changes. Large blurs stay cheap at low settings.
* **B — blend-towards-source.** Lower resolution would shrink the glow and rely
  on upsampling for softness.

Reading A is implemented and tested:

* `buildPasses` gives **bit-identical** `steps·stride·compW` extents for
  resolution 1.0 / 0.75 / 0.5 / 0.25 (`DGSmoke`, section 7);
* an end-to-end RMS-radius measurement of a blurred blob gives 32.97 px at
  100 % and 33.17 px at 50 % (`DGSmoke`, section 8).

At 25 % the widest kernel is larger than the reduced buffer, so
`GL_MIRRORED_REPEAT` folds the halo back in and the result brightens
perceptibly — this is inherent to the (recovered) wrap mode, not a port bug.

---

## 9. Additional recovered facts

### RoI helper (`0x00f020`)

The output region of interest is the source dilated by `size0 · 0.7 · 0.7`
(`0.9 · 0.7` when the angle is enabled). `0.7·0.7 = 0.49` is the
"3σ ≈ kernel radius" rule for the shader's Gaussian,
`σ = 0.3·steps·stride`. Note it uses `size0`, the *smallest* octave — the RoI
only bounds what the host must supply, not what the effect visibly touches. The
port requests the full frame because tiles are disabled anyway.

### Composite

Fixed-function blending in the original (no shader): the glow is blended onto
the *already uploaded source* in the output buffer.

| blend | formula |
|---|---|
| Screen | `out = src·op + glow − src·op·glow` (shader 05 clamps the glow to 1) |
| Add | `out = src·op + glow` |

`Source Opacity` multiplies the source before compositing; `Unmult` decides
whether the result is written straight or premultiplied (see
`05-ofx-mapping.md` §5).

Alpha note: in the original `src` is premultiplied by AE, so `src·op` already
carries the source alpha while the glow term does not. The port reproduces that
by premultiplying only the source (`src_straight · op · a`) and leaving the glow
additive; the output alpha stays the source alpha. That is what keeps the halo
visible on a transparent comp — see `05-ofx-mapping.md` §5 and the regression in
`DGSmoke` §10.

### Lens dirt

Applied *after* postProcess, in comp space, before the composite: the dirt
matte is `luma(dirt · 2^matteExposure)^matteGamma`, the dirt colour is mixed
towards the local glow colour by **Inherit Glow Color**, and the result is
added to the glow scaled by `Opacity · matte`. The port follows this recipe
(`applyDirt`), with the exact sampling of the original's two-pass dirt shader
not recovered — see `06-limitations.md`.

### Debug axes

`Debug View = Blur Width` bypasses the **height** pass, `Blur Height` bypasses
the **width** pass (`bypassB` in shader 02); the port returns the raw glow
without postProcess for these views. `Pre Proc Solid Fill` replaces the
pre-processed input with white so the kernels are visible.

---

## 10. Code map

| Recovered | Port |
|---|---|
| `0x00e600` pass builder | `dg::buildPasses` (`src/DGParams.h`) |
| quality table `0x28a229` | `dg::qualityFromPreset` |
| fade ramp `0x00e93d` | `dg::fadeRamp` |
| `0x01a240` uniform helper | `renderGlow` angle/stride block |
| shader 02 | `blurPass` / `blurAccel` (`src/DGPipeline.cpp`) |
| shader 03 | `dg::downsample13` |
| shader 07 | `dg::upsampleTent` |
| shader 06 / 05 | `preProcessImage` / `postProcessImage` (`src/DGColor.h`) |
| composite `0x1bb50` | final loop in `renderGlow` |
| dirt | `applyDirt`, `dg::applyMask` |

---

## 11. Constants used

All of these are IEEE-754 **doubles** in `.rdata` (the code converts the float
parameters with `cvtps2pd` before using them), except the single-pixel stop test
which is a `float` compared with `comiss`.

| meaning | address | value |
|---|---|---|
| single-pixel stop | `0x1803dc6fc` | `1.0f` |
| Extreme fade factor | `0x1803dc708` | 0.2 |
| halving factor | `0x1803dc710` | 0.5 |
| RoI dilation factor | `0x1803dc718` | 0.7 |
| Standard/High octave weight | `0x1803dc728` | 0.9 |
| one | `0x1803dc730` | 1.0 |
| `π/2` (angle 2nd pass) | `0x1803dc740` | 1.5707963267948966 |
| `param → deg` unit | `0x1803dfbc0` | 1/65536 |
| `deg → rad` | `0x1803dfbe0` | 0.017453292519943295 |
| `|2 − aspect|` | `0x1803dfc00` | 2.0 |
| angle wrap modulus | `0x1803dfc48` | 360.0 |
| Gaussian constant | in shader 02 | `0.06` (`= 0.3²·σ_units`)|
