// scratch: timing of renderGlow for realistic comp sizes / settings
#include <chrono>
#include <cmath>
#include <cstdio>

#include "DGPipeline.h"

static double bench(int w, int h, const dg::Params& p, int reps = 2) {
    dg::ImageF img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float* q = img.at(x, y);
            const double v = 0.5 + 0.5 * std::sin(x * 0.05) * std::cos(y * 0.03);
            q[0] = static_cast<float>(v);
            q[1] = static_cast<float>(v * 0.8);
            q[2] = static_cast<float>(v * 0.6);
            q[3] = 1.0f;
        }
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        dg::RenderResult res = dg::renderGlow(img, p);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < best) best = ms;
        if (r + 1 == reps) std::printf("%dx%d passes=%d work=%dx%d  %.0f ms\n", w, h, res.passes,
                                       res.workW, res.workH, best);
    }
    return best;
}

int main() {
    dg::Params p;   // defaults: radius 1000, Standard, resolution 0.75
    std::printf("defaults (radius 1000, Standard, res 75%%)\n");
    bench(1920, 1080, p);
    bench(3840, 2160, p);

    std::printf("\nresolution sweep, 1920x1080 radius 400\n");
    for (double res : {0.25, 0.5, 0.75, 1.0}) {
        dg::Params q;
        q.radius = 400;
        q.resolution = res;
        bench(1920, 1080, q, 1);
    }

    std::printf("\nquality sweep, 1920x1080 radius 1000 res 75%%\n");
    const char* names[] = {"", "Draft", "Low", "Medium", "Standard", "High", "Extreme"};
    for (int preset = 1; preset <= 6; ++preset) {
        dg::Params q;
        q.qualityPreset = preset;
        std::printf("%-9s ", names[preset]);
        bench(1920, 1080, q, 1);
    }

    std::printf("\nradius sweep, 1920x1080 Standard res 75%%\n");
    for (double rad : {0.0, 50.0, 200.0, 500.0, 1000.0, 2000.0}) {
        dg::Params q;
        q.radius = rad;
        std::printf("r%-7.0f ", rad);
        bench(1920, 1080, q, 1);
    }
    return 0;
}
