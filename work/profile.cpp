// Vertical/horizontal falloff of the recovered glow, printed in linear values.
#include <cmath>
#include <cstdio>

#include "DGPipeline.h"

int main() {
    const int W = 1920, H = 1080;
    dg::ImageF img(W, H);
    // a solid 900x90 white bar: stands in for a line of large type
    for (int y = H / 2 - 45; y < H / 2 + 45; ++y)
        for (int x = W / 2 - 450; x < W / 2 + 450; ++x) {
            float* o = img.at(x, y);
            o[0] = o[1] = o[2] = o[3] = 1.0f;
        }

    dg::Params p;
    p.radius = 1000.0;
    p.resolution = 0.75;
    p.qualityPreset = dg::kQualityStandard;
    p.sourceOpacity = 0.0;   // glow only, so the profile is not hidden by the bar
    // The bar has alpha 1, everything around it is alpha 0 - exactly like a
    // title on a transparent comp.  Print both alpha modes.
    for (int mode = 0; mode < 2; ++mode) {
    p.unmult = (mode == 1);
    std::printf("\n================ unmult=%s ================\n", p.unmult ? "ON" : "OFF");

    const dg::RenderResult r = dg::renderGlow(img, p);
    std::printf("passes=%d work=%dx%d\n", r.passes, r.workW, r.workH);

    std::printf("\nvertical profile at x=centre (distance from bar edge, linear luma)\n");
    for (int d = 0; d <= 560; d += 20) {
        const int y = H / 2 + 45 + d;
        if (y >= H) break;
        const float* v = r.image.at(W / 2, y);
        std::printf("  +%4d px : %.5f\n", d, 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]);
    }
    std::printf("\nhorizontal profile at y=centre (distance from bar edge)\n");
    for (int d = 0; d <= 560; d += 20) {
        const int x = W / 2 + 450 + d;
        if (x >= W) break;
        const float* v = r.image.at(x, H / 2);
        std::printf("  +%4d px : %.5f\n", d, 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]);
    }

    // what a compositor *displays* over a transparent background, i.e.
    // premultiplied RGB + (1 - a) * background, is just the RGB here.
    std::printf("\nalpha at 0 / 100 / 300 px above the bar edge: %.3f %.3f %.3f\n",
                r.image.at(W / 2, H / 2 + 45 - 1)[3], r.image.at(W / 2, H / 2 + 45 + 100)[3],
                r.image.at(W / 2, H / 2 + 45 + 300)[3]);
    }
    return 0;
}
