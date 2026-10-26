# 03 — Shaders

All ten GLSL programs were extracted byte-exactly from `.rdata`
(`extracted/shaders/*.glsl`). They are the authoritative description of the
look. This document lists each program, its uniforms, the exact math, and the
CPU code that replaces it.

---

## 1. Index

| # | RVA | Stage | Purpose | CPU replacement |
|---|---|---|---|---|
| 00 | `0x3d58e1` | blit | centred rescale, optional small-value cull | `dg::blitCenter` |
| 01 | `0x3d5d01` | blit | integer-offset blit into a larger buffer | `dg::blitCenter` |
| 02 | `0x3d6221` | blur | separable Gaussian, per-channel taps | `blurPass` / `blurAccel` |
| 03 | `0x3d7151` | downsample | 13-tap energy-preserving downsample | `dg::downsample13` |
| 04 | `0x3d7a01` | copy | straight texture copy | `dg::resample` / assignment |
| 05 | `0x3d7ac1` | post | exposure, tint, tonemap, gamma | `dg::postProcessImage` |
| 06 | `0x3d9da1` | pre | aberration, threshold, exposure, gamma | `dg::preProcessImage` |
| 07 | `0x3db361` | upsample | 3×3 tent with `in_res`/`out_res` | `dg::upsampleTent` |
| 08 | `0x3db981` | vertex | transformed quad | n/a |
| 09 | `0x3dba51` | vertex | untransformed quad | n/a |

---

## 2. The swizzle (read this first)

Shaders 05 and 06 do not operate in RGB order:

```glsl
// shader 06
FragColour = texture(videoTexture, out_uvs).gbr;
...
FragColour = FragColour.brg;      // only for VIEW_INPUT
// shader 05
FragColour = texture(videoTexture, out_uvs).brg;
```

AE uploads the layer without a channel swap; the effect compensates inside the
shaders so that its internal vector order is **B, R, G** (component 0 = blue,
1 = red, 2 = green). Every `.rgb` value written between the two programs stays
in that order, which is why the Gaussian (shader 02) can sample normally.

Two consequences matter for a re-implementation:

1. The luminance dot products look "wrong" in the extracted source:

   ```glsl
   dot(FragColour.rgb, vec3(0.7152, 0.0722, 0.2126))
   ```

   With the internal order (B,R,G) this is
   `0.2126·R + 0.7152·G + 0.0722·B` — the standard Rec.709 luma.

2. The glow-aberration channel mapping in shader 02 is not a bug:

   ```glsl
   FragColour.r = sampleRed(greenMultF);   // internal .r = green
   FragColour.g = sampleGreen(blueMultF);  // internal .g = blue
   FragColour.b = sampleBlue(redMultF);    // internal .b = red
   ```

   In RGB terms: **R uses Multiply Red, G uses Multiply Green, B uses Multiply
   Blue**.

The port works in plain RGB throughout. The swizzles disappear, and the luma
coefficients are used in their un-swizzled form
(`0.2126 R + 0.7152 G + 0.0722 B`).

---

## 3. Shader 02 — separable Gaussian (the core)

```glsl
uniform float STEPS;          // taps per side
uniform float strideX, strideY;
uniform float fadeMultiplier;
uniform bool  isAngleB;       // directional mode
uniform bool  isBlurWidthB;   // true on the horizontal pass
uniform bool  is_glow_aberrationB;
uniform float redMultF, greenMultF, blueMultF;
uniform bool  bypassB;        // debug: skip this axis
```

### Kernel

```glsl
float getWeight(float x) { return exp(-0.5 * (x*x) / 0.06); }
```

with `x = i / STEPS`, so the Gaussian σ is `0.3 · STEPS` in *step* units. Each
pass is normalised by the weight sum, therefore the filter is energy
preserving for any tap count.

### Normal path

```glsl
for (float i = -STEPS; i <= STEPS; i++) {
    total       += getWeight(i / STEPS) * texture(videoTexture, uv + i*stride).rgb;
    totalWeight += getWeight(i / STEPS);
}
FragColour = total / totalWeight;
if (isBlurWidthB) FragColour *= fadeMultiplier;
```

* `strideX` is non-zero on the horizontal draw and `strideY` on the vertical
  draw (the host sets the other to zero).
* `fade` is applied **only on the width pass** and therefore propagates
  through the height pass. That is the recovered per-octave attenuation.

### Aberration path

When `is_glow_aberrationB` is set, each channel is filtered separately:

```glsl
stepsF = max(STEPS * multiplier, 1.0);   // multiplier = red/green/blue %
// stride is NOT scaled by the multiplier
for (i = -stepsF; i <= stepsF; i++)
    sum += getWeight(i / stepsF) * texture(videoTexture, uv + i*axisStride).channel;
```

So the aberration changes the **number of taps**, not the stride: a channel
with a 0.8 multiplier gets fewer samples in the same spatial span, producing a
slightly tighter/aliased kernel. The port reproduces exactly this.

### CPU implementation

`blurPass()` in `DGPipeline.cpp` evaluates shader 02 directly on the CPU:

* uniforms `steps[c]` for `c ∈ {0,1,2}`, a single tap direction `(dirU, dirV)`,
  `fade` and `wrap`;
* a fast path with one shared weight table when all three channels have the
  same tap count (the common case), and a per-channel path otherwise;
* tap counts are clamped to 256 per side to bound CPU time, and the tap
  direction is stretched by `rawTapCount / clampedTapCount` so the kernel
  **span** is unchanged (only sampling density drops);
* the buffer is sampled with `ImageF::fetch`, which is a bilinear fetch in
  normalised UV with mirror-repeat / clamp-to-edge / clamp-to-border.

`dirU`/`dirV` is `(strideX, 0)` on the width pass and `(0, strideY)` on the
height pass. When "Enable Angle" is on both components are non-zero and the
two passes are drawn at θ and θ+π/2, matching the shader's `isAngleB` branch.

`blurAccel()` wraps it for very wide kernels (more than 40 taps per side): the
buffer is downsampled by a power of two, the tap count and stride are divided/
multiplied by the same factor, the blur runs there and the result is resampled
back. The kernel extent in UV space is preserved; only the sampling density
drops. This is a CPU performance approximation (see `06-limitations.md`), not
part of the original algorithm.

---

## 4. Shader 06 — preProcess

```glsl
uniform int   gammaCorrect;
uniform float threshold, thresholdSmooth, satBiasF;
uniform float exposure;
uniform int   viewE, blendModeE;
uniform int   threshold_modeE, aberration_modeE;
uniform vec2  aberration_amountF;
uniform int   simple_exposureB;
uniform float custom_gammaF;
```

Order of operations (each step is visible in `main`):

1. **Read + swizzle** `.gbr`.
2. **Pixel aberration** (before thresholding, per the source comment):

   | mode | +offset sample | −offset sample |
   |---|---|---|
   | `R_G` (1) | red → internal blue | green → internal red |
   | `R_B` (2) | red → internal blue | blue → internal green |
   | `G_B` (3) | green → internal red | blue → internal green |

   In RGB terms: R/G, R/B or G/B are split by ±`aberration_amountF`.
   `aberration_amountF` is a UV offset; the port uses
   `pixelOffset / imageHeight` on both axes (isotropic in pixels).

3. **Clamp negatives to 0.**
4. **Threshold** (only if `threshold > 0`):
   * *Luma*: `mask = smoothstep(thr − smooth, thr + smooth, luma)`, applied to
     all channels.
   * *Chroma*: per-channel
     `C *= smoothstep(thr − smooth, thr + smooth, C)`. If `satBiasF != 0`,
     compute saturation `s` from the **original** colour (`rgb2s`), invert it
     when the bias is negative, build a mask from `s`, and blend the
     thresholded colour towards the original with `abs(satBiasF)`.
   * `rgb2s` normalises HDR values by their max before computing saturation.

5. **View early-out** (`VIEW_INPUT`): swizzle back to RGB, optionally apply
   `pow(1/gamma)`; note the tail of the shader then applies `pow(gamma)`, so
   the two cancel. The port skips both (documented in `DGColor.h`).
6. **Exposure / clamp**:
   * Screen: `min(colour, 1)` — exposure is deferred to postProcess;
   * Add: `colour *= exposure`;
   * `simple_exposureB == 1`: no exposure here at all.
7. **Gamma**: `pow(colour, gamma)`.

Port: `preProcessImage()` follows the same order. `doThreshold` is gated on
`threshold > 0`. `gammaOn` requires both the checkbox and `gamma != 1` (a no-op
either way).

---

## 5. Shader 05 — postProcess

```glsl
uniform sampler2D videoTexture;
uniform sampler2D inputTexture;   // declared, never sampled
uniform int   gammaCorrect, blendMode, tintMode, tintMultiB;
uniform float exposure, tintMix;
uniform vec3  tintColor, tintColorOuter;
uniform int   tonemapE;
uniform float clampMixF;
uniform int   simple_exposureB;
uniform float custom_gammaF;
```

Order:

1. read + swizzle `.brg`;
2. exposure: `simple_exposureB` **or** Screen → `*= exposure` (Add already
   multiplied during preProcess);
3. tint (only when `tintMix > 0`; tint-off forces the mix to 0 upstream);
4. tonemap (only when `tonemapE >= ACES` **and** `clampMixF > 0.01`);
5. gamma `pow(1/gamma)`;
6. Screen → `min(colour, 1)`.

### Tint (`DoTint`)

`tintMode` is used as the **blend mode selector** (1 multiply, 2 overlay,
3 soft light); the Off/Mono/Duo choice lives in the host parameters.

| blend | formula |
|---|---|
| Multiply | `C · tint` |
| Overlay | `pow(overlay(pow(C, 0.45), pow(tint, 0.45)), 2.22)` |
| Soft Light | `softLight(min(C,1), tint)` with the pegtop formula |

Duocolor (`tintMultiB == 1`) computes the same operation for `tintColorOuter`
and mixes inner→outer by `min(C, 1)`. Note the **exponent quirk**: the inner
overlay uses `2.22`, the outer one `2.2`. The port preserves it.

Final: `C = mix(C, tinted, tintMix)`, then `max(C, 0)` to kill NaNs from
`tintMix > 1`.

### Tonemap (`DoTonemap`)

| value | curve |
|---|---|
| 3 ACES | `clamp((x(2.51x+0.03))/(x(2.43x+0.59)+0.14), 0, 1)` |
| 4 Filmic | `X=max(0,x−0.004); pow((X(6.2X+0.5))/(X(6.2X+1.7)+0.06), 2.2)` |
| 5 Hable | `uncharted2(x·2) · 1/uncharted2(11.2)` |
| 6 Reinhard | `x/(x+1)` |
| 7 Reinhard 2 | `x(1+x/16)/(1+x)` (`L_white = 4`) |
| 9 Clamp Chroma | desaturate towards luma by `clamp(luma,0,1)`, clamp negatives |
| 1, 2, 8 | no-op (None / Break markers) |

Then `mix(original, tonemapped, clampMixF)`.

The `rand()` helper at the bottom of the file is dead code.

Port: `postProcessImage()` + `applyTint()`. `doTonemap` requires
`mode >= ACES` and `mix > 0.01`, exactly like the shader.

---

## 6. Shader 00 / 01 / 04 — blits

**00** — centred rescale:

```glsl
vec2 previousCoords = (uv * currentSize - (currentSize - previousSize) * 0.5) / previousSize;
FragColor = texture(videoTexture, previousCoords).rgb;
if (cull_small_valuesB == 1)  // |c| < 1e-5 → 0
```

**01** — integer offset into a larger buffer; outside the input it writes
`vec4(0,0,0,1)`.

**04** — plain copy.

Port: the hot path uses `resample(src, w, h)` instead of a centre crop. The
original runs 00 to rescale from the previous octave; a crop would make deep
octaves see only the frame centre, so the recovered behaviour (a full-frame
rescale of the pre-processed image to the octave's buffer size) is reproduced
with a bilinear clamp-to-edge resample (a plain copy when the sizes match).

`blitCenter(src, w, h, cullSmallValues)` in `DGImage.h` remains as the literal
implementation of 00 (centre crop/pad maths, black outside, optional 1e-5
cull) for reference and for 01/04, but it is no longer on the render path.

---

## 7. Shader 03 — 13-tap downsample

The standard "next generation post processing" 13-tap filter:

```
a b c      weights:  e·0.125
  j k              (a+c+g+i)·0.03125
d e f              (b+d+f+h)·0.0625
  l m              (j+k+l+m)·0.125
g h i
```

`srcResolution` is the input size; taps are at ±1 and ±2 texels. The result is
energy preserving (0.125·5 + 0.03125·4 + 0.0625·4 = 1). `downsample13` is a
literal transcription (including the mirror-repeat wrap).

This is used by the port's mip-accelerated blur path (see
`06-limitations.md`), not by the main octave chain.

---

## 8. Shader 07 — tent upsample

```glsl
aspectRatio = in_res / out_res;
offset      = (1/in_res) * aspectRatio;
// 1/16 * [1 2 1; 2 4 2; 1 2 1]
```

The `in_res`/`out_res` correction makes the 3×3 footprint cover the same UV
span when the resolution ratio is not exactly 2. `upsampleTent` reproduces it:

```
ox = (inW/outW)/inW, oy = (inH/outH)/inH
```

and then the 4/2/1 weights divided by 16.

The upsample chain is **additive** in the host code (`in_res`/`out_res` per
level, then add into the previous level), which is why the octave weights look
like a geometric series in the final image.

---

## 9. Vertex shaders (08, 09)

Both are trivial quad shaders; 08 applies a `mat4 xform`, 09 does not. They
have no pixel-processing content and no CPU equivalent is needed.

---

## 10. Numerical notes

* GLSL `pow` on negative bases is undefined; shader 06 clamps to 0 before the
  threshold and the port uses `safePow` (`max(0, x)`).
* GLSL `smoothstep(e0, e1, x)` with `e0 == e1` is undefined; the port returns a
  hard step.
* The Hable curve's `eps` is `1e-5` (the comment mentions `0.0001` for a
  variant that is not used).
* Texture filtering is bilinear; the CPU `fetch` matches it, including GL's
  texel-centre convention (`u·w − 0.5`).
* `GL_CLAMP_TO_BORDER` uses the default border colour (transparent black); the
  port returns zero for all four components, matching the `.rgb` read.
