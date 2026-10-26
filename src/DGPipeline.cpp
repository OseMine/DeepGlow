// Deep Glow 2 — OFX port
// DGPipeline.cpp : see DGPipeline.h
#include "DGPipeline.h"

#include <cstring>

namespace dg {
namespace {

inline void addInto(ImageF& dst, const ImageF& src) {
    const size_t n = std::min(dst.px.size(), src.px.size());
    for (size_t i = 0; i < n; ++i) dst.px[i] += src.px[i];
}

ImageF downsampleTimes(const ImageF& src, int times) {
    ImageF cur = src;
    for (int i = 0; i < times; ++i) cur = downsample13(cur);
    return cur;
}

// Safety valve for the CPU implementation: the original shaders run one loop
// iteration per tap with no upper bound.  We cap the tap count and scale the
// stride so the kernel *extent* (sigma in UV) stays identical.
constexpr int kMaxTapsPerSide = 256;

// ---------------------------------------------------------------------------
// One directional blur pass — a literal transcription of shader 02.
//
//   normal path:
//       for (i = -STEPS; i <= STEPS; ++i)
//           w = exp(-0.5*(i/STEPS)^2 / 0.06)      // getWeight()
//           total += w * texture(uv + i*(strideX,strideY))
//       out = total / sum(w) * (isBlurWidth ? fade : 1)
//
//   glow-aberration path (per channel c):
//       nf = max(STEPS * multiplier[c], 1.0)      // float loop bound!
//       for (float i = -nf; i <= nf; i += 1)
//           w = exp(-0.5*(i/nf)^2 / 0.06)
//       => the tap count is floor(2*nf)+1 and tap positions are fractional.
//
// `dirU`/`dirV` are the UV offsets of a single tap.  For the (non-angle) width
// pass dirV is zero, for the height pass dirU is zero; with "Enable Angle" both
// components are non-zero and the shader's isAngleB branch keeps them as-is.
// ---------------------------------------------------------------------------
void blurPass(const ImageF& src, ImageF& dst, const int steps[3], const float mult[3],
              bool aberration, float dirU, float dirV, float fade, Wrap wrap) {
    dst.resize(src.w, src.h);

    float out[3] = {0.0f, 0.0f, 0.0f};

    if (!aberration) {
        // ---- single kernel, all channels --------------------------------
        const int raw = std::max(steps[0], std::max(steps[1], steps[2]));
        const int n = std::clamp(raw, 1, kMaxTapsPerSide);
        const float spanScale = static_cast<float>(raw) / static_cast<float>(n);
        const float du = dirU * spanScale;
        const float dv = dirV * spanScale;
        const float inv = 1.0f / static_cast<float>(n);

        std::vector<float> weights(static_cast<size_t>(2 * n + 1));
        float totalWeight = 0.0f;
        for (int i = -n; i <= n; ++i) {
            const float x = static_cast<float>(i) * inv;
            const float w = std::exp(-0.5f * x * x / 0.06f);
            weights[static_cast<size_t>(i + n)] = w;
            totalWeight += w;
        }
        const float norm = fade / std::max(1e-20f, totalWeight);

        for (int y = 0; y < src.h; ++y) {
            for (int x = 0; x < src.w; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.w);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.h);
                float acc[4] = {0, 0, 0, 0};
                float tmp[4];
                for (int i = -n; i <= n; ++i) {
                    src.fetch(u + static_cast<float>(i) * du, v + static_cast<float>(i) * dv, wrap,
                              tmp);
                    const float w = weights[static_cast<size_t>(i + n)];
                    acc[0] += tmp[0] * w;
                    acc[1] += tmp[1] * w;
                    acc[2] += tmp[2] * w;
                }
                float* o = dst.at(x, y);
                o[0] = acc[0] * norm;
                o[1] = acc[1] * norm;
                o[2] = acc[2] * norm;
                o[3] = 1.0f;
            }
        }
        return;
    }

    // ---- per channel kernels (glow aberration) --------------------------
    float nf[3], kernNorm[3];
    std::vector<std::vector<float>> weights(3);
    for (int c = 0; c < 3; ++c) {
        float f = std::max(static_cast<float>(steps[c]) * mult[c], 1.0f);
        // clamp while preserving the kernel width
        if (f > static_cast<float>(kMaxTapsPerSide)) f = static_cast<float>(kMaxTapsPerSide);
        nf[c] = f;
        std::vector<float> w;
        float totalWeight = 0.0f;
        for (float i = -nf[c]; i <= nf[c]; i += 1.0f) {
            const float x = i / nf[c];
            const float wt = std::exp(-0.5f * x * x / 0.06f);
            w.push_back(wt);
            totalWeight += wt;
            if (w.size() > 4096) break;
        }
        kernNorm[c] = fade / std::max(1e-20f, totalWeight);
        weights[c] = std::move(w);
    }

    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.w);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.h);
            for (int c = 0; c < 3; ++c) {
                float acc = 0.0f;
                float tmp[4];
                float i = -nf[c];
                for (size_t k = 0; k < weights[c].size(); ++k, i += 1.0f) {
                    src.fetch(u + i * dirU, v + i * dirV, wrap, tmp);
                    acc += tmp[c] * weights[c][k];
                }
                out[c] = acc * kernNorm[c];
            }
            float* o = dst.at(x, y);
            o[0] = out[0]; o[1] = out[1]; o[2] = out[2];
            o[3] = 1.0f;
        }
    }
}

// Mip-accelerated wrapper: very wide kernels are evaluated on a downsampled
// copy of the buffer.  The stride is multiplied by the same factor, so the
// kernel keeps its extent in UV space and only the sampling density drops
// (the same trade-off the original makes with its per-octave weight).  This
// is a CPU performance approximation — see docs/06-limitations.md.
void blurAccel(const ImageF& src, ImageF& dst, const int steps[3], const float mult[3],
               bool aberration, float dirU, float dirV, float fade, Wrap wrap) {
    const int maxSteps = std::max(steps[0], std::max(steps[1], steps[2]));
    if (maxSteps <= 40 || src.w < 8 || src.h < 8) {
        blurPass(src, dst, steps, mult, aberration, dirU, dirV, fade, wrap);
        return;
    }

    int k = 1;
    int times = 0;
    while (maxSteps / k > 32 && k < 64) {
        k *= 2;
        ++times;
    }

    ImageF small = downsampleTimes(src, times);
    int steps2[3];
    for (int c = 0; c < 3; ++c) {
        steps2[c] = std::max(1, static_cast<int>(std::lround(static_cast<double>(steps[c]) / k)));
    }
    ImageF blurred;
    blurPass(small, blurred, steps2, mult, aberration, dirU * static_cast<float>(k),
             dirV * static_cast<float>(k), fade, wrap);
    dst = resample(blurred, src.w, src.h);
}

void applyMask(const ImageF& mask, ImageF& pre, const Params& p) {
    ImageF m = resample(mask, pre.w, pre.h);
    for (int y = 0; y < pre.h; ++y) {
        for (int x = 0; x < pre.w; ++x) {
            const float* mp = m.at(x, y);
            float v = (p.maskMode == 2)
                          ? (0.2126f * mp[0] + 0.7152f * mp[1] + 0.0722f * mp[2])
                          : mp[3];
            if (p.maskInvert) v = 1.0f - v;
            v = clampf(v, 0.0f, 1.0f);
            float* o = pre.at(x, y);
            o[0] *= v; o[1] *= v; o[2] *= v;
        }
    }
}

void applyDirt(const ImageF& dirt, ImageF& glow, const Params& p) {
    ImageF d = resample(dirt, glow.w, glow.h);
    const float opacity = static_cast<float>(p.dirtOpacity);
    if (opacity <= 0.0f) return;
    const float inherit = static_cast<float>(p.dirtInherit);
    const float exposure = std::pow(2.0f, static_cast<float>(p.dirtMatteExposure));
    const float mgamma = static_cast<float>(std::max(0.01, p.dirtMatteGamma));

    for (int y = 0; y < glow.h; ++y) {
        for (int x = 0; x < glow.w; ++x) {
            const float* dp = d.at(x, y);
            float* gp = glow.at(x, y);
            float matte = (0.2126f * dp[0] + 0.7152f * dp[1] + 0.0722f * dp[2]) * exposure;
            matte = std::pow(std::max(0.0f, matte), mgamma);
            float rgb[3] = {dp[0] * exposure, dp[1] * exposure, dp[2] * exposure};
            for (int k = 0; k < 3; ++k)
                rgb[k] = std::pow(std::max(0.0f, rgb[k]), mgamma);
            if (inherit > 0.0f) {
                const float l = 0.2126f * gp[0] + 0.7152f * gp[1] + 0.0722f * gp[2];
                for (int k = 0; k < 3; ++k)
                    rgb[k] = rgb[k] + (gp[k] * std::max(0.0f, l) - rgb[k]) * inherit;
            }
            for (int k = 0; k < 3; ++k) gp[k] += rgb[k] * (opacity * matte);
        }
    }
}

} // namespace

RenderResult renderGlow(const ImageF& source, const Params& p, const ImageF* mask,
                        const ImageF* dirt, const std::function<bool()>& abortFn) {
    RenderResult result;
    if (source.empty()) return result;

    const int compW = source.w;
    const int compH = source.h;
    const double resolution = std::clamp(p.resolution, 0.05, 1.0);
    const int baseW = std::max(1, static_cast<int>(std::lround(compW * resolution)));
    const int baseH = std::max(1, static_cast<int>(std::lround(compH * resolution)));
    const double compAspect = static_cast<double>(compW) / static_cast<double>(compH);

    result.workW = baseW;
    result.workH = baseH;

    // --- 0. debug views of the lens dirt texture --------------------------
    // These bypass the whole glow chain in the original as well: the dirt
    // texture is sampled directly (see docs/04-pipeline.md).
    if (dirt && (p.view == kViewDirtInput || p.view == kViewDirtMatte || p.view == kViewDirtMatted)) {
        ImageF d = resample(*dirt, compW, compH);
        const float exposure = std::pow(2.0f, static_cast<float>(p.dirtMatteExposure));
        const float mgamma = static_cast<float>(std::max(0.01, p.dirtMatteGamma));
        ImageF out(compW, compH);
        for (int y = 0; y < compH; ++y) {
            for (int x = 0; x < compW; ++x) {
                const float* dp = d.at(x, y);
                float* o = out.at(x, y);
                float rgb[3];
                for (int k = 0; k < 3; ++k)
                    rgb[k] = std::pow(std::max(0.0f, dp[k] * exposure), mgamma);
                float matte = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
                if (p.view == kViewDirtInput) {
                    o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2];
                } else if (p.view == kViewDirtMatte) {
                    o[0] = o[1] = o[2] = matte;
                } else {
                    o[0] = rgb[0] * matte; o[1] = rgb[1] * matte; o[2] = rgb[2] * matte;
                }
                o[3] = 1.0f;
            }
        }
        result.image = std::move(out);
        return result;
    }

    // --- 1. upload + preProcess -------------------------------------------
    ImageF working = resample(source, baseW, baseH);
    ImageF pre;
    preProcessImage(working, pre, p, p.view == kViewInput);

    if (p.preProcSolidFill) {
        for (size_t i = 0; i < pre.px.size(); i += 4) {
            pre.px[i] = pre.px[i + 1] = pre.px[i + 2] = 1.0f;
        }
    }

    if (p.view == kViewInput) {
        result.image = resample(pre, compW, compH);
        return result;
    }

    if (mask && p.useMask) applyMask(*mask, pre, p);

    // --- 2. pass list ------------------------------------------------------
    // Virtual octave dimensions are derived from the *full* comp size: the
    // original never applies "Resolution" to the stride math.
    std::vector<PassInfo> passes = buildPasses(p, compAspect, compW, compH);
    result.passes = static_cast<int>(passes.size());
    if (passes.empty()) {
        result.image = source;
        return result;
    }

    // Wrap mode is a single GL state in the original (ctx+0x7008) and is set to
    // CLAMP_TO_BORDER as soon as "Enable Angle" is on.
    const Wrap wrap = p.enableAngle ? Wrap::ClampToBorder : Wrap::MirrorRepeat;

    const double kPi = 3.14159265358979323846;
    const double angleRad = p.angle * kPi / 180.0;

    // Per-channel aberration multipliers.  The shader uses
    //     nf = max(STEPS * multiplier, 1.0)
    // as a *float* loop bound, so the multiplier is never rounded here; the
    // fractional tap grid is reproduced inside blurPass().
    const float mult[3] = {static_cast<float>(p.multR), static_cast<float>(p.multG),
                           static_cast<float>(p.multB)};

    std::vector<ImageF> levels;
    levels.reserve(passes.size());

    for (size_t i = 0; i < passes.size(); ++i) {
        if (abortFn && abortFn()) return result;
        const PassInfo& pi = passes[i];

        // Each octave is a *whole-frame* resample of the pre-processed image at
        // this octave's working resolution (comp * resolution * weight).
        // blitCenter() would crop instead of scale here, which would make the
        // deep octaves see only the centre of the frame.
        ImageF buf = resample(pre, pi.workW, pi.workH);

        const int stepsX[3] = {pi.stepsX, pi.stepsX, pi.stepsX};
        const int stepsY[3] = {pi.stepsY, pi.stepsY, pi.stepsY};

        const double vw = static_cast<double>(std::max(1, pi.w));
        const double vh = static_cast<double>(std::max(1, pi.h));
        const double density = std::max(1e-6, static_cast<double>(pi.density));

        ImageF tmp;
        if (p.enableAngle) {
            // Angle mode: two *full 2D* directional passes.  Pass 1 runs along
            // theta with stepsX and carries the pass fade; pass 2 runs along
            // theta + 90 deg with stepsY and a fade of exactly 1.0.  The stride
            // is cos/(w*density) , sin/(h*density) in *virtual* comp pixels, so
            // it is unaffected by the Resolution slider.
            if (!p.debugH) {
                const float du = static_cast<float>(std::cos(angleRad) / (vw * density));
                const float dv = static_cast<float>(std::sin(angleRad) / (vh * density));
                blurAccel(buf, tmp, stepsX, mult, p.aberrationGlow, du, dv, pi.fade, wrap);
            } else {
                tmp = buf;
            }
            if (!p.debugW) {
                const double a2 = angleRad + kPi * 0.5;
                const float du = static_cast<float>(std::cos(a2) / (vw * density));
                const float dv = static_cast<float>(std::sin(a2) / (vh * density));
                ImageF tmp2;
                blurAccel(tmp, tmp2, stepsY, mult, p.aberrationGlow, du, dv, 1.0f, wrap);
                tmp.swap(tmp2);
            }
        } else {
            // Separable mode: width pass then height pass.  Only the width pass
            // is faded (shader 02 multiplies by fadeMultiplier when
            // isBlurWidthB is set).
            if (!p.debugH) {
                blurAccel(buf, tmp, stepsX, mult, p.aberrationGlow, pi.strideX, 0.0f, pi.fade,
                          wrap);
            } else {
                tmp = buf;
            }
            if (!p.debugW) {
                ImageF tmp2;
                blurAccel(tmp, tmp2, stepsY, mult, p.aberrationGlow, 0.0f, pi.strideY, 1.0f,
                          wrap);
                tmp.swap(tmp2);
            }
        }
        buf.swap(tmp);

        levels.push_back(std::move(buf));
    }

    // --- 3. additive upsample chain ---------------------------------------
    for (int i = static_cast<int>(levels.size()) - 1; i >= 1; --i) {
        if (abortFn && abortFn()) return result;
        ImageF up = upsampleTent(levels[i], levels[i - 1].w, levels[i - 1].h);
        addInto(levels[i - 1], up);
    }

    ImageF glow = std::move(levels[0]);

    // debug views show the raw blur result
    if (p.debugW || p.debugH) {
        result.image = resample(glow, compW, compH);
        return result;
    }

    // --- 4. postProcess ----------------------------------------------------
    postProcessImage(glow, p);

    // --- 5. lens dirt ------------------------------------------------------
    if (dirt && p.dirtOpacity > 0.0) applyDirt(*dirt, glow, p);

    // --- 6. composite with the source --------------------------------------
    if (glow.w != compW || glow.h != compH) glow = resample(glow, compW, compH);

    ImageF out(compW, compH);
    const float srcOpacity = static_cast<float>(p.sourceOpacity);
    const bool screen = (p.blendMode == kBlendScreen);
    const bool unmult = p.unmult;

    for (int y = 0; y < compH; ++y) {
        for (int x = 0; x < compW; ++x) {
            const float* g = glow.at(x, y);
            const float* s = source.at(x, y);
            float* o = out.at(x, y);
            const float a = s[3];
            // Only the *source* is premultiplied.  The glow is emitted light:
            // it must survive where the source has no coverage, otherwise a
            // title on a transparent comp renders with no glow at all.  A
            // premultiplied image is allowed to exceed its alpha, and every
            // compositor (AE and Resolve alike) then draws it as
            // rgb + (1 - a) * background, i.e. the glow adds over the backdrop
            // exactly like the original does.
            const float m = unmult ? 1.0f : a;
            for (int k = 0; k < 3; ++k) {
                const float sv = s[k] * srcOpacity * m;
                o[k] = screen ? (1.0f - (1.0f - sv) * (1.0f - g[k])) : (sv + g[k]);
            }
            o[3] = unmult ? 1.0f : a;
        }
    }

    result.image = std::move(out);
    return result;
}

} // namespace dg
