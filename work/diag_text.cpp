// scratch: reproduce the "Custom Title" look and check blur isotropy
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "DGPipeline.h"

// ---------------------------------------------------------------- tiny 5x7 font
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
        if (g) {
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (g->rows[r][c] == '#')
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx) {
                                float* o = img.at(x + c * scale + sx, y0 + r * scale + sy);
                                o[0] = o[1] = o[2] = o[3] = 1.0f;
                            }
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
            for (int k = 0; k < 3; ++k) {
                float v = std::pow(std::max(0.0f, p[k]), 1.0f / 2.2f);
                c[k] = static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            std::fwrite(c, 1, 3, f);
        }
    std::fclose(f);
}

static void moments(const dg::ImageF& img, double& rmsX, double& rmsY, double& mean) {
    double s = 0, sx = 0, sy = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const double v = img.at(x, y)[1];
            s += v; sx += v * x; sy += v * y;
        }
    const double cx = sx / s, cy = sy / s;
    double vx = 0, vy = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const double v = img.at(x, y)[1];
            vx += v * (x - cx) * (x - cx);
            vy += v * (y - cy) * (y - cy);
        }
    rmsX = std::sqrt(vx / s);
    rmsY = std::sqrt(vy / s);
    mean = s / (img.w * img.h);
}

int main() {
    for (int square = 0; square <= 1; ++square) {
        const int W = 512;
        const int H = square ? 512 : 288;
        dg::ImageF img(W, H);
        drawText(img, "Custom Title", 64, H / 2 - 14, 4);
        // opaque black background: what an AE "solid + text" comp looks like
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) img.at(x, y)[3] = 1.0f;

        dg::Params p;                 // full default render (Debug View = None)
        p.radius = 1000.0;
        p.resolution = 0.75;
        p.qualityPreset = dg::kQualityStandard;

        const dg::RenderResult r = dg::renderGlow(img, p);
        double rx, ry, mean;
        moments(r.image, rx, ry, mean);
        std::printf("%dx%d  full render  rmsX=%.2f  rmsY=%.2f  ratio=%.2f  mean=%.4f  passes=%d\n",
                    W, H, rx, ry, rx / ry, mean, r.passes);

        char path[64];
        std::snprintf(path, sizeof(path), "smoke_out/diag_%dx%d.ppm", W, H);
        writePPM(path, r.image);

        // same frame, but with the width pass skipped (the old broken default,
        // identical to Debug View = Blur Height)
        dg::Params bad = p;
        bad.debugH = true;
        const dg::RenderResult rb = dg::renderGlow(img, bad);
        moments(rb.image, rx, ry, mean);
        std::printf("              height-only  rmsX=%.2f  rmsY=%.2f  ratio=%.2f  mean=%.4f\n", rx,
                    ry, rx / ry, mean);
        std::snprintf(path, sizeof(path), "smoke_out/diag_%dx%d_heightonly.ppm", W, H);
        writePPM(path, rb.image);
    }
    return 0;
}
