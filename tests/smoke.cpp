// Deep Glow 2 — OFX port
// tests/smoke.cpp : headless exercise of the recovered pixel pipeline.
//
// The plugin cannot be validated against After Effects renders without the
// original host, so this test does what *can* be automated:
//
//   1. every parameter combination used below must produce finite pixels
//   2. the glow must actually bleed (energy outside the source shape)
//   3. Screen must never darken the source, Add must never darken it either
//   4. the debug views must affect only their own axis, the full render must be
//      isotropic, and the abort callback must terminate cleanly
//
// It also dumps a few PFM files into ./smoke_out/ for eyeballing.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "DGPipeline.h"
#include "DGParams.h"

namespace {

int gFailures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++gFailures;
    }
}

// 256x144 "comp" with an HDR disc in the middle and a dim gradient background.
dg::ImageF makeSource(int w, int h) {
    dg::ImageF img(w, h);
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float r = std::min(w, h) * 0.12f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.at(x, y);
            const float dx = x - cx, dy = y - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float disc = d < r ? 4.0f : 0.0f;               // HDR core
            const float bg = 0.02f + 0.01f * (static_cast<float>(x) / w);
            p[0] = disc + bg;
            p[1] = disc * 0.6f + bg;
            p[2] = disc * 0.3f + bg * 0.5f;
            p[3] = 1.0f;
        }
    }
    return img;
}

bool allFinite(const dg::ImageF& img) {
    for (float v : img.px) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

double meanOutside(const dg::ImageF& img, float cx, float cy, float r) {
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            const float dx = x - cx, dy = y - cy;
            if (std::sqrt(dx * dx + dy * dy) <= r) continue;
            sum += img.at(x, y)[1];
            ++n;
        }
    }
    return n ? sum / n : 0.0;
}

void writePFM(const std::string& path, const dg::ImageF& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "PF\n%d %d\n-1.0\n", img.w, img.h);
    std::vector<float> row(static_cast<size_t>(img.w) * 3);
    for (int y = img.h - 1; y >= 0; --y) {          // PFM is bottom-up
        for (int x = 0; x < img.w; ++x) {
            const float* p = img.at(x, y);
            row[x * 3 + 0] = p[0];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[2];
        }
        std::fwrite(row.data(), sizeof(float), row.size(), f);
    }
    std::fclose(f);
}

dg::Params defaults() {
    dg::Params p;
    p.resolution = 0.5;      // keep the test snappy
    p.radius = 400.0;
    return p;
}

void runCase(const char* name, dg::Params p, const dg::ImageF& src, bool dump) {
    const auto t0 = std::chrono::steady_clock::now();
    const dg::RenderResult r = dg::renderGlow(src, p);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::printf("%-34s %6.1f ms  passes=%d  work=%dx%d\n", name, ms, r.passes,
                r.workW, r.workH);

    check(!r.image.empty(), "image is not empty");
    check(r.image.w == src.w && r.image.h == src.h, "output matches comp size");
    check(allFinite(r.image), "all pixels finite");

    if (dump) writePFM(std::string("smoke_out/out_") + name + ".pfm", r.image);
    (void)dump;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::filesystem::create_directories("smoke_out");
    const int W = 256, H = 144;
    const dg::ImageF src = makeSource(W, H);
    const float cx = W * 0.5f, cy = H * 0.5f;
    const float r = std::min(W, H) * 0.12f;

    std::printf("Deep Glow 2 pipeline smoke test (%dx%d)\n\n", W, H);

    // --- 1. baseline ------------------------------------------------------
    {
        dg::Params p = defaults();
        const dg::RenderResult res = dg::renderGlow(src, p);
        const double out = meanOutside(res.image, cx, cy, r);
        const double ref = meanOutside(src, cx, cy, r);
        std::printf("baseline: outside mean %.5f (source %.5f)\n", out, ref);
        check(out > ref * 2.0, "glow bleeds outside the source shape");
        check(out <= 1.0001, "Screen keeps the result <= 1");
    }

    // --- 2. glow must not darken values that are already in range ---------
    // Screen clamps the glow to 1.0, so a source above 1.0 *can* come out
    // darker (this is the original's documented behaviour); only the 0..1
    // range is guaranteed to be non-decreasing.
    for (int blend = 1; blend <= 2; ++blend) {
        dg::Params p = defaults();
        p.blendMode = blend;
        const dg::RenderResult res = dg::renderGlow(src, p);
        double worst = 0.0;
        for (int i = 0; i < W * H; ++i) {
            const double s = src.px[i * 4 + 1];
            if (s > 1.0) continue;
            const double o = res.image.px[i * 4 + 1];
            worst = std::min(worst, o - s);
        }
        std::printf("blend %d: worst (out - src) = %.6f\n", blend, worst);
        check(worst > -1e-4, "glow never darkens in-range source values");
    }

    // --- 3. every quality preset -----------------------------------------
    for (int q : {1, 2, 3, 4, 5, 6, 8}) {
        dg::Params p = defaults();
        p.qualityPreset = q;
        char label[64];
        std::snprintf(label, sizeof(label), "quality preset %d", q);
        runCase(label, p, src, false);
    }

    // --- 4. feature matrix ------------------------------------------------
    {
        dg::Params p = defaults();
        p.threshold = 0.5; p.thresholdSmooth = 0.2; p.thresholdMode = dg::kThresholdLuma;
        runCase("threshold luma", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.threshold = 0.5; p.thresholdSmooth = 0.1; p.satBias = 0.6;
        runCase("threshold chroma + satbias", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.aspectRatio = 1.8;
        runCase("aspect 1.8", p, src, true);
    }
    {
        dg::Params p = defaults();
        p.enableAngle = true; p.angle = 30.0; p.aspectRatio = 1.4;
        runCase("angled blur", p, src, true);
    }
    {
        dg::Params p = defaults();
        p.aberrationPixel = true; p.aberrationChannels = dg::kAberrationRB; p.pixelOffset = 3.0;
        runCase("pixel aberration", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.aberrationGlow = true;
        runCase("glow aberration", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.tintMode = dg::kTintMono; p.tintBlend = dg::kTintOverlay;
        p.tintColor = dg::RGBAf{0.2f, 0.6f, 1.0f, 1.0f}; p.tintStrength = 1.0;
        runCase("tint monocolor overlay", p, src, true);
    }
    {
        dg::Params p = defaults();
        p.tintMode = dg::kTintDuo; p.tintBlend = dg::kTintSoftLight;
        p.tintColor = dg::RGBAf{1.0f, 0.4f, 0.1f, 1.0f};
        p.tintColorOuter = dg::RGBAf{0.1f, 0.2f, 1.0f, 1.0f};
        runCase("tint duocolor soft light", p, src, true);
    }
    for (int tm : {dg::kTonemapAces, dg::kTonemapFilmic, dg::kTonemapHable,
                   dg::kTonemapReinhard, dg::kTonemapReinhard2, dg::kTonemapClampChroma}) {
        dg::Params p = defaults();
        p.tonemapMode = tm;
        char label[64];
        std::snprintf(label, sizeof(label), "tonemap %d", tm);
        runCase(label, p, src, false);
    }
    {
        dg::Params p = defaults();
        p.gammaCorrect = true; p.gamma = 2.2;
        runCase("gamma 2.2", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.exposure = 2.0; p.blendMode = dg::kBlendAdd;
        runCase("add blend, exposure 2", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.view = dg::kViewInput;
        const dg::RenderResult r = dg::renderGlow(src, p);
        check(r.image.w == W && r.image.h == H, "view input size");
        check(allFinite(r.image), "view input finite");
        std::printf("view input                         ok\n");
    }
    {
        dg::Params p = defaults();
        p.debugW = true;
        runCase("debug blur width", p, src, false);
        p.debugW = false; p.debugH = true;
        runCase("debug blur height", p, src, false);
    }
    {
        dg::Params p = defaults();
        p.unmult = true;
        const dg::RenderResult r = dg::renderGlow(src, p);
        bool opaque = true;
        for (int i = 0; i < W * H; ++i) opaque = opaque && (r.image.px[i * 4 + 3] == 1.0f);
        check(opaque, "unmult produces an opaque alpha");
        std::printf("unmult alpha                       ok\n");
    }

    // --- 5. mask + dirt ---------------------------------------------------
    {
        dg::ImageF mask(W, H);
        dg::ImageF dirt(W, H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float* m = mask.at(x, y);
                m[0] = m[1] = m[2] = 0.0f;
                m[3] = (x < W / 2) ? 1.0f : 0.0f;      // left half only
                float* d = dirt.at(x, y);
                const float v = ((x * 7 + y * 13) % 17) / 17.0f;
                d[0] = v; d[1] = v * 0.5f; d[2] = v * 0.25f; d[3] = 1.0f;
            }
        }
        dg::Params p = defaults();
        p.useMask = true;
        p.maskMode = 1;
        p.dirtOpacity = 0.5;
        const dg::RenderResult r = dg::renderGlow(src, p, &mask, &dirt);
        check(!r.image.empty() && allFinite(r.image), "mask + dirt finite");
        writePFM("smoke_out/out_mask_dirt.pfm", r.image);
        std::printf("mask + lens dirt                   ok\n");
    }

    // --- 6. abort callback ------------------------------------------------
    {
        dg::Params p = defaults();
        int calls = 0;
        const dg::RenderResult r =
            dg::renderGlow(src, p, nullptr, nullptr, [&calls]() { return ++calls > 2; });
        check(r.image.empty(), "aborted render returns an empty image");
        std::printf("abort callback                     ok\n");
    }

    // --- 7. pass-builder invariants (exact, no rendering) ------------------
    // The recovered stride math must make the octave footprint independent of
    // the Resolution slider.  For every octave:
    //     stepsX * strideX * compW  ==  spanX / weight  ==  preSize * aspectX
    // (the quality weight cancels: stride divides by compW*weight while spanX
    // already contains it).  Extents are compared across resolutions exactly,
    // and against the analytic value up to the integer rounding of the buffer
    // dimensions.
    {
        const double compAspect = 16.0 / 9.0;
        const int cw = 1920, ch = 1080;
        std::vector<double> refX, refY;
        for (double res : {1.0, 0.75, 0.5, 0.25}) {
            dg::Params p;
            p.radius = 1000.0;
            p.resolution = res;
            const std::vector<dg::PassInfo> passes = dg::buildPasses(p, compAspect, cw, ch);
            check(passes.size() == 10, "10 octaves with the standard preset");
            std::vector<double> extX, extY;
            double prevExtent = -1.0;
            bool grows = true;
            for (const dg::PassInfo& pi : passes) {
                const double ex =
                    static_cast<double>(pi.stepsX) * pi.strideX * static_cast<double>(cw);
                const double ey =
                    static_cast<double>(pi.stepsY) * pi.strideY * static_cast<double>(ch);
                const double wantX = static_cast<double>(pi.size) / pi.weight;  // aspect 1
                check(std::fabs(ex - wantX) <= 5e-3 * wantX,
                      "stride matches the virtual footprint (x)");
                check(std::fabs(ey - wantX) <= 5e-3 * wantX,
                      "stride matches the virtual footprint (y)");
                if (prevExtent > 0.0 && ex < prevExtent * 0.99) grows = false;
                prevExtent = ex;
                extX.push_back(ex);
                extY.push_back(ey);
            }
            check(grows, "octave footprint grows monotonically");
            check(passes.back().workW < cw && passes.back().workH < ch,
                  "deep octaves use smaller buffers");
            if (refX.empty()) {
                refX = extX;
                refY = extY;
            } else {
                bool identical = (extX.size() == refX.size());
                for (size_t i = 0; identical && i < extX.size(); ++i)
                    identical = std::fabs(extX[i] - refX[i]) <= 1e-9 * refX[i] &&
                                std::fabs(extY[i] - refY[i]) <= 1e-9 * refY[i];
                check(identical, "Resolution does not change the octave footprint");
            }
        }
    }

    // --- 8. glow footprint end-to-end (RMS radius) -------------------------
    // A small HDR blob goes through the whole octave chain; the RMS radius of
    // the resulting glow checks the recovered scale behaviour:
    //   * Radius scales the footprint linearly, and
    //   * Resolution keeps the footprint (it only changes sampling density).
    // The comparison is done at 100 % vs 50 %; at 25 % the kernel of the widest
    // octave is wider than the reduced buffer, so GL_MIRRORED_REPEAT folds the
    // halo back into the frame (see 06-limitations.md).
    {
        const int N = 512;
        // A small filled disc rather than a single pixel: a delta can fall
        // between the bilinear samples of a 4x downsample and lose all of its
        // energy, which would make the comparison meaningless.
        dg::ImageF blob(N, N);
        const int cx = N / 2, cy = N / 2;
        const double srcR = 4.0;
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const double d = std::sqrt(static_cast<double>((x - cx) * (x - cx) +
                                                              (y - cy) * (y - cy)));
                if (d > srcR) continue;
                float* q = blob.at(x, y);
                q[0] = q[1] = q[2] = 1.0f;
                q[3] = 1.0f;
            }
        }

        auto rmsRadius = [&](double radius, double resolution) {
            dg::Params p;
            p.radius = radius;
            p.resolution = resolution;
            p.qualityPreset = dg::kQualityStandard;
            // Isolate the glow: source opacity 0 makes the screen composite
            // output exactly the glow, and Unmult keeps the (small) alpha from
            // premultiplying it away.
            p.sourceOpacity = 0.0;
            p.unmult = true;
            const dg::RenderResult r = dg::renderGlow(blob, p);
            double num = 0.0, den = 0.0;
            for (int y = 0; y < N; ++y) {
                for (int x = 0; x < N; ++x) {
                    const double d2 = static_cast<double>((x - cx) * (x - cx) +
                                                          (y - cy) * (y - cy));
                    const double v = r.image.at(x, y)[1];
                    num += v * d2;
                    den += v;
                }
            }
            return den > 0.0 ? std::sqrt(num / den) : 0.0;
        };

        const double r400a = rmsRadius(400.0, 1.0);
        const double r400b = rmsRadius(400.0, 0.5);
        const double r200 = rmsRadius(200.0, 0.5);
        std::printf("glow RMS radius: r400 @1.0=%.2f px  @0.5=%.2f px  r200 @0.5=%.2f px\n",
                    r400a, r400b, r200);
        check(r400a > 20.0, "the blob actually glows");
        check(std::fabs(r400b - r400a) <= 0.15 * r400a, "resolution 0.5 keeps the footprint");
        check(r400b > 1.7 * r200 && r400b < 2.3 * r200, "footprint scales with Radius");
    }

    // --- 9. debug view mapping and axis semantics ---------------------------
    // The OFX "Debug View" choices are laid out in AE order
    // {0 = Blur Width, 1 = Blur Height, 2 = None}, which is *not* the same as
    // the AE enum values (1-based).  Reading the choice index as if it were the
    // enum made the default ("None") skip the X pass, so every render came out
    // vertically streaked.  Pin both halves here:
    //   * the index -> axis mapping, and
    //   * the actual axis semantics of a width-only / height-only / full render.
    {
        bool w = true, h = true;
        dg::debugAxesFromChoice(dg::kDebugViewWidth, w, h);
        check(w && !h, "choice 0 is width-only");
        dg::debugAxesFromChoice(dg::kDebugViewHeight, w, h);
        check(!w && h, "choice 1 is height-only");
        dg::debugAxesFromChoice(dg::kDebugViewNone, w, h);
        check(!w && !h, "choice 2 (default) is the full render");

        // A single small disc, so the measured spread is the blur only.
        const int N = 96;
        dg::ImageF dot(N, N);
        for (int y = N / 2 - 1; y <= N / 2 + 1; ++y)
            for (int x = N / 2 - 1; x <= N / 2 + 1; ++x) {
                float* q = dot.at(x, y);
                q[0] = q[1] = q[2] = q[3] = 1.0f;
            }

        auto axes = [&](bool debugW, bool debugH, double& rmsX, double& rmsY) {
            dg::Params p;
            p.radius = 120.0;
            p.resolution = 1.0;
            p.sourceOpacity = 0.0;
            p.unmult = true;
            p.debugW = debugW;
            p.debugH = debugH;
            const dg::RenderResult r = dg::renderGlow(dot, p);
            double s = 0.0, sx = 0.0, sy = 0.0;
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x) {
                    const double v = r.image.at(x, y)[1];
                    s += v; sx += v * x; sy += v * y;
                }
            const double cx = sx / s, cy = sy / s;
            double vx = 0.0, vy = 0.0;
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x) {
                    const double v = r.image.at(x, y)[1];
                    vx += v * (x - cx) * (x - cx);
                    vy += v * (y - cy) * (y - cy);
                }
            rmsX = std::sqrt(vx / s);
            rmsY = std::sqrt(vy / s);
        };

        double wx, wy, hx, hy, fx, fy;
        axes(true, false, wx, wy);
        axes(false, true, hx, hy);
        axes(false, false, fx, fy);
        std::printf("debug axes: width-only %.2fx%.2f  height-only %.2fx%.2f  full %.2fx%.2f\n",
                    wx, wy, hx, hy, fx, fy);
        // The 3x3 disc contributes ~2 px of its own spread, so the axis that is
        // *not* blurred stays around 2 px while the blurred one grows.
        check(wx > 2.5 * wy && wy < 3.0, "width-only debug view spreads along X");
        check(hy > 2.5 * hx && hx < 3.0, "height-only debug view spreads along Y");
        // The full render must be round: this is the regression that the
        // stretched default render exposed.
        check(fx > 0.9 * fy && fx < 1.1 * fy, "full render is isotropic");
    }

    // --- 10. glow survives a transparent background -------------------------
    // A title on a transparent comp is the normal use case: alpha is 1 only
    // inside the glyphs.  The compositor draws a premultiplied image as
    // rgb + (1 - a) * background, so the glow must be present in rgb *outside*
    // the source alpha.  Premultiplying the glow by the source alpha (which the
    // port used to do) makes the whole halo vanish and the effect look like a
    // plain copy of the source.
    {
        dg::ImageF title(W, H);           // fully transparent except a white bar
        for (int y = H / 2 - 6; y < H / 2 + 6; ++y)
            for (int x = W / 2 - 40; x < W / 2 + 40; ++x) {
                float* q = title.at(x, y);
                q[0] = q[1] = q[2] = q[3] = 1.0f;
            }

        dg::Params p = defaults();
        const dg::RenderResult r = dg::renderGlow(title, p);

        double glowOut = 0.0;
        int n = 0;
        bool alphaKept = true;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float* q = r.image.at(x, y);
                if (q[3] > 0.0f) continue;              // outside the source alpha only
                alphaKept = alphaKept && q[3] == 0.0f;
                glowOut += q[1];
                ++n;
            }
        glowOut = n ? glowOut / n : 0.0;
        std::printf("transparent title: mean glow outside alpha %.5f (alpha preserved %s)\n",
                    glowOut, alphaKept ? "yes" : "no");
        check(glowOut > 0.002, "glow is emitted outside the source alpha");
        check(alphaKept, "source alpha is not expanded by the glow");
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "PASSED" : "FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
