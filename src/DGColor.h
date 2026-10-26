// Deep Glow 2 — OFX port
// DGColor.h : CPU equivalents of shader 06 (preProcess) and shader 05
// (postProcess).
//
// The original shaders operate on .gbr / .brg swizzled textures because the
// AE image is uploaded without a channel swap.  On the CPU we work in plain
// RGB order, so all swizzles disappear — but the *luma weights* are kept in
// their recovered (already un-swizzled) form:
//     dot(rgb, vec3(0.7152, 0.0722, 0.2126))  ==  0.2126 R + 0.7152 G + 0.0722 B
#pragma once

#include <algorithm>
#include <cmath>

#include "DGImage.h"
#include "DGParams.h"

namespace dg {

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float smoothstepf(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float safePow(float v, float e) {
    return std::pow(std::max(0.0f, v), e);
}

// shader 06 helper — saturation of an HDR colour
inline float rgb2s(const float* c) {
    float maxVal = std::max(std::max(c[0], c[1]), c[2]);
    float r = c[0], g = c[1], b = c[2];
    if (maxVal > 1.0f) {
        const float inv = 1.0f / maxVal;
        r *= inv; g *= inv; b *= inv;
    }
    const float minVal = std::min(std::min(r, g), b);
    const float delta = maxVal - minVal;
    const float s = (maxVal != 0.0f) ? delta / maxVal : 0.0f;
    return s;
}

// ---------------------------------------------------------------------------
// shader 06 : preProcess (threshold, aberration, exposure, gamma)
//
//  * `viewInput` reproduces the VIEW_INPUT early-out: the exposure / gamma
//    tail is skipped (the original applies pow(1/g) followed by pow(g), which
//    cancels out).
// ---------------------------------------------------------------------------
inline void preProcessImage(const ImageF& src, ImageF& dst, const Params& p, bool viewInput) {
    dst.resize(src.w, src.h);

    const float offU = static_cast<float>(p.pixelOffset / std::max(1, src.h));
    const float offV = offU;
    const int ab = p.aberrationPixel ? p.aberrationChannels : kAberrationNone;

    const float thr = static_cast<float>(p.threshold);
    const float sm = static_cast<float>(p.thresholdSmooth);
    const float lo = thr - sm;
    const float hi = thr + sm;
    const bool doThreshold = thr > 0.0f;
    const bool luma = (p.thresholdMode == kThresholdLuma);
    const float satBias = static_cast<float>(p.satBias);

    const float exposure = static_cast<float>(p.exposure);
    const bool gammaOn = p.gammaCorrect && std::fabs(p.gamma - 1.0) > 1e-6;
    const float gamma = static_cast<float>(p.gamma);
    const bool screen = (p.blendMode == kBlendScreen);

    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.w);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.h);
            float c[4], a[4], b[4];

            const float* s = src.at(x, y);
            c[0] = s[0]; c[1] = s[1]; c[2] = s[2]; c[3] = s[3];

            if (ab != kAberrationNone) {
                src.fetch(u + offU, v + offV, Wrap::MirrorRepeat, a);
                src.fetch(u - offU, v - offV, Wrap::MirrorRepeat, b);
                switch (ab) {
                    case kAberrationRG:
                        c[0] = a[0];   // R from +offset
                        c[1] = b[1];   // G from -offset
                        break;
                    case kAberrationRB:
                        c[0] = a[0];   // R from +offset
                        c[2] = b[2];   // B from -offset
                        break;
                    default:           // kAberrationGB
                        c[1] = a[1];   // G from +offset
                        c[2] = b[2];   // B from -offset
                        break;
                }
            }

            c[0] = std::max(0.0f, c[0]);
            c[1] = std::max(0.0f, c[1]);
            c[2] = std::max(0.0f, c[2]);

            if (doThreshold) {
                if (luma) {
                    const float L = 0.7152f * c[1] + 0.0722f * c[2] + 0.2126f * c[0];
                    const float m = smoothstepf(lo, hi, L);
                    c[0] *= m; c[1] *= m; c[2] *= m;
                } else {
                    const float orig[3] = {c[0], c[1], c[2]};
                    c[0] *= smoothstepf(lo, hi, c[0]);
                    c[1] *= smoothstepf(lo, hi, c[1]);
                    c[2] *= smoothstepf(lo, hi, c[2]);
                    if (satBias != 0.0f) {
                        float sat = rgb2s(orig);
                        if (satBias < 0.0f) sat = 1.0f - sat;
                        const float m = smoothstepf(lo, hi, sat);
                        const float mixv = std::fabs(satBias);
                        for (int k = 0; k < 3; ++k)
                            c[k] = c[k] + (orig[k] * m - c[k]) * mixv;
                    }
                }
            }

            if (!viewInput) {
                if (screen) {
                    c[0] = std::min(c[0], 1.0f);
                    c[1] = std::min(c[1], 1.0f);
                    c[2] = std::min(c[2], 1.0f);
                } else {
                    c[0] *= exposure; c[1] *= exposure; c[2] *= exposure;
                }
                if (gammaOn) {
                    c[0] = safePow(c[0], gamma);
                    c[1] = safePow(c[1], gamma);
                    c[2] = safePow(c[2], gamma);
                }
            }

            float* o = dst.at(x, y);
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = s[3];
        }
    }
}

// ---------------------------------------------------------------------------
// shader 05 helpers
// ---------------------------------------------------------------------------
inline float overlayV(float bg, float fg) {
    return bg < 0.5f ? (2.0f * bg * fg) : (1.0f - 2.0f * (1.0f - bg) * (1.0f - fg));
}

inline float softLightV(float a, float b) {
    return ((1.0f - 2.0f * b) * a * a) + (2.0f * b * a);
}

inline void applyTint(float* c, const Params& p) {
    const float mixAmount = static_cast<float>(p.tintMode == kTintOff ? 0.0 : p.tintStrength);
    if (mixAmount <= 0.0f) return;

    const bool duo = (p.tintMode == kTintDuo);
    const float A[3] = {p.tintColor.r, p.tintColor.g, p.tintColor.b};
    const float B[3] = {p.tintColorOuter.r, p.tintColorOuter.g, p.tintColorOuter.b};

    float t1[3], t2[3];
    for (int k = 0; k < 3; ++k) {
        switch (p.tintBlend) {
            case kTintOverlay:
                t1[k] = safePow(overlayV(safePow(c[k], 0.45f), safePow(A[k], 0.45f)), 2.22f);
                break;
            case kTintSoftLight:
                t1[k] = softLightV(std::min(c[k], 1.0f), A[k]);
                break;
            case kTintMultiply:
            default:
                t1[k] = c[k] * A[k];
                break;
        }
    }

    if (duo) {
        for (int k = 0; k < 3; ++k) {
            switch (p.tintBlend) {
                case kTintOverlay:
                    t2[k] = safePow(overlayV(safePow(c[k], 0.45f), safePow(B[k], 0.45f)), 2.2f);
                    break;
                case kTintSoftLight:
                    t2[k] = softLightV(std::min(c[k], 1.0f), B[k]);
                    break;
                case kTintMultiply:
                default:
                    t2[k] = c[k] * B[k];
                    break;
            }
        }
        for (int k = 0; k < 3; ++k) {
            const float m = std::min(c[k], 1.0f);
            t1[k] = t1[k] + (t2[k] - t1[k]) * m;
        }
    }

    for (int k = 0; k < 3; ++k) {
        c[k] = c[k] + (t1[k] - c[k]) * mixAmount;
        c[k] = std::max(c[k], 0.0f);
    }
}

inline float acesFilmic(float x) {
    const float a = 2.51f, b = 0.03f, cc = 2.43f, d = 0.59f, e = 0.14f;
    return clampf((x * (a * x + b)) / (x * (cc * x + d) + e), 0.0f, 1.0f);
}

inline float filmicTonemap(float x) {
    const float X = std::max(0.0f, x - 0.004f);
    const float r = (X * (6.2f * X + 0.5f)) / (X * (6.2f * X + 1.7f) + 0.06f);
    return safePow(r, 2.2f);
}

inline float reinhard2(float x) {
    const float Lwhite = 4.0f;
    return (x * (1.0f + x / (Lwhite * Lwhite))) / (1.0f + x);
}

inline float uncharted2Partial(float x) {
    const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
    x = std::max(1e-5f, x);
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

inline float uncharted2Filmic(float v) {
    const float exposureBias = 2.0f;
    const float curr = uncharted2Partial(v * exposureBias);
    const float W = 11.2f;
    const float whiteScale = 1.0f / uncharted2Partial(W);
    return curr * whiteScale;
}

// ---------------------------------------------------------------------------
// shader 05 : postProcess (exposure, tint, tonemap, gamma)
// ---------------------------------------------------------------------------
inline void postProcessImage(ImageF& img, const Params& p) {
    const float exposure = static_cast<float>(p.exposure);
    const bool gammaOn = p.gammaCorrect && std::fabs(p.gamma - 1.0) > 1e-6;
    const float invGamma = static_cast<float>(1.0 / std::max(1e-6, p.gamma));
    const bool screen = (p.blendMode == kBlendScreen);
    const float tonemapMix = static_cast<float>(p.tonemapMix);
    const bool doTonemap = (p.tonemapMode >= kTonemapAces) && (tonemapMix > 0.01f);

    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            float c[4];
            const float* s = img.at(x, y);
            c[0] = s[0]; c[1] = s[1]; c[2] = s[2]; c[3] = s[3];

            if (screen) {
                c[0] *= exposure; c[1] *= exposure; c[2] *= exposure;
            }

            if (p.tintMode != kTintOff && p.tintStrength > 0.0) applyTint(c, p);

            if (doTonemap) {
                float t[3] = {c[0], c[1], c[2]};
                switch (p.tonemapMode) {
                    case kTonemapAces:
                        for (int k = 0; k < 3; ++k) t[k] = acesFilmic(t[k]);
                        break;
                    case kTonemapFilmic:
                        for (int k = 0; k < 3; ++k) t[k] = filmicTonemap(t[k]);
                        break;
                    case kTonemapHable:
                        for (int k = 0; k < 3; ++k) t[k] = uncharted2Filmic(t[k]);
                        break;
                    case kTonemapReinhard:
                        for (int k = 0; k < 3; ++k) t[k] = t[k] / (t[k] + 1.0f);
                        break;
                    case kTonemapReinhard2:
                        for (int k = 0; k < 3; ++k) t[k] = reinhard2(t[k]);
                        break;
                    case kTonemapClampChroma: {
                        const float l = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
                        const float d = clampf(l, 0.0f, 1.0f);
                        for (int k = 0; k < 3; ++k)
                            t[k] = std::max(0.0f, t[k] + (l - t[k]) * d);
                        break;
                    }
                    default:
                        break;
                }
                for (int k = 0; k < 3; ++k) c[k] = c[k] + (t[k] - c[k]) * tonemapMix;
            }

            if (gammaOn) {
                c[0] = safePow(c[0], invGamma);
                c[1] = safePow(c[1], invGamma);
                c[2] = safePow(c[2], invGamma);
            }

            if (screen) {
                c[0] = std::min(c[0], 1.0f);
                c[1] = std::min(c[1], 1.0f);
                c[2] = std::min(c[2], 1.0f);
            }

            float* o = img.at(x, y);
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
        }
    }
}

} // namespace dg
