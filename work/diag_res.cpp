// Render our own pipeline at the user's comp resolution and dump it, so the
// screenshot can be compared against what the code is *supposed* to produce at
// radius 1000 / standard / resolution 75.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "DGPipeline.h"

struct Glyph { char c; const char* rows[7]; };

static const Glyph kGlyphs[] = {
    {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
    {'u', {"     ", "     ", "#   #", "#   #", "#   #", "#   #", " ####"}},
    {'s', {"     ", "     ", " ####", "#    ", " ### ", "    #", "#### "}},
    {'t', {"  #  ", "  #  ", "#####", "  #  ", "  #  ", "  #  ", "   ##"}},
    {'o', {"     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "}},
    {'m', {"     ", "     ", "## # ", "# # #", "# # #", "# # #", "# # #"}},
    {' ', {"     ", "     ", "     ", "     ", "     ", "     ", "     "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'i', {" # ", "   ", " # ", " # ", " # ", " # ", " # "}},
    {'l', {"#  ", "#  ", "#  ", "#  ", "#  ", "#  ", " ##"}},
    {'e', {"     ", "     ", " ### ", "#   #", "#####", "#    ", " ### "}},
};

static void drawText(dg::ImageF& img, const char* text, int x0, int y0, int scale) {
    int x = x0;
    for (const char* p = text; *p; ++p) {
        const Glyph* g = nullptr;
        for (const Glyph& q : kGlyphs)
            if (q.c == *p) g = &q;
        if (g)
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (g->rows[r][c] == '#')
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx) {
                                float* o = img.at(x + c * scale + sx, y0 + r * scale + sy);
                                o[0] = o[1] = o[2] = o[3] = 1.0f;
                            }
        x += 6 * scale;
    }
}

static void writePPM(const char* path, const dg::ImageF& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const float* p = img.at(x, y);
            unsigned char c[3];
            for (int k = 0; k < 3; ++k)
                c[k] = static_cast<unsigned char>(
                    std::clamp(std::pow(std::max(0.0f, p[k]), 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f +
                    0.5f);
            std::fwrite(c, 1, 3, f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    // Match the screenshot: 1920x1080 comp, cap height ~93 px (scale 13).
    const int W = argc > 1 ? std::atoi(argv[1]) : 1920;
    const int H = argc > 2 ? std::atoi(argv[2]) : 1080;
    const int scale = argc > 3 ? std::atoi(argv[3]) : 13;

    dg::ImageF img(W, H);
    const char* text = "Custom Title";
    const int textW = static_cast<int>(std::strlen(text)) * 6 * scale;
    drawText(img, text, (W - textW) / 2, H / 2 - 3 * scale, scale);

    dg::Params p;
    p.radius = 1000.0;
    p.resolution = 0.75;
    p.qualityPreset = dg::kQualityStandard;

    const dg::RenderResult r = dg::renderGlow(img, p);
    std::printf("%dx%d cap=%d  passes=%d\n", W, H, 7 * scale, r.passes);

    char path[128];
    std::snprintf(path, sizeof(path), "smoke_out/our_%dx%d.ppm", W, H);
    writePPM(path, r.image);
    std::printf("wrote %s\n", path);
    return 0;
}
