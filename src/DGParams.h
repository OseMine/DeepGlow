// Deep Glow 2 — OFX port
// DGParams.h : parameter model + the recovered glow "pass list" builder.
//
// Everything in here is a direct transcription of the parameter block and of
// the pass builder function at RVA 0x00e600 (see docs/04-pipeline.md).  The
// numeric tables (quality presets, fade ramps) are byte-exact copies of the
// constants found in the binary.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dg {

// ---------------------------------------------------------------------------
// enums — values match the fake enums declared inside the original shaders
// ---------------------------------------------------------------------------
enum BlendMode {
    kBlendScreen = 1,
    kBlendAdd = 2
};

enum ThresholdMode {
    kThresholdChroma = 1,
    kThresholdLuma = 2
};

enum AberrationMode {
    kAberrationNone = 0,
    kAberrationRG = 1,
    kAberrationRB = 2,
    kAberrationGB = 3
};

enum TintMode {
    kTintOff = 0,
    kTintMono = 1,
    kTintDuo = 2
};

enum TintBlend {
    kTintMultiply = 1,
    kTintOverlay = 2,
    kTintSoftLight = 3
};

// 1-based on purpose: 0 is "uninitialised" in the original code.
enum TonemapMode {
    kTonemapNone = 1,
    kTonemapBreak = 2,
    kTonemapAces = 3,
    kTonemapFilmic = 4,
    kTonemapHable = 5,
    kTonemapReinhard = 6,
    kTonemapReinhard2 = 7,
    kTonemapBreak2 = 8,
    kTonemapClampChroma = 9
};

enum ViewMode {
    kViewFinal = 1,
    kViewInput = 2,
    kViewIris = 3,
    kViewDirtInput = 4,
    kViewDirtMatte = 5,
    kViewDirtMatted = 6
};

enum QualityPreset {
    kQualityDraft = 1,
    kQualityLow = 2,
    kQualityMedium = 3,
    kQualityStandard = 4,
    kQualityHigh = 5,
    kQualityExtreme = 6,
    kQualityCustom = 8
};

struct RGBAf {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// ---------------------------------------------------------------------------
// parameter block.  All "UI" units are the ones shown in the AE effect
// controls; the *100 scaling of the original is applied by the OFX layer.
// ---------------------------------------------------------------------------
struct Params {
    // main
    int glowMode = 1;                 // 1 = Exponential, 2 = Lens Iris (unsupported)
    int blendMode = kBlendScreen;
    double radius = 1000.0;           // AE default, valid 0..2000
    double exposure = 1.0;

    // input / threshold
    int thresholdMode = kThresholdChroma;
    double threshold = 0.0;           // scene units (UI value / 100)
    double thresholdSmooth = 0.0;     // scene units (UI value / 100)
    double satBias = 0.0;             // -1..1

    bool useMask = false;
    int maskMode = 1;                 // 1 = Alpha, 2 = Luminance
    bool maskInvert = false;

    // style / gamma
    bool gammaCorrect = false;
    double gamma = 1.0;               // custom_gammaF (UI 10..300 -> /100)
    double aspectRatio = 1.0;         // 1..2
    bool enableAngle = false;
    double angle = 0.0;               // degrees

    // chromatic aberration
    bool aberrationPixel = false;
    int aberrationChannels = kAberrationRG;
    double pixelOffset = 1.0;         // pixels

    bool aberrationGlow = false;
    double multR = 0.8, multG = 1.0, multB = 1.2;   // sigma multipliers

    // tint
    int tintMode = kTintOff;
    int tintBlend = kTintMultiply;
    RGBAf tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    RGBAf tintColorOuter{0.0f, 0.0f, 0.0f, 1.0f};
    double tintStrength = 1.0;        // 0..2  (UI / 100)
    double innerOuterBias = 0.5;      // 0..1

    // tone mapping
    int tonemapMode = kTonemapNone;
    double tonemapMix = 1.0;

    // lens dirt (approximated, see docs/06-limitations.md)
    double dirtOpacity = 1.0;
    double dirtInherit = 0.0;
    double dirtMatteExposure = 0.0;
    double dirtMatteGamma = 1.0;

    // quality
    int qualityPreset = kQualityStandard;
    double resolution = 0.75;         // 0.25..1.0
    double stepsMultiplier = 0.5;
    int glowIterations = 8;
    bool halfFloat = false;
    bool bufferExpansionCompSize = false;
    double customWeight = 0.9;        // only consulted by the Custom preset

    // output
    int view = kViewFinal;
    double sourceOpacity = 1.0;
    bool unmult = false;

    // debug
    bool debugW = false;   // Debug View = "Blur Width": show the X pass only
    bool debugH = false;   // Debug View = "Blur Height": show the Y pass only
    bool preProcSolidFill = false;
};

// ---------------------------------------------------------------------------
// "Debug View" choice indices.
//
// The OFX choice order is the AE order: 0 = Blur Width, 1 = Blur Height,
// 2 = None.  These are *choice indices*, not AE enum values (the AE enum is
// 1-based), so never compare a raw choice value against 1/2 directly — that
// mistake made the default render skip the X pass and look vertically
// streaked.  Use debugAxesFromChoice().
// ---------------------------------------------------------------------------
enum DebugViewChoice {
    kDebugViewWidth = 0,    // show the X pass only
    kDebugViewHeight = 1,   // show the Y pass only
    kDebugViewNone = 2      // full render
};

inline void debugAxesFromChoice(int choice, bool& widthOnly, bool& heightOnly) {
    widthOnly = (choice == kDebugViewWidth);
    heightOnly = (choice == kDebugViewHeight);
}

// ---------------------------------------------------------------------------
// quality preset table (setup fn 0x28a229..0x28a2b1)
//   Draft   : steps*0.25  weight 0.80
//   Low     : steps*0.25  weight 0.85
//   Medium  : steps*0.33  weight 0.90
//   Standard: steps*0.50  weight 0.90
//   High    : steps*0.50  weight 0.97
//   Extreme : steps*1.00  weight 1.00
// ---------------------------------------------------------------------------
struct QualitySettings {
    double stepsMultiplier = 0.5;
    double weight = 0.9;
};

inline QualitySettings qualityFromPreset(const Params& p) {
    QualitySettings q;
    switch (p.qualityPreset) {
        case kQualityDraft:    q.stepsMultiplier = 0.25;      q.weight = 0.8;  break;
        case kQualityLow:      q.stepsMultiplier = 0.25;      q.weight = 0.85; break;
        case kQualityMedium:   q.stepsMultiplier = 0.3300000131; q.weight = 0.9; break;
        case kQualityStandard: q.stepsMultiplier = 0.5;       q.weight = 0.9;  break;
        case kQualityHigh:     q.stepsMultiplier = 0.5;       q.weight = 0.97; break;
        case kQualityExtreme:  q.stepsMultiplier = 1.0;       q.weight = 1.0;  break;
        default:               q.stepsMultiplier = p.stepsMultiplier; q.weight = p.customWeight; break;
    }
    return q;
}

// ---------------------------------------------------------------------------
// one entry of the blur pass list (record size 0x38 in the original)
// ---------------------------------------------------------------------------
struct PassInfo {
    float weight = 1.0f;   // +0x00  resolution weight of this octave
    int w = 1;             // +0x04  virtual (full-comp) buffer width, +wx
    int h = 1;             // +0x08  virtual (full-comp) buffer height
    int stepsX = 1;        // +0x0c  taps per side, x
    int stepsY = 1;        // +0x10  taps per side, y
    float strideX = 0.0f;  // +0x14  UV step, x
    float strideY = 0.0f;  // +0x18  UV step, y
    float size = 0.0f;     // +0x1c  blur span in comp pixels (pre-weight * weight)
    double spanX = 0.0;    // +0x20  size * aspect span
    double spanY = 0.0;    // +0x28  size * |2 - aspect span|
    float fade = 1.0f;     // +0x30  attenuation applied on the width pass

    // --- extras that only exist in this port ------------------------------
    float density = 0.25f; // density value used for the tap counts (0x6fa8)
    int workW = 1;         // *actual* buffer width  = compW * resolution * weight
    int workH = 1;         // *actual* buffer height = compH * resolution * weight
};

// fade ramps, indexed by (passIndex - 1) for presets != Extreme
// (lo, span) pairs, transcribed from 0x00e93d..0x00eb30
inline void fadeRamp(int rampIndex, float& lo, float& span) {
    static const float kLo[10] = {0.f, 0.f, 5.f, 10.f, 20.f, 35.f, 60.f, 90.f, 110.f, 130.f};
    static const float kSpan[10] = {20.f, 40.f, 55.f, 70.f, 100.f, 105.f, 100.f, 90.f, 110.f, 150.f};
    const int i = std::clamp(rampIndex, 0, 9);
    lo = kLo[i];
    span = kSpan[i];
}

// ---------------------------------------------------------------------------
// buildPasses — transcription of RVA 0x00e600.
//
//   `size0` (ctx+0x7064) already holds radius * compAspect, i.e. the radius in
//   comp *pixels* (Radius is expressed in comp-height units, so it is scaled by
//   the comp aspect ratio).  It is then halved `passCount` times, stopping early
//   once the span drops to one pixel or below.
//
//   In Exponential mode the pass count is *fixed at ten*: the setup function
//   writes 0x6fac = 10 at 0x28a303 for every quality preset.  "Glow Iterations"
//   (param +0x2360) is only copied into 0x6fac on the Lens Iris branch
//   (0x289f13), which is why the control has no effect in Exponential mode.
//
//   Every octave doubles its span; the *buffer* resolution is shrunk by the
//   quality weight so large blurs are evaluated at low resolution.  The UV
//   strides are computed against those shrunken dims, which cancels the weight
//   again: pass i covers size_i * aspect pixels of the comp.  The user
//   "Resolution" slider scales the buffers *and* the tap density, so it changes
//   sampling quality but not the glow footprint (see docs/04-pipeline.md).
// ---------------------------------------------------------------------------
inline std::vector<PassInfo> buildPasses(const Params& p, double compAspect, int compW,
                                         int compH) {
    std::vector<PassInfo> passes;
    const QualitySettings q = qualityFromPreset(p);
    const bool custom = (p.qualityPreset == kQualityCustom);
    const bool extreme = (p.qualityPreset == kQualityExtreme);
    const double resolution = std::clamp(p.resolution, 0.05, 1.0);

    // --- 1. halving loop (0x18000e647..0x18000e68f) ------------------------
    const int requested = 10;   // 0x6fac is forced to 10 in the setup function
    double size = std::max(0.0, p.radius) * compAspect;
    int count = 0;
    while (count < requested) {
        ++count;
        size *= 0.5;
        if (size <= 1.0) break;
    }
    const double size0 = size;

    const double aspectX = std::max(0.0, p.aspectRatio);
    const double aspectY = std::fabs(2.0 - p.aspectRatio);

    // --- 2. per pass: size, weight, fade ----------------------------------
    double preSize = size0;
    float weight = 1.0f;
    for (int i = 0; i < count; ++i) {
        if (i > 0) preSize *= 2.0;

        if (i == 0) {
            weight = custom ? static_cast<float>(q.weight)
                            : (p.qualityPreset >= kQualityStandard ? 1.0f
                                                                   : static_cast<float>(q.weight));
        } else {
            weight = custom ? static_cast<float>(q.weight)
                            : static_cast<float>(q.weight) * weight;
        }

        PassInfo pi;
        pi.weight = weight;
        pi.size = static_cast<float>(preSize);

        // fade (0x18000e8df..0x18000eb3e)
        if (extreme) {
            const double a = preSize * aspectX;
            const double b = preSize * aspectY;
            const double v = 0.2 * std::max(a, b);
            pi.fade = static_cast<float>(std::min(1.0, std::max(0.0, v)));
        } else if (custom || i == 0) {
            pi.fade = 1.0f;
        } else {
            float lo = 0.f, span = 1.f;
            fadeRamp(i - 1, lo, span);
            const float v = (static_cast<float>(p.radius) - lo) / std::max(1e-6f, span);
            pi.fade = std::clamp(v, 0.0f, 1.0f);
        }

        // A pass whose fade reaches zero truncates the list *after* itself
        // (0x18000eb7b).  The extreme branch jumps over this check.
        const bool truncated = (!extreme && pi.fade == 0.0f);
        passes.push_back(pi);
        if (truncated) break;
    }

    // --- Extreme post-pass (0x18000eb97..0x18000ec57) ----------------------
    // The deepest pass is forced to full fade and to a minimum span of one
    // pixel, and the preset is then rewritten to "Custom" (0x6f78 = 8).
    if (extreme && !passes.empty()) {
        passes.back().fade = 1.0f;
        passes.back().size = std::max(1.0f, passes.back().size);
    }

    // --- 3. second phase: buffer size, steps and UV strides ----------------
    // Density (0x6fa8) = quality value / comp aspect (0x28a2e0), Custom uses the
    // raw "Steps Multiplier" parameter.
    const float density = static_cast<float>(q.stepsMultiplier / std::max(1e-6, compAspect));
    const int passCount = static_cast<int>(passes.size());

    for (int i = 0; i < passCount; ++i) {
        PassInfo& pi = passes[i];
        pi.size *= pi.weight;
        pi.density = density;

        pi.spanX = static_cast<double>(pi.size) * aspectX;
        pi.spanY = static_cast<double>(pi.size) * aspectY;

        // Virtual dims: these are what the original divides the UV strides by.
        pi.w = std::max(1, static_cast<int>(std::lround(static_cast<double>(compW) * pi.weight)));
        pi.h = std::max(1, static_cast<int>(std::lround(static_cast<double>(compH) * pi.weight)));

        // Actual buffers used by this port (Resolution shrinks them).
        pi.workW = std::max(
            1, static_cast<int>(std::lround(static_cast<double>(compW) * resolution * pi.weight)));
        pi.workH = std::max(
            1, static_cast<int>(std::lround(static_cast<double>(compH) * resolution * pi.weight)));

        pi.stepsX = std::max(1, static_cast<int>(std::lround(density * pi.spanX)));
        pi.stepsY = std::max(1, static_cast<int>(std::lround(density * pi.spanY)));

        pi.strideX = static_cast<float>((pi.spanX / pi.stepsX) / static_cast<double>(pi.w));
        pi.strideY = static_cast<float>((pi.spanY / pi.stepsY) / static_cast<double>(pi.h));
    }

    return passes;
}

} // namespace dg
