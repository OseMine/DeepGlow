// scratch: emit the pass table for the docs
#include <cmath>
#include <cstdio>

#include "DGParams.h"

static void dump(const char* title, dg::Params p, double aspect, int cw, int ch) {
    const std::vector<dg::PassInfo> passes = dg::buildPasses(p, aspect, cw, ch);
    std::printf("\n### %s  (%dx%d, compAspect %.4f, radius %.0f)\n\n", title, cw, ch, aspect,
                p.radius);
    std::printf("| i | preSize | weight | size | fade | w | h | stepsX | stepsY | strideX | workW | workH |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (size_t i = 0; i < passes.size(); ++i) {
        const dg::PassInfo& q = passes[i];
        std::printf("| %zu | %.2f | %.4f | %.3f | %.3f | %d | %d | %d | %d | %.6f | %d | %d |\n", i,
                    double(q.size) / q.weight, q.weight, q.size, q.fade, q.w, q.h, q.stepsX,
                    q.stepsY, q.strideX, q.workW, q.workH);
    }
    std::printf("\nextent in comp px (stepsX*strideX*compW):");
    for (size_t i = 0; i < passes.size(); ++i)
        std::printf(" %.1f", passes[i].stepsX * passes[i].strideX * cw);
    std::printf("\ndensity: %.6f\n", passes.empty() ? 0.0 : passes[0].density);
}

int main() {
    dg::Params std16;
    std16.radius = 1000.0;
    std16.resolution = 0.75;
    dump("Standard, 16:9, res 75%", std16, 1920.0 / 1080.0, 1920, 1080);

    dg::Params extreme = std16;
    extreme.qualityPreset = dg::kQualityExtreme;
    dump("Extreme, 16:9, res 75%", extreme, 1920.0 / 1080.0, 1920, 1080);

    dg::Params custom = std16;
    custom.qualityPreset = dg::kQualityCustom;
    custom.stepsMultiplier = 0.8;
    custom.customWeight = 0.75;
    dump("Custom (steps 0.8, weight 0.75)", custom, 1920.0 / 1080.0, 1920, 1080);

    dg::Params small = std16;
    small.radius = 30.0;
    dump("Standard, radius 30 (fade truncation)", small, 1920.0 / 1080.0, 1920, 1080);

    dg::Params aspect = std16;
    aspect.aspectRatio = 1.5;
    dump("Standard, aspect 1.5", aspect, 1920.0 / 1080.0, 1920, 1080);
    return 0;
}
