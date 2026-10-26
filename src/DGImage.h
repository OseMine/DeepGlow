// Deep Glow 2 — OFX port
// DGImage.h : minimal float RGBA image container + resampling primitives.
//
// The original plugin renders through OpenGL 3.3 with RGB16F/RGB32F FBOs and
// three fragment shaders (see docs/03-shaders.md).  This header provides the
// CPU equivalents of the operations those shaders perform:
//
//   * bilinear texture fetch with mirror-repeat / clamp wrap modes
//   * 13-tap energy preserving downsample      (shader 03)
//   * 3x3 tent upsample                        (shader 07)
//   * integer-offset blit                      (shader 01 / 00)
//
// The separable Gaussian of shader 02 lives in DGPipeline.cpp (blurPass), where
// it is driven by the recovered pass list.
//
// All colour data is stored as straight (non-premultiplied) linear RGB plus an
// alpha channel, matching the shaders which only ever sample .rgb.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dg {

// ---------------------------------------------------------------------------
// wrap modes (mirrors the GL constants recovered from ctx+0x7008)
// ---------------------------------------------------------------------------
enum class Wrap {
    MirrorRepeat = 0,   // GL_MIRRORED_REPEAT (0x8370) — plugin default
    ClampToEdge = 1,    // GL_CLAMP_TO_EDGE
    ClampToBorder = 2   // GL_CLAMP_TO_BORDER (0x812D) — used when angle is enabled
};

inline int reflectIndex(int i, int n) {
    if (n <= 1) return 0;
    const int period = 2 * n;
    int m = i % period;
    if (m < 0) m += period;
    if (m >= n) m = period - 1 - m;
    return m;
}

struct ImageF {
    int w = 0;
    int h = 0;
    std::vector<float> px;   // RGBA, row major, w*h*4

    ImageF() = default;
    ImageF(int width, int height) { resize(width, height); }

    void resize(int width, int height) {
        w = std::max(1, width);
        h = std::max(1, height);
        px.assign(static_cast<size_t>(w) * h * 4, 0.0f);
    }

    bool empty() const { return px.empty(); }

    void swap(ImageF& other) noexcept {
        std::swap(w, other.w);
        std::swap(h, other.h);
        px.swap(other.px);
    }

    inline float* at(int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
    inline const float* at(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }

    // Bilinear fetch, uv in [0,1] (GL texture coordinate convention).
    inline void fetch(float u, float v, Wrap wrap, float* out) const {
        const float fx = u * static_cast<float>(w) - 0.5f;
        const float fy = v * static_cast<float>(h) - 0.5f;
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);

        auto wrapCoord = [&](int i, int n) -> int {
            if (i >= 0 && i < n) return i;
            switch (wrap) {
                case Wrap::ClampToEdge: return std::clamp(i, 0, n - 1);
                case Wrap::ClampToBorder: return -1;
                case Wrap::MirrorRepeat:
                default: return reflectIndex(i, n);
            }
        };

        const int ix0 = wrapCoord(x0, w), ix1 = wrapCoord(x0 + 1, w);
        const int iy0 = wrapCoord(y0, h), iy1 = wrapCoord(y0 + 1, h);

        // GL_CLAMP_TO_BORDER with the default border colour: black.
        if (ix0 < 0 || ix1 < 0 || iy0 < 0 || iy1 < 0) {
            out[0] = out[1] = out[2] = out[3] = 0.0f;
            return;
        }

        const float* p00 = at(ix0, iy0);
        const float* p10 = at(ix1, iy0);
        const float* p01 = at(ix0, iy1);
        const float* p11 = at(ix1, iy1);

        for (int c = 0; c < 4; ++c) {
            const float a = p00[c] + (p10[c] - p00[c]) * tx;
            const float b = p01[c] + (p11[c] - p01[c]) * tx;
            out[c] = a + (b - a) * ty;
        }
    }

    inline void fetchPixel(int x, int y, float* out) const {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            out[0] = out[1] = out[2] = out[3] = 0.0f;
            return;
        }
        const float* s = at(x, y);
        out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = s[3];
    }
};

// ---------------------------------------------------------------------------
// shader 03 : 13-tap downsample (Call of Duty style, energy preserving)
// ---------------------------------------------------------------------------
inline ImageF downsample13(const ImageF& src) {
    ImageF dst(std::max(1, src.w / 2), std::max(1, src.h / 2));
    const float x = 1.0f / static_cast<float>(src.w);
    const float y = 1.0f / static_cast<float>(src.h);

    for (int j = 0; j < dst.h; ++j) {
        for (int i = 0; i < dst.w; ++i) {
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(dst.w);
            const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(dst.h);

            float a[4], b[4], c[4], d[4], e[4], f[4], g[4], h[4], i2[4], j2[4], k[4], l[4], m[4];
            src.fetch(u - 2 * x, v + 2 * y, Wrap::MirrorRepeat, a);
            src.fetch(u,         v + 2 * y, Wrap::MirrorRepeat, b);
            src.fetch(u + 2 * x, v + 2 * y, Wrap::MirrorRepeat, c);
            src.fetch(u - 2 * x, v,         Wrap::MirrorRepeat, d);
            src.fetch(u,         v,         Wrap::MirrorRepeat, e);
            src.fetch(u + 2 * x, v,         Wrap::MirrorRepeat, f);
            src.fetch(u - 2 * x, v - 2 * y, Wrap::MirrorRepeat, g);
            src.fetch(u,         v - 2 * y, Wrap::MirrorRepeat, h);
            src.fetch(u + 2 * x, v - 2 * y, Wrap::MirrorRepeat, i2);
            src.fetch(u - x,     v + y,     Wrap::MirrorRepeat, j2);
            src.fetch(u + x,     v + y,     Wrap::MirrorRepeat, k);
            src.fetch(u - x,     v - y,     Wrap::MirrorRepeat, l);
            src.fetch(u + x,     v - y,     Wrap::MirrorRepeat, m);

            float* o = dst.at(i, j);
            for (int ch = 0; ch < 4; ++ch) {
                float sum = e[ch] * 0.125f;
                sum += (a[ch] + c[ch] + g[ch] + i2[ch]) * 0.03125f;
                sum += (b[ch] + d[ch] + f[ch] + h[ch]) * 0.0625f;
                sum += (j2[ch] + k[ch] + l[ch] + m[ch]) * 0.125f;
                o[ch] = sum;
            }
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// shader 07 : 3x3 tent upsample with in_res/out_res aspect correction
// ---------------------------------------------------------------------------
inline ImageF upsampleTent(const ImageF& src, int outW, int outH) {
    ImageF dst(std::max(1, outW), std::max(1, outH));
    const float inW = static_cast<float>(src.w);
    const float inH = static_cast<float>(src.h);
    const float aspectX = inW / static_cast<float>(dst.w);
    const float aspectY = inH / static_cast<float>(dst.h);
    const float ox = aspectX / inW;
    const float oy = aspectY / inH;

    for (int j = 0; j < dst.h; ++j) {
        for (int i = 0; i < dst.w; ++i) {
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(dst.w);
            const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(dst.h);

            float a[4], b[4], c[4], d[4], e[4], f[4], g[4], h[4], i2[4];
            src.fetch(u - ox, v + oy, Wrap::MirrorRepeat, a);
            src.fetch(u,      v + oy, Wrap::MirrorRepeat, b);
            src.fetch(u + ox, v + oy, Wrap::MirrorRepeat, c);
            src.fetch(u - ox, v,      Wrap::MirrorRepeat, d);
            src.fetch(u,      v,      Wrap::MirrorRepeat, e);
            src.fetch(u + ox, v,      Wrap::MirrorRepeat, f);
            src.fetch(u - ox, v - oy, Wrap::MirrorRepeat, g);
            src.fetch(u,      v - oy, Wrap::MirrorRepeat, h);
            src.fetch(u + ox, v - oy, Wrap::MirrorRepeat, i2);

            float* o = dst.at(i, j);
            for (int ch = 0; ch < 4; ++ch) {
                float sum = e[ch] * 4.0f;
                sum += (b[ch] + d[ch] + f[ch] + h[ch]) * 2.0f;
                sum += (a[ch] + c[ch] + g[ch] + i2[ch]);
                o[ch] = sum * (1.0f / 16.0f);
            }
        }
    }
    return dst;
}

// ---------------------------------------------------------------------------
// shader 00 : blit into a differently sized buffer (centre crop / pad)
// ---------------------------------------------------------------------------
inline ImageF blitCenter(const ImageF& src, int outW, int outH, bool cullSmallValues) {
    ImageF dst(std::max(1, outW), std::max(1, outH));
    const float prevW = static_cast<float>(src.w);
    const float prevH = static_cast<float>(src.h);
    const float curW = static_cast<float>(dst.w);
    const float curH = static_cast<float>(dst.h);

    for (int y = 0; y < dst.h; ++y) {
        for (int x = 0; x < dst.w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / curW;
            const float v = (static_cast<float>(y) + 0.5f) / curH;
            const float pu = (u * curW - (curW - prevW) * 0.5f) / prevW;
            const float pv = (v * curH - (curH - prevH) * 0.5f) / prevH;
            float c[4];
            if (pu < 0.0f || pu > 1.0f || pv < 0.0f || pv > 1.0f) {
                c[0] = c[1] = c[2] = c[3] = 0.0f;
            } else {
                src.fetch(pu, pv, Wrap::ClampToEdge, c);
                if (cullSmallValues) {
                    const float thr = 0.00001f;
                    for (int k = 0; k < 3; ++k)
                        if (std::fabs(c[k]) < thr) c[k] = 0.0f;
                }
            }
            float* o = dst.at(x, y);
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
        }
    }
    return dst;
}

// Simple high quality resample used to bring the source to the working
// resolution (the plugin uploads to a smaller buffer when Resolution < 100%).
inline ImageF resample(const ImageF& src, int outW, int outH) {
    if (src.w == outW && src.h == outH) return src;
    ImageF dst(std::max(1, outW), std::max(1, outH));
    for (int y = 0; y < dst.h; ++y) {
        for (int x = 0; x < dst.w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dst.w);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dst.h);
            float c[4];
            src.fetch(u, v, Wrap::ClampToEdge, c);
            float* o = dst.at(x, y);
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
        }
    }
    return dst;
}

} // namespace dg
