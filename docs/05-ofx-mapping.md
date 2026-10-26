# 05 — AE → OFX parameter mapping

The original exposes 80+ controls through its own Cairo-drawn ECW page. The port
exposes the ones that affect the Exponential render path, with the **same names,
defaults, ranges and units** as in After Effects, so existing looks can be
rebuilt by copying numbers.

Everything here was recovered from the UI definition table
(`work/uiparams.txt`, `work/paramtable.txt`): each control's `+0x00` field is
the parameter id, `+0x68/+0x6c/+0x70/+0x74` are min/soft-min/soft-max/max, and
`+0x3c` is the widget style + item list.

---

## 1. Plugin identity

| | |
|---|---|
| OFX id | `com.deepglow.DeepGlow2` |
| label | `Deep Glow 2` |
| version | 1.0 |
| grouping | `DeepGlow` |
| contexts | `filter`, `general` |
| bit depths | `float`, `half` |
| threading | `setHostFrameThreading(false)` — the render is single threaded |
| tiles | **off** (the blur is non-local) |
| multi-resolution | off |
| temporal access | off |
| identity | never (`isIdentity` returns false) |

### Clips

| clip | components | optional | replaces |
|---|---|---|---|
| `Source` | RGBA / RGB / Alpha | no | AE source layer |
| `Output` | RGBA | — | — |
| `Mask` | RGBA / Alpha | yes (`isMask`) | AE **Mask Layer** (id `0x96`) |
| `Lens Dirt` | RGBA / RGB | yes | AE lens-dirt **Texture** (id `0x56`) |

OFX has no "layer" parameter, so the two AE layer pickers become optional input
clips. `Mask` is multiplied into the glow input *after* preProcess;
`Lens Dirt` is added to the glow before the composite.

---

## 2. Parameter table

`AE id` is the recovered control id. `×` lists the unit conversion applied in
`DeepGlowEffect::buildParams` (OFX value → scene value).

### Main

| OFX name | label | type | default | range (display) | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `glowMode` | Glow Mode | choice | 0 Exponential | Exponential, Lens Iris | `0x67` | — | Lens Iris is **not** ported; selecting it renders Exponential |
| `blendMode` | Blend Mode | choice | 0 Screen | Screen, Add | `0x18` | — | |
| `radius` | Radius | double | 1000 | 0…2000 (0…500) | `0x11` | — | comp-height units |
| `exposure` | Exposure | double | 1 | 0…20 (0.9…1.1) | `0x12` | — | |

### Input

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `thresholdMode` | Threshold Mode | choice | 0 Chrominance | Chrominance, Luminance | `0x5f` | — | |
| `threshold` | Threshold | double | 0 | 0…500 (0…200) | `0x14` | 0.01 | percent → scene |
| `thresholdSmooth` | Threshold Smooth | double | 0 | 0…100 | `0x15` | 0.01 | |
| `satBias` | Saturation Bias | double | 0 | −100…100 | `0x60` | 0.01 | sign inverts the mask |

### Input Masking

| OFX name | label | type | default | range | AE id | notes |
|---|---|---|---|---|---|---|
| `maskEnabled` | Enable Mask | bool | false | — | `0x96` (layer) | enable the `Mask` clip |
| `maskMode` | Mask Mode | choice | 0 Alpha | Alpha, Luminance | `0x97` | |
| `maskInvert` | Mask Invert | bool | false | — | `0x98` | |

### Style

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `gammaCorrect` | Enable Gamma Correction | bool | false | — | `0x1a` | — | AE calls it "Auto Detect Gamma" |
| `gamma` | Gamma Correction | double | 100 | 0…300 (0…100) | `0x9a` | 0.01 | |
| `aspectRatio` | Aspect Ratio | double | 1 | 0…2 | `0x1f` | — | |
| `enableAngle` | Enable Angle | bool | false | — | `0x20` | — | switches to clamp-to-border |
| `angle` | Aspect Angle | double | 0 | −180…180 | `0x21` | — | degrees |

### Chromatic Aberration

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `aberrationPixel` | Enable Pixel Aberration | bool | false | — | `0x24` | — | |
| `aberrationChannels` | Aberration Channels | choice | 0 Red & Green | R&G, R&B, G&B | `0x25` | — | enum 1/2/3 |
| `pixelOffset` | Pixel Offset | double | 1 | −1000…1000 (−5…5) | `0x26` | — | relative to frame height |
| `aberrationGlow` | Enable Glow Aberration | bool | false | — | `0x24` | — | the original reuses id `0x24` for both enables |
| `multR` | Multiply Red | double | 80 | 0…200 (50…150) | `0x61` | 0.01 | |
| `multG` | Multiply Green | double | 100 | 0…200 (50…150) | `0x62` | 0.01 | |
| `multB` | Multiply Blue | double | 120 | 0…200 (50…150) | `0x63` | 0.01 | |

### Tint

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `tintMode` | Tint Mode | choice | 0 Off | Off, Monocolor, Duocolor | `0x2b` | — | AE list has a separator at index 1 (`Off, (-, Mono, Duo`), hence the value holes in §3 |
| `tintBlend` | Tint Blend Mode | choice | 0 Multiply | Multiply, Overlay, Soft Light | `0x43` | — | enum 1/2/3 |
| `tintColor` | Color | RGBA | 1,1,1,1 | 0…4 | `0x2a` | — | |
| `tintColorOuter` | Color Outer | RGBA | 0,0,0,1 | 0…4 | `0x42` | — | Duocolor only |
| `tintStrength` | Tint Strength | double | 100 | 0…200 | `0x2c` | 0.01 | |

> **Inner/Outer Bias** (`0x44`, default 50) is *not* exposed: shader 05 mixes
> inner→outer with `min(colour, 1)` and has no bias uniform, so the control has
> no effect on the recovered path.

### Tone Mapping

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `tonemapMode` | Operation | choice | 0 None | None, ACES Filmic, Filmic, Hable Filmic, Reinhard, Reinhard 2, Clamp Chroma | `0x5d` | — | enum {1,3,4,5,6,7,9} |
| `tonemapMix` | Mix | double | 100 | 0…100 | `0x52` | 0.01 | |

### Lens Dirt

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `dirtEnabled` | Enable Lens Dirt | bool | false | — | `0x56` (layer) | — | enable the `Lens Dirt` clip |
| `dirtOpacity` | Opacity | double | 100 | 0…100 | `0x58` | 0.01 | |
| `dirtInherit` | Inherit Glow Color | double | 0 | 0…100 | `0x94` | 0.01 | |
| `dirtMatteExposure` | Matte Exposure | double | 0 | −10…10 (−2…2) | `0x59` | — | in stops |
| `dirtMatteGamma` | Matte Gamma | double | 1 | 0.1…5 (0.1…2) | `0x5a` | — | |

### Quality

| OFX name | label | type | default | range | AE id | notes |
|---|---|---|---|---|---|---|
| `qualityPreset` | Quality Preset | choice | 3 Standard | Draft…Extreme, Custom | `0x30` | enum {1,2,3,4,5,6,8} |
| `resolution` | Resolution | double | 75 | 25…100 | `0x31` | percent |
| `stepsMultiplier` | Steps Multiplier | double | 0.5 | 0.01…3 (0.01…1) | `0x32` | Custom preset only |
| `glowIterations` | Glow Iterations | int | 8 | 6…10 | `0x33` | **no effect** in Exponential (see `04-pipeline.md` §2) |
| `customWeight` | Custom Weight | double | 0.9 | 0.1…1.0 | — | hidden AE value (`ctx+0x4b40 ×0.01`), exposed for the Custom preset |

### Output

| OFX name | label | type | default | range | AE id | × | notes |
|---|---|---|---|---|---|---|---|
| `view` | View | choice | 0 Final Result | Final, Glow Input, Dirt Input, Dirt Matte, Dirt Matted | `0x35`/`0x87` | — | enum {1,2,4,5,6} |
| `sourceOpacity` | Source Opacity | double | 100 | 0…100 | `0x36` | 0.01 | |
| `unmult` | Unmult | bool | false | — | `0x37` | — | see §5 |

### Debug

| OFX name | label | type | default | range | AE id | notes |
|---|---|---|---|---|---|---|
| `debugView` | Debug View | choice | 2 None | Blur Width, Blur Height, None | `0x88` | choice **order** is AE order; indices are 0/1/2, not the 1-based AE enum, and index 2 must mean a full render |
| `preProcSolidFill` | Pre Proc Solid Fill | bool | false | — | `0x9b` | white input for kernel inspection |

### Groups

`Main`, `Input`, `Input Masking`, `Style`, `Chromatic Aberration`, `Tint`,
`Tone Mapping`, `Lens Dirt`, `Quality`, `Output`, `Debug` — the same nesting as
the AE page, minus the AE-only groups (`About & Support`, `Error Logging`,
`Iris`, `Astigmatism`, `Deprecated`).

---

## 3. Choice values and the holes in them

The AE enums are visible as "separator" items in the UI list. They are kept as
the *values*, not as visible options, so a look that was saved with
`qualityPreset = 5` means the same thing in both plugins:

| parameter | OFX index → value |
|---|---|
| `qualityPreset` | `0→1 Draft, 1→2 Low, 2→3 Medium, 3→4 Standard, 4→5 High, 5→6 Extreme, 6→8 Custom` |
| `tonemapMode` | `0→1 None, 1→3 ACES, 2→4 Filmic, 3→5 Hable, 4→6 Reinhard, 5→7 Reinhard 2, 6→9 Clamp Chroma` |
| `view` | `0→1 Final, 1→2 Glow Input, 2→4 Dirt Input, 3→5 Dirt Matte, 4→6 Dirt Matted` |
| `tintMode` | `0→0 Off, 1→1 Monocolor, 2→2 Duocolor` (AE: Off, separator, Mono, Duo) |
| `aberrationChannels` | `0→1 R&G, 1→2 R&B, 2→3 G&B` |
| `blendMode` | `0→1 Screen, 1→2 Add` |
| `thresholdMode` | `0→1 Chrominance, 1→2 Luminance` |
| `maskMode` | `0→1 Alpha, 1→2 Luminance` |
| `glowMode` | `0→1 Exponential, 1→2 Lens Iris` |

---

## 4. Units

| quantity | AE unit | OFX unit | conversion |
|---|---|---|---|
| Threshold, Threshold Smooth | percent (0–500 / 0–100) | same | `×0.01` in `buildParams` |
| Saturation Bias | percent (−100…100) | same | `×0.01` |
| Gamma Correction | percent (100 = 1.0) | same | `×0.01` |
| Tint Strength | percent (100 = 1.0) | same | `×0.01` |
| Multiply R/G/B | percent (100 = 1.0) | same | `×0.01` |
| Matte Exposure | stops | same | — (`2^x` applied in the kernel) |
| Matte Gamma | gamma | same | — |
| Source Opacity | percent | same | `×0.01` |
| Dirt Opacity / Inherit | percent | same | `×0.01` |
| Resolution | percent | same | `×0.01` |
| Radius | comp-height units | same | — (scaled by comp aspect internally) |
| Aspect Angle | degrees | degrees | `→rad` inside the pipeline |
| Pixel Offset | pixels (height-relative) | same | `/imageHeight` in the shader path |

OFX parameter labels intentionally keep the AE unit (e.g. "100" for gamma) so
the numbers on screen match the AE screenshot a user may be copying from.

`Steps Multiplier` is **not** divided by 100: the original copies the raw
parameter into the density (`ctx+0x6fa8`) on the Custom path.

---

## 5. Alpha and premultiplication

AE hands plugins premultiplied pixels; the shaders only ever sample `.rgb`, so
the arithmetic the original performs is on premultiplied colour. The port
instead:

1. `loadImage()` unpremultiplies (`rgb /= a`) into straight RGBA;
2. runs the whole chain on straight colour;
3. composites, premultiplying **only the source**:

   ```
   sv = src_straight · SourceOpacity · a
   out.rgb = screen(sv, glow)      or   sv + glow
   out.a   = a                     (1.0 with Unmult on)
   ```
4. declares the output as premultiplied (`eImagePreMultiplied`), or
   unpremultiplied with `a = 1` when **Unmult** is on.

Only the source is premultiplied; the glow is **not**. That matters: an
emissive effect must survive where the source has no coverage, and a
premultiplied image is allowed to exceed its own alpha. Every compositor — AE
and Resolve alike — draws such an image as

```
displayed = rgb + (1 - a) · background
```

so on a transparent comp the halo adds over the backdrop, which is exactly how
the original looks. Premultiplying the glow too would multiply the whole halo
by `a = 0` and leave a bare copy of the source. `DGSmoke` §10 pins this: the
mean glow outside the source alpha must be non-zero *and* the source alpha must
not be expanded by the glow.

Where the source is opaque (`a = 1`) both forms are identical, so opaque comps
are unaffected by this choice.

> Historical note: the first build of this port multiplied the *whole* result by
> the source alpha. On a title over a transparent background the render then had
> literally no glow (a screenshot of it contained exactly three values: the two
> checkerboard greys and the white glyphs). See `06-limitations.md` §1.

---

## 6. AE controls that are not exposed

| AE control | id | reason |
|---|---|---|
| Glow Mode → Lens Iris, Iris Layer, Iris Sampling Quality, Iris Iterations, Radii Easing | `0x67`, `0x69`, `0x6b`, `0x76`, `0x77` | Lens Iris render path not ported |
| Astigmatism group (Enable, Force Square Ratio, View Focal Point, Focal Length, Focal Falloff) | `0x46`–`0x4f` | deprecated in the original UI |
| Deprecated group (Edge Behaviour, Renderer, Metal GPU Capture, Gamma Correction (old), Lens View) | `0x3a`, `0x66`, `0x6d`, `0x1b`, `0x5b` | GL/Metal backend switches |
| Buffer Expansion Mode, Aux Buffer Expansion | `0x40`, `0x41` | buffer-only optimisation; the port allocates what it needs |
| Half Float | `0x5e` | float32 throughout |
| Repeat Edges | `0x78` | the port always mirrors |
| Simplified Exposure | `0x8f` | internal A/B switch for the exposure order |
| Show Buffers, Draw Dimensions Comp UI, `cout PreRender Rects`, `cout Buffer Dims`, `Shaders Per Frame` | `0x39`, `0x84`, `0x72`, `0x73`, `0x89` | developer diagnostics |
| Iris Sample Quality, Iris Cull Small Values | `0x83`, `0x92` | Lens Iris only |
| Render Quality (Quantization) | `0x6b`(dup) | Lens Iris only |
| Inner/Outer Bias | `0x44` | no corresponding shader uniform (see §2) |
| About & Support, Error Logging, licensing widgets | `0x9d`, `0x8d`, … | host/licence UI |
| Mask Layer, Texture (layer pickers) | `0x96`, `0x56` | replaced by input clips |
| Auto Detect Gamma (real behaviour) | `0x1a` | detection of the AE working space cannot be reproduced; the port exposes a plain on/off switch |

---

## 7. Host behaviour

* **RoI**: the full region of definition of every connected input is requested
  (a glow pixel can depend on any source pixel). Tiles are disabled, so this
  costs nothing extra.
* **Output components**: `RGBA`. The `Source` clip also accepts RGB and
  alpha-only images; alpha-only is loaded as `(0,0,0,a)`.
* **Depth**: float and half. Half output is rounded to nearest-even.
* **`getClipPreferences`** forces RGBA on every connected clip and sets the
  premultiplication flag from `Unmult` (see §5).
* **Abort**: `Effect::abort()` is polled between octaves and during the
  upsample chain; an aborted render leaves the output buffer untouched.
* **Parameter enabling**: `changedParam` greys out controls that the current
  mode ignores (mask sub-params, aberration channels, tint colours, tone-map
  mix, dirt sub-params, gamma value, angle, custom weight).

---

## 8. Installing in DaVinci Resolve

1. Build `build\bundle\DeepGlow2.ofx.bundle` (see the README).
2. Copy the bundle to
   `C:\Program Files\Common Files\OFX\Plugins\` (all users) or
   `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\OFX\` (this user).
3. Make sure the Microsoft Visual C++ 2015–2022 x64 Redistributable is present
   (the plugin links the dynamic CRT; Resolve normally installs it).
4. Restart Resolve; the effect appears under **Filters → DeepGlow → Deep Glow 2**.
5. Because tiles are disabled and the effect is CPU-only, use the **Standard**
   preset for interactive work and **Extreme** only for final renders.

In After Effects-compatible hosts the parameter names, defaults and units match
the AE plug-in, so a look can be re-entered numerically.
