// Deep Glow 2 — OFX port
// DeepGlowOFX.cpp : OFX / DaVinci Resolve glue.
//
// This file maps the recovered After Effects effect onto the OFX 1.4 filter
// API.  It owns:
//   * the plugin factory (describe / describeInContext / createInstance)
//   * the parameter table (names, ranges and defaults taken from the AE
//     parameter block of the original binary, see docs/05-ofx-mapping.md)
//   * the AE-units -> scene-units conversion in buildParams()
//   * image load/store including float16, and the AE => OFX premultiplication
//     difference
//
// All of the actual image processing lives in DGPipeline.cpp.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsLog.h"

#include "DGParams.h"
#include "DGPipeline.h"

namespace {

using namespace OFX;

// ---------------------------------------------------------------------------
// plugin identity
// ---------------------------------------------------------------------------
const char* const kPluginName        = "Deep Glow 2";
const char* const kPluginGrouping    = "DeepGlow";
const char* const kPluginDescription =
    "Physically inspired multi octave glow with chromatic aberration, tinting and "
    "tone mapping. OFX port of the Deep Glow 2 After Effects plugin.";
const char* const kPluginIdentifier  = "com.deepglow.DeepGlow2";
const int         kPluginVersionMajor = 1;
const int         kPluginVersionMinor = 0;

// clip names (the source/output names are mandated by OFX)
const char* const kClipMask = "Mask";
const char* const kClipDirt = "LensDirt";

// ---------------------------------------------------------------------------
// float16 <-> float32
// ---------------------------------------------------------------------------
inline float halfToFloat(unsigned short h) {
    const unsigned int sign = (static_cast<unsigned int>(h) >> 15) & 1u;
    const unsigned int exp  = (static_cast<unsigned int>(h) >> 10) & 0x1fu;
    const unsigned int mant = static_cast<unsigned int>(h) & 0x3ffu;
    float v;
    if (exp == 0u) {
        v = (mant == 0u) ? 0.0f : std::ldexp(static_cast<float>(mant), -24);
    } else if (exp == 31u) {
        v = (mant == 0u) ? std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
    } else {
        v = std::ldexp(static_cast<float>(mant | 0x400u), static_cast<int>(exp) - 25);
    }
    return sign ? -v : v;
}

inline unsigned short floatToHalf(float f) {
    unsigned int x = 0;
    std::memcpy(&x, &f, sizeof(x));
    const unsigned int sign = (x >> 16) & 0x8000u;
    const int expRaw = static_cast<int>((x >> 23) & 0xffu);
    const unsigned int mant = x & 0x7fffffu;

    if (expRaw == 0xff) {                                   // inf / nan
        return static_cast<unsigned short>(sign | 0x7c00u | (mant ? 0x200u : 0u));
    }
    int exp = expRaw - 127 + 15;
    if (exp <= 0) {                                          // subnormal / zero
        if (exp < -10) return static_cast<unsigned short>(sign);
        const unsigned int m = mant | 0x800000u;
        const int shift = 14 - exp;
        unsigned int r = m >> shift;
        const unsigned int guard = 1u << (shift - 1);
        const unsigned int sticky = (shift > 1) ? (m & (guard - 1u)) : 0u;
        if ((m & guard) && (sticky || (r & 1u))) ++r;        // round to nearest even
        return static_cast<unsigned short>(sign | r);
    }
    if (exp >= 31) return static_cast<unsigned short>(sign | 0x7c00u);

    unsigned int m = mant >> 13;
    if (mant & 0x1000u) {
        const unsigned int sticky = mant & 0x0fffu;
        if (sticky || (m & 1u)) {                            // round to nearest even
            ++m;
            if (m == 0x400u) { m = 0; ++exp; }
        }
    }
    if (exp >= 31) return static_cast<unsigned short>(sign | 0x7c00u);
    return static_cast<unsigned short>(sign | (static_cast<unsigned int>(exp) << 10) | m);
}

// ---------------------------------------------------------------------------
// Image <-> ImageF conversion
//
// OFX hands us either float or half buffers, with 1..4 components.  The
// pipeline always wants straight (non premultiplied) RGBA float.
// ---------------------------------------------------------------------------
bool loadImage(Image& img, dg::ImageF& out, bool unpremultiply) {
    const OfxRectI b = img.getBounds();
    const int w = b.x2 - b.x1;
    const int h = b.y2 - b.y1;
    if (w <= 0 || h <= 0) return false;

    out.resize(w, h);

    const int comps = std::max(1, img.getPixelComponentCount());
    const bool half = (img.getPixelDepth() == eBitDepthHalf);

    for (int y = 0; y < h; ++y) {
        const void* rowp = img.getPixelAddress(b.x1, b.y1 + y);
        if (!rowp) {
            std::memset(out.at(0, y), 0, sizeof(float) * 4 * static_cast<size_t>(w));
            continue;
        }
        for (int x = 0; x < w; ++x) {
            float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (half) {
                const unsigned short* q =
                    static_cast<const unsigned short*>(rowp) + static_cast<size_t>(x) * comps;
                for (int k = 0; k < comps && k < 4; ++k) c[k] = halfToFloat(q[k]);
            } else {
                const float* q = static_cast<const float*>(rowp) + static_cast<size_t>(x) * comps;
                for (int k = 0; k < comps && k < 4; ++k) c[k] = q[k];
            }

            if (comps == 1) {              // alpha only
                c[3] = c[0];
                c[0] = c[1] = c[2] = 0.0f;
            } else if (comps == 3) {       // rgb, opaque
                c[3] = 1.0f;
            }

            if (unpremultiply) {
                const float a = c[3];
                if (a > 1e-6f) {
                    c[0] /= a; c[1] /= a; c[2] /= a;
                } else {
                    c[0] = c[1] = c[2] = 0.0f;
                }
            }

            float* o = out.at(x, y);
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3];
        }
    }
    return true;
}

bool storeImage(const dg::ImageF& img, Image& dst) {
    const OfxRectI b = dst.getBounds();
    const int w = b.x2 - b.x1;
    const int h = b.y2 - b.y1;
    if (w <= 0 || h <= 0) return false;

    const int comps = std::max(1, dst.getPixelComponentCount());
    const bool half = (dst.getPixelDepth() == eBitDepthHalf);

    for (int y = 0; y < h; ++y) {
        void* rowp = dst.getPixelAddress(b.x1, b.y1 + y);
        if (!rowp) continue;
        for (int x = 0; x < w; ++x) {
            float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (x < img.w && y < img.h) {
                const float* s = img.at(x, y);
                c[0] = s[0]; c[1] = s[1]; c[2] = s[2]; c[3] = s[3];
            }
            if (half) {
                unsigned short* q =
                    static_cast<unsigned short*>(rowp) + static_cast<size_t>(x) * comps;
                for (int k = 0; k < comps && k < 4; ++k) q[k] = floatToHalf(c[k]);
            } else {
                float* q = static_cast<float*>(rowp) + static_cast<size_t>(x) * comps;
                for (int k = 0; k < comps && k < 4; ++k) q[k] = c[k];
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// parameter name constants
// ---------------------------------------------------------------------------
namespace pn {
const char* const glowMode        = "glowMode";
const char* const blendMode       = "blendMode";
const char* const radius          = "radius";
const char* const exposure        = "exposure";

const char* const thresholdMode   = "thresholdMode";
const char* const threshold       = "threshold";
const char* const thresholdSmooth = "thresholdSmooth";
const char* const satBias         = "satBias";

const char* const maskEnabled     = "maskEnabled";
const char* const maskMode        = "maskMode";
const char* const maskInvert      = "maskInvert";

const char* const gammaCorrect    = "gammaCorrect";
const char* const gamma           = "gamma";
const char* const aspectRatio     = "aspectRatio";
const char* const enableAngle     = "enableAngle";
const char* const angle           = "angle";

const char* const aberrationPixel    = "aberrationPixel";
const char* const aberrationChannels = "aberrationChannels";
const char* const pixelOffset        = "pixelOffset";
const char* const aberrationGlow     = "aberrationGlow";
const char* const multR              = "multR";
const char* const multG              = "multG";
const char* const multB              = "multB";

const char* const tintMode       = "tintMode";
const char* const tintBlend      = "tintBlend";
const char* const tintColor      = "tintColor";
const char* const tintColorOuter = "tintColorOuter";
const char* const tintStrength   = "tintStrength";

const char* const tonemapMode    = "tonemapMode";
const char* const tonemapMix     = "tonemapMix";

const char* const dirtEnabled       = "dirtEnabled";
const char* const dirtOpacity       = "dirtOpacity";
const char* const dirtInherit       = "dirtInherit";
const char* const dirtMatteExposure = "dirtMatteExposure";
const char* const dirtMatteGamma    = "dirtMatteGamma";

const char* const qualityPreset  = "qualityPreset";
const char* const resolution     = "resolution";
const char* const stepsMultiplier = "stepsMultiplier";
const char* const glowIterations = "glowIterations";
const char* const customWeight   = "customWeight";

const char* const view           = "view";
const char* const sourceOpacity  = "sourceOpacity";
const char* const unmult         = "unmult";

const char* const debugView      = "debugView";
const char* const preProcSolidFill = "preProcSolidFill";
} // namespace pn

// choice value tables — note the holes, they are the original AE enum values
const int kQualityValues[7] = {1, 2, 3, 4, 5, 6, 8};          // Draft..Extreme, Custom
const int kTonemapValues[7] = {1, 3, 4, 5, 6, 7, 9};          // None, ACES..Clamp Chroma
const int kViewValues[5]    = {1, 2, 4, 5, 6};                // Final, Input, Dirt Input/Matte/Matted

// "Debug View" choice order (AE order, see docs/05-ofx-mapping.md).  The stored
// value is the OFX choice index, not the AE enum value.  The index constants and
// the index -> Params mapping live in DGParams.h (dg::kDebugView*,
// dg::debugAxesFromChoice) so the descriptor and buildParams cannot drift apart.
const char* const kDebugChoiceLabels[3] = {"Blur Width", "Blur Height", "None"};

// ---------------------------------------------------------------------------
// parameter descriptors
// ---------------------------------------------------------------------------
void attach(PageParamDescriptor* page, GroupParamDescriptor* grp, ParamDescriptor* p) {
    if (grp) p->setParent(*grp);
    page->addChild(*p);
}

DoubleParamDescriptor* defDouble(ImageEffectDescriptor& d, const std::string& name,
                                 const std::string& label, double def, double lo, double hi,
                                 double dlo, double dhi, const std::string& hint = "") {
    DoubleParamDescriptor* p = d.defineDoubleParam(name);
    p->setLabels(label, label, label);
    p->setScriptName(name);
    if (!hint.empty()) p->setHint(hint);
    p->setDefault(def);
    p->setRange(lo, hi);
    p->setDisplayRange(dlo, dhi);
    return p;
}

IntParamDescriptor* defInt(ImageEffectDescriptor& d, const std::string& name,
                           const std::string& label, int def, int lo, int hi,
                           const std::string& hint = "") {
    IntParamDescriptor* p = d.defineIntParam(name);
    p->setLabels(label, label, label);
    p->setScriptName(name);
    if (!hint.empty()) p->setHint(hint);
    p->setDefault(def);
    p->setRange(lo, hi);
    p->setDisplayRange(lo, hi);
    return p;
}

BooleanParamDescriptor* defBool(ImageEffectDescriptor& d, const std::string& name,
                                const std::string& label, bool def, const std::string& hint = "") {
    BooleanParamDescriptor* p = d.defineBooleanParam(name);
    p->setLabels(label, label, label);
    p->setScriptName(name);
    if (!hint.empty()) p->setHint(hint);
    p->setDefault(def);
    return p;
}

ChoiceParamDescriptor* defChoice(ImageEffectDescriptor& d, const std::string& name,
                                 const std::string& label, int def,
                                 const std::vector<std::string>& items,
                                 const std::string& hint = "") {
    ChoiceParamDescriptor* p = d.defineChoiceParam(name);
    p->setLabels(label, label, label);
    p->setScriptName(name);
    if (!hint.empty()) p->setHint(hint);
    p->setDefault(def);
    for (size_t i = 0; i < items.size(); ++i) p->appendOption(items[i]);
    return p;
}

RGBAParamDescriptor* defRGBA(ImageEffectDescriptor& d, const std::string& name,
                             const std::string& label, double r, double g, double b, double a,
                             const std::string& hint = "") {
    RGBAParamDescriptor* p = d.defineRGBAParam(name);
    p->setLabels(label, label, label);
    p->setScriptName(name);
    if (!hint.empty()) p->setHint(hint);
    p->setDefault(r, g, b, a);
    p->setRange(0.0, 0.0, 0.0, 0.0, 4.0, 4.0, 4.0, 1.0);
    p->setDisplayRange(0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0);
    return p;
}

// ---------------------------------------------------------------------------
// typed parameter handles for one instance
// ---------------------------------------------------------------------------
struct Refs {
    ChoiceParam*  glowMode = nullptr;
    ChoiceParam*  blendMode = nullptr;
    DoubleParam*  radius = nullptr;
    DoubleParam*  exposure = nullptr;

    ChoiceParam*  thresholdMode = nullptr;
    DoubleParam*  threshold = nullptr;
    DoubleParam*  thresholdSmooth = nullptr;
    DoubleParam*  satBias = nullptr;

    BooleanParam* maskEnabled = nullptr;
    ChoiceParam*  maskMode = nullptr;
    BooleanParam* maskInvert = nullptr;

    BooleanParam* gammaCorrect = nullptr;
    DoubleParam*  gamma = nullptr;
    DoubleParam*  aspectRatio = nullptr;
    BooleanParam* enableAngle = nullptr;
    DoubleParam*  angle = nullptr;

    BooleanParam* aberrationPixel = nullptr;
    ChoiceParam*  aberrationChannels = nullptr;
    DoubleParam*  pixelOffset = nullptr;
    BooleanParam* aberrationGlow = nullptr;
    DoubleParam*  multR = nullptr;
    DoubleParam*  multG = nullptr;
    DoubleParam*  multB = nullptr;

    ChoiceParam*  tintMode = nullptr;
    ChoiceParam*  tintBlend = nullptr;
    RGBAParam*    tintColor = nullptr;
    RGBAParam*    tintColorOuter = nullptr;
    DoubleParam*  tintStrength = nullptr;

    ChoiceParam*  tonemapMode = nullptr;
    DoubleParam*  tonemapMix = nullptr;

    BooleanParam* dirtEnabled = nullptr;
    DoubleParam*  dirtOpacity = nullptr;
    DoubleParam*  dirtInherit = nullptr;
    DoubleParam*  dirtMatteExposure = nullptr;
    DoubleParam*  dirtMatteGamma = nullptr;

    ChoiceParam*  qualityPreset = nullptr;
    DoubleParam*  resolution = nullptr;
    DoubleParam*  stepsMultiplier = nullptr;
    IntParam*     glowIterations = nullptr;
    DoubleParam*  customWeight = nullptr;

    ChoiceParam*  view = nullptr;
    DoubleParam*  sourceOpacity = nullptr;
    BooleanParam* unmult = nullptr;

    ChoiceParam*  debugView = nullptr;
    BooleanParam* preProcSolidFill = nullptr;
};

// ---------------------------------------------------------------------------
// the effect
// ---------------------------------------------------------------------------
inline int intChoice(ChoiceParam* p) {
    int v = 0;
    if (p) p->getValue(v);
    return v;
}

inline int choiceAt(ChoiceParam* p, double t) {
    int v = 0;
    if (p) p->getValueAtTime(t, v);
    return v;
}

class DeepGlowEffect : public ImageEffect {
public:
    explicit DeepGlowEffect(OfxImageEffectHandle handle);

    void render(const RenderArguments& args) override;
    bool isIdentity(const IsIdentityArguments& args, Clip*& identityClip,
                    double& identityTime) override;
    void getClipPreferences(ClipPreferencesSetter& prefs) override;
    void getRegionsOfInterest(const RegionsOfInterestArguments& args,
                              RegionOfInterestSetter& rois) override;
    void changedParam(const InstanceChangedArgs& args, const std::string& paramName) override;
    void changedClip(const InstanceChangedArgs& args, const std::string& clipName) override;

private:
    dg::Params buildParams(double time) const;
    void refreshEnabled();

    // does not own these
    Clip* m_srcClip = nullptr;
    Clip* m_dstClip = nullptr;
    Clip* m_maskClip = nullptr;
    Clip* m_dirtClip = nullptr;
    Refs  m_p;
};

DeepGlowEffect::DeepGlowEffect(OfxImageEffectHandle handle) : ImageEffect(handle) {
    m_dstClip  = fetchClip(kOfxImageEffectOutputClipName);
    m_srcClip  = fetchClip(kOfxImageEffectSimpleSourceClipName);
    m_maskClip = fetchClip(kClipMask);
    m_dirtClip = fetchClip(kClipDirt);

    m_p.glowMode            = fetchChoiceParam(pn::glowMode);
    m_p.blendMode           = fetchChoiceParam(pn::blendMode);
    m_p.radius              = fetchDoubleParam(pn::radius);
    m_p.exposure            = fetchDoubleParam(pn::exposure);

    m_p.thresholdMode       = fetchChoiceParam(pn::thresholdMode);
    m_p.threshold           = fetchDoubleParam(pn::threshold);
    m_p.thresholdSmooth     = fetchDoubleParam(pn::thresholdSmooth);
    m_p.satBias             = fetchDoubleParam(pn::satBias);

    m_p.maskEnabled         = fetchBooleanParam(pn::maskEnabled);
    m_p.maskMode            = fetchChoiceParam(pn::maskMode);
    m_p.maskInvert          = fetchBooleanParam(pn::maskInvert);

    m_p.gammaCorrect        = fetchBooleanParam(pn::gammaCorrect);
    m_p.gamma               = fetchDoubleParam(pn::gamma);
    m_p.aspectRatio         = fetchDoubleParam(pn::aspectRatio);
    m_p.enableAngle         = fetchBooleanParam(pn::enableAngle);
    m_p.angle               = fetchDoubleParam(pn::angle);

    m_p.aberrationPixel     = fetchBooleanParam(pn::aberrationPixel);
    m_p.aberrationChannels  = fetchChoiceParam(pn::aberrationChannels);
    m_p.pixelOffset         = fetchDoubleParam(pn::pixelOffset);
    m_p.aberrationGlow      = fetchBooleanParam(pn::aberrationGlow);
    m_p.multR               = fetchDoubleParam(pn::multR);
    m_p.multG               = fetchDoubleParam(pn::multG);
    m_p.multB               = fetchDoubleParam(pn::multB);

    m_p.tintMode            = fetchChoiceParam(pn::tintMode);
    m_p.tintBlend           = fetchChoiceParam(pn::tintBlend);
    m_p.tintColor           = fetchRGBAParam(pn::tintColor);
    m_p.tintColorOuter      = fetchRGBAParam(pn::tintColorOuter);
    m_p.tintStrength        = fetchDoubleParam(pn::tintStrength);

    m_p.tonemapMode         = fetchChoiceParam(pn::tonemapMode);
    m_p.tonemapMix          = fetchDoubleParam(pn::tonemapMix);

    m_p.dirtEnabled         = fetchBooleanParam(pn::dirtEnabled);
    m_p.dirtOpacity         = fetchDoubleParam(pn::dirtOpacity);
    m_p.dirtInherit         = fetchDoubleParam(pn::dirtInherit);
    m_p.dirtMatteExposure   = fetchDoubleParam(pn::dirtMatteExposure);
    m_p.dirtMatteGamma      = fetchDoubleParam(pn::dirtMatteGamma);

    m_p.qualityPreset       = fetchChoiceParam(pn::qualityPreset);
    m_p.resolution          = fetchDoubleParam(pn::resolution);
    m_p.stepsMultiplier     = fetchDoubleParam(pn::stepsMultiplier);
    m_p.glowIterations      = fetchIntParam(pn::glowIterations);
    m_p.customWeight        = fetchDoubleParam(pn::customWeight);

    m_p.view                = fetchChoiceParam(pn::view);
    m_p.sourceOpacity       = fetchDoubleParam(pn::sourceOpacity);
    m_p.unmult              = fetchBooleanParam(pn::unmult);

    m_p.debugView           = fetchChoiceParam(pn::debugView);
    m_p.preProcSolidFill    = fetchBooleanParam(pn::preProcSolidFill);

    refreshEnabled();
}

// AE parameter values are expressed in "UI units" (0..100 percentages etc).
// Everything below converts to the scene units the pipeline expects; the
// /100 factors are the ones the original plugin applies when it uploads its
// uniforms (docs/04-pipeline.md).
dg::Params DeepGlowEffect::buildParams(double time) const {
    using namespace dg;
    Params p;

    // --- main -------------------------------------------------------------
    p.glowMode     = choiceAt(m_p.glowMode, time) + 1;     // 0 => Exponential
    p.blendMode    = (choiceAt(m_p.blendMode, time) == 1) ? kBlendAdd : kBlendScreen;
    p.radius       = m_p.radius->getValueAtTime(time);
    p.exposure     = m_p.exposure->getValueAtTime(time);

    // --- input ------------------------------------------------------------
    p.thresholdMode   = (choiceAt(m_p.thresholdMode, time) == 1) ? kThresholdLuma
                                                                      : kThresholdChroma;
    p.threshold       = m_p.threshold->getValueAtTime(time) * 0.01;
    p.thresholdSmooth = m_p.thresholdSmooth->getValueAtTime(time) * 0.01;
    p.satBias         = m_p.satBias->getValueAtTime(time) * 0.01;

    // --- mask -------------------------------------------------------------
    p.useMask    = m_p.maskEnabled->getValueAtTime(time);
    p.maskMode   = choiceAt(m_p.maskMode, time) + 1;       // 0 => Alpha
    p.maskInvert = m_p.maskInvert->getValueAtTime(time);

    // --- style ------------------------------------------------------------
    p.gammaCorrect = m_p.gammaCorrect->getValueAtTime(time);
    p.gamma        = m_p.gamma->getValueAtTime(time) * 0.01;
    p.aspectRatio  = m_p.aspectRatio->getValueAtTime(time);
    p.enableAngle  = m_p.enableAngle->getValueAtTime(time);
    p.angle        = m_p.angle->getValueAtTime(time);

    // --- aberration -------------------------------------------------------
    p.aberrationPixel    = m_p.aberrationPixel->getValueAtTime(time);
    p.aberrationChannels = choiceAt(m_p.aberrationChannels, time) + 1;  // 1..3
    p.pixelOffset        = m_p.pixelOffset->getValueAtTime(time);
    p.aberrationGlow     = m_p.aberrationGlow->getValueAtTime(time);
    p.multR              = m_p.multR->getValueAtTime(time) * 0.01;
    p.multG              = m_p.multG->getValueAtTime(time) * 0.01;
    p.multB              = m_p.multB->getValueAtTime(time) * 0.01;

    // --- tint -------------------------------------------------------------
    p.tintMode     = choiceAt(m_p.tintMode, time);          // 0 off, 1 mono, 2 duo
    p.tintBlend    = choiceAt(m_p.tintBlend, time) + 1;     // 1..3
    p.tintStrength = m_p.tintStrength->getValueAtTime(time) * 0.01;

    double r = 1.0, g = 1.0, b = 1.0, a = 1.0;
    m_p.tintColor->getValueAtTime(time, r, g, b, a);
    p.tintColor = RGBAf{static_cast<float>(r), static_cast<float>(g),
                        static_cast<float>(b), static_cast<float>(a)};
    m_p.tintColorOuter->getValueAtTime(time, r, g, b, a);
    p.tintColorOuter = RGBAf{static_cast<float>(r), static_cast<float>(g),
                             static_cast<float>(b), static_cast<float>(a)};

    // --- tone mapping -----------------------------------------------------
    const int tmIdx = std::clamp(choiceAt(m_p.tonemapMode, time), 0, 6);
    p.tonemapMode = kTonemapValues[tmIdx];
    p.tonemapMix  = m_p.tonemapMix->getValueAtTime(time) * 0.01;

    // --- lens dirt --------------------------------------------------------
    p.dirtOpacity       = m_p.dirtOpacity->getValueAtTime(time) * 0.01;
    p.dirtInherit       = m_p.dirtInherit->getValueAtTime(time) * 0.01;
    p.dirtMatteExposure = m_p.dirtMatteExposure->getValueAtTime(time);
    p.dirtMatteGamma    = m_p.dirtMatteGamma->getValueAtTime(time);

    // --- quality ----------------------------------------------------------
    const int qIdx = std::clamp(choiceAt(m_p.qualityPreset, time), 0, 6);
    p.qualityPreset    = kQualityValues[qIdx];
    p.resolution       = m_p.resolution->getValueAtTime(time) * 0.01;
    p.stepsMultiplier  = m_p.stepsMultiplier->getValueAtTime(time);
    p.glowIterations   = m_p.glowIterations->getValueAtTime(time);
    p.customWeight     = m_p.customWeight->getValueAtTime(time);

    // --- output -----------------------------------------------------------
    const int vIdx = std::clamp(choiceAt(m_p.view, time), 0, 4);
    p.view          = kViewValues[vIdx];
    p.sourceOpacity = m_p.sourceOpacity->getValueAtTime(time) * 0.01;
    p.unmult        = m_p.unmult->getValueAtTime(time);

    // --- debug ------------------------------------------------------------
    // AE order is {Blur Width, Blur Height, None}; the OFX choice index is not
    // the AE enum value, so go through the shared mapping helper.
    debugAxesFromChoice(choiceAt(m_p.debugView, time), p.debugW, p.debugH);
    p.preProcSolidFill = m_p.preProcSolidFill->getValueAtTime(time);

    return p;
}

void DeepGlowEffect::render(const RenderArguments& args) {
    if (!m_srcClip->isConnected()) throwSuiteStatusException(kOfxStatErrValue);

    std::unique_ptr<Image> dst(m_dstClip->fetchImage(args.time));
    if (!dst) throwSuiteStatusException(kOfxStatErrValue);
    std::unique_ptr<Image> src(m_srcClip->fetchImage(args.time));
    if (!src) throwSuiteStatusException(kOfxStatErrValue);

    const BitDepthEnum depth = dst->getPixelDepth();
    if (depth != eBitDepthFloat && depth != eBitDepthHalf) {
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    // The original uploads the AE layer premultiplied and the shaders only ever
    // touch .rgb, so the whole pipeline works on straight colour.  Convert.
    dg::ImageF source;
    if (!loadImage(*src, source, true)) throwSuiteStatusException(kOfxStatErrBadHandle);

    const dg::Params p = buildParams(args.time);

    // optional inputs
    dg::ImageF maskImg, dirtImg;
    const bool wantMask = p.useMask && m_maskClip && m_maskClip->isConnected();
    if (wantMask) {
        std::unique_ptr<Image> m(m_maskClip->fetchImage(args.time));
        if (m) loadImage(*m, maskImg, false);
    }
    const bool wantDirt = m_p.dirtEnabled->getValueAtTime(args.time) && m_dirtClip &&
                          m_dirtClip->isConnected();
    if (wantDirt) {
        std::unique_ptr<Image> d(m_dirtClip->fetchImage(args.time));
        if (d) loadImage(*d, dirtImg, false);
    }

    dg::RenderResult result = dg::renderGlow(
        source, p,
        (wantMask && !maskImg.empty()) ? &maskImg : nullptr,
        (wantDirt && !dirtImg.empty()) ? &dirtImg : nullptr,
        [this]() { return this->abort(); });

    if (result.image.empty()) return;   // aborted

    if (!storeImage(result.image, *dst)) throwSuiteStatusException(kOfxStatErrBadHandle);
}

bool DeepGlowEffect::isIdentity(const IsIdentityArguments& /*args*/, Clip*& /*identityClip*/,
                                double& /*identityTime*/) {
    // Even Radius 0 changes the image (the glow is screened onto the source),
    // so there is no identity configuration.
    return false;
}

void DeepGlowEffect::getClipPreferences(ClipPreferencesSetter& prefs) {
    if (m_srcClip && m_srcClip->isConnected()) prefs.setClipComponents(*m_srcClip, ePixelComponentRGBA);
    if (m_maskClip && m_maskClip->isConnected()) prefs.setClipComponents(*m_maskClip, ePixelComponentRGBA);
    if (m_dirtClip && m_dirtClip->isConnected()) prefs.setClipComponents(*m_dirtClip, ePixelComponentRGBA);

    // "Unmult" is the AE workaround for AE's premultiplied alpha pipeline.  In
    // OFX we express the same thing as a real premultiplication preference.
    const bool un = m_p.unmult && m_p.unmult->getValue();
    prefs.setOutputPremultiplication(un ? eImageUnPreMultiplied : eImagePreMultiplied);
    prefs.setOutputHasContinousSamples(false);
}

void DeepGlowEffect::getRegionsOfInterest(const RegionsOfInterestArguments& args,
                                          RegionOfInterestSetter& rois) {
    // A multi octave glow needs the whole frame: even a single output pixel can
    // be influenced by any source pixel (the blur uses mirror-repeat at the
    // borders).  So request the full RoD of every connected input.
    if (m_srcClip && m_srcClip->isConnected()) {
        rois.setRegionOfInterest(*m_srcClip, m_srcClip->getRegionOfDefinition(args.time));
    }
    if (m_maskClip && m_maskClip->isConnected()) {
        rois.setRegionOfInterest(*m_maskClip, m_maskClip->getRegionOfDefinition(args.time));
    }
    if (m_dirtClip && m_dirtClip->isConnected()) {
        rois.setRegionOfInterest(*m_dirtClip, m_dirtClip->getRegionOfDefinition(args.time));
    }
}

void DeepGlowEffect::changedParam(const InstanceChangedArgs& /*args*/,
                                  const std::string& paramName) {
    if (paramName == pn::maskEnabled || paramName == pn::aberrationPixel ||
        paramName == pn::aberrationGlow || paramName == pn::tintMode ||
        paramName == pn::tonemapMode || paramName == pn::dirtEnabled ||
        paramName == pn::qualityPreset || paramName == pn::gammaCorrect ||
        paramName == pn::unmult) {
        refreshEnabled();
    }
}

void DeepGlowEffect::changedClip(const InstanceChangedArgs& /*args*/,
                                 const std::string& clipName) {
    if (clipName == kOfxImageEffectSimpleSourceClipName) refreshEnabled();
}

void DeepGlowEffect::refreshEnabled() {
    if (!m_p.radius) return;   // params not defined (should not happen)

    const bool maskOn = m_p.maskEnabled->getValue();
    if (m_p.maskMode) m_p.maskMode->setEnabled(maskOn);
    if (m_p.maskInvert) m_p.maskInvert->setEnabled(maskOn);

    const bool abPixel = m_p.aberrationPixel->getValue();
    m_p.aberrationChannels->setEnabled(abPixel);
    m_p.pixelOffset->setEnabled(abPixel);

    const bool abGlow = m_p.aberrationGlow->getValue();
    m_p.multR->setEnabled(abGlow);
    m_p.multG->setEnabled(abGlow);
    m_p.multB->setEnabled(abGlow);

    const int tint = intChoice(m_p.tintMode);
    m_p.tintBlend->setEnabled(tint != 0);
    m_p.tintColor->setEnabled(tint != 0);
    m_p.tintColorOuter->setEnabled(tint == 2);
    m_p.tintStrength->setEnabled(tint != 0);

    m_p.tonemapMix->setEnabled(intChoice(m_p.tonemapMode) != 0);

    const bool dirtOn = m_p.dirtEnabled->getValue();
    m_p.dirtOpacity->setEnabled(dirtOn);
    m_p.dirtInherit->setEnabled(dirtOn);
    m_p.dirtMatteExposure->setEnabled(dirtOn);
    m_p.dirtMatteGamma->setEnabled(dirtOn);

    m_p.gamma->setEnabled(m_p.gammaCorrect->getValue());
    m_p.angle->setEnabled(m_p.enableAngle->getValue());
    m_p.customWeight->setEnabled(intChoice(m_p.qualityPreset) == 6);   // "Custom"
}

// ---------------------------------------------------------------------------
// factory
// ---------------------------------------------------------------------------
mDeclarePluginFactory(DeepGlowFactory, {}, {});

void DeepGlowFactory::describe(ImageEffectDescriptor& desc) {
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthHalf);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(false);
    desc.setSupportsTiles(false);          // the blur is not tileable
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setSupportsMultipleClipDepths(false);
#ifdef DG_OFX_HAS_NO_SPATIAL_AWARENESS
    desc.setNoSpatialAwareness(false);     // output depends on neighbours
#endif
}

void DeepGlowFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum /*context*/) {
    // ---- clips -----------------------------------------------------------
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->setLabels("Source", "Source", "Source");
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    ClipDescriptor* dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->setLabels("Output", "Output", "Output");
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(false);

    // Optional input matte.  Replaces the AE "Mask Layer" layer parameter.
    ClipDescriptor* maskClip = desc.defineClip(kClipMask);
    maskClip->setLabels("Mask", "Mask", "Mask");
    maskClip->addSupportedComponent(ePixelComponentRGBA);
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    maskClip->setSupportsTiles(false);
    maskClip->setOptional(true);
    maskClip->setIsMask(true);

    // Optional lens dirt texture.  Replaces the AE "Texture" layer parameter.
    ClipDescriptor* dirtClip = desc.defineClip(kClipDirt);
    dirtClip->setLabels("Lens Dirt", "Lens Dirt", "Lens Dirt");
    dirtClip->addSupportedComponent(ePixelComponentRGBA);
    dirtClip->addSupportedComponent(ePixelComponentRGB);
    dirtClip->setTemporalClipAccess(false);
    dirtClip->setSupportsTiles(false);
    dirtClip->setOptional(true);
    dirtClip->setIsMask(false);

    // ---- pages and groups ------------------------------------------------
    PageParamDescriptor* page = desc.definePageParam("Controls");

    GroupParamDescriptor* gMain   = desc.defineGroupParam("grpMain");
    GroupParamDescriptor* gInput  = desc.defineGroupParam("grpInput");
    GroupParamDescriptor* gMask   = desc.defineGroupParam("grpMask");
    GroupParamDescriptor* gStyle  = desc.defineGroupParam("grpStyle");
    GroupParamDescriptor* gAb     = desc.defineGroupParam("grpAberration");
    GroupParamDescriptor* gTint   = desc.defineGroupParam("grpTint");
    GroupParamDescriptor* gTone   = desc.defineGroupParam("grpTonemap");
    GroupParamDescriptor* gDirt   = desc.defineGroupParam("grpLensDirt");
    GroupParamDescriptor* gQual   = desc.defineGroupParam("grpQuality");
    GroupParamDescriptor* gOut    = desc.defineGroupParam("grpOutput");
    GroupParamDescriptor* gDebug  = desc.defineGroupParam("grpDebug");

    gMain->setLabels("Main", "Main", "Main");
    gInput->setLabels("Input", "Input", "Input");
    gMask->setLabels("Input Masking", "Input Masking", "Input Masking");
    gStyle->setLabels("Style", "Style", "Style");
    gAb->setLabels("Chromatic Aberration", "Chromatic Aberration", "Chromatic Aberration");
    gTint->setLabels("Tint", "Tint", "Tint");
    gTone->setLabels("Tone Mapping", "Tone Mapping", "Tone Mapping");
    gDirt->setLabels("Lens Dirt", "Lens Dirt", "Lens Dirt");
    gQual->setLabels("Quality", "Quality", "Quality");
    gOut->setLabels("Output", "Output", "Output");
    gDebug->setLabels("Debug", "Debug", "Debug");

    attach(page, nullptr, gMain);
    attach(page, nullptr, gInput);
    attach(page, nullptr, gMask);
    attach(page, nullptr, gStyle);
    attach(page, nullptr, gAb);
    attach(page, nullptr, gTint);
    attach(page, nullptr, gTone);
    attach(page, nullptr, gDirt);
    attach(page, nullptr, gQual);
    attach(page, nullptr, gOut);
    attach(page, nullptr, gDebug);

    // ---- Main ------------------------------------------------------------
    attach(page, gMain, defChoice(desc, pn::glowMode, "Glow Mode", 0,
        {"Exponential", "Lens Iris (not implemented - uses Exponential)"},
        "The original's Lens Iris mode is not part of this port; it renders the "
        "Exponential glow instead. See docs/06-limitations.md."));
    attach(page, gMain, defChoice(desc, pn::blendMode, "Blend Mode", 0,
        {"Screen", "Add"}, "How the glow is combined with the source."));
    attach(page, gMain, defDouble(desc, pn::radius, "Radius", 1000.0, 0.0, 2000.0, 0.0, 500.0,
        "Glow radius in composition-height units (AE default 1000)."));
    attach(page, gMain, defDouble(desc, pn::exposure, "Exposure", 1.0, 0.0, 20.0, 0.9, 1.1,
        "Glow exposure. Applied to the blur input (Add) or the blur result (Screen)."));

    // ---- Input -----------------------------------------------------------
    attach(page, gInput, defChoice(desc, pn::thresholdMode, "Threshold Mode", 0,
        {"Chrominance", "Luminance"}, "Threshold is evaluated per channel or on luminance."));
    attach(page, gInput, defDouble(desc, pn::threshold, "Threshold", 0.0, 0.0, 500.0, 0.0, 200.0,
        "Values below this are removed before blurring (percentage, 100 = 1.0)."));
    attach(page, gInput, defDouble(desc, pn::thresholdSmooth, "Threshold Smooth", 0.0,
        0.0, 100.0, 0.0, 100.0, "Width of the smoothstep around the threshold."));
    attach(page, gInput, defDouble(desc, pn::satBias, "Saturation Bias", 0.0,
        -100.0, 100.0, -100.0, 100.0,
        "Biases the threshold towards saturated (positive) or desaturated (negative) pixels."));

    // ---- Input masking ---------------------------------------------------
    attach(page, gMask, defBool(desc, pn::maskEnabled, "Enable Mask", false,
        "Multiply the glow input by the optional Mask input."));
    attach(page, gMask, defChoice(desc, pn::maskMode, "Mask Mode", 0,
        {"Alpha", "Luminance"}, "Which channel of the Mask input to use."));
    attach(page, gMask, defBool(desc, pn::maskInvert, "Mask Invert", false, "Invert the matte."));

    // ---- Style -----------------------------------------------------------
    attach(page, gStyle, defBool(desc, pn::gammaCorrect, "Enable Gamma Correction", false,
        "Apply a scene gamma ramp to the glow (the original's \"Auto Detect Gamma\" switch)."));
    attach(page, gStyle, defDouble(desc, pn::gamma, "Gamma Correction", 100.0, 0.0, 300.0, 0.0, 100.0,
        "Scene gamma in percent (100 = 1.0, no change)."));
    attach(page, gStyle, defDouble(desc, pn::aspectRatio, "Aspect Ratio", 1.0, 0.0, 2.0, 0.0, 2.0,
        "Horizontal/vertical stretch of the glow. 1 = round, 2 = stretched horizontally."));
    attach(page, gStyle, defBool(desc, pn::enableAngle, "Enable Angle", false,
        "Use a single directional blur at the given angle instead of separable blur."));
    attach(page, gStyle, defDouble(desc, pn::angle, "Aspect Angle", 0.0, -180.0, 180.0, -180.0, 180.0,
        "Direction of the directional blur, in degrees."));

    // ---- Chromatic aberration --------------------------------------------
    attach(page, gAb, defBool(desc, pn::aberrationPixel, "Enable Pixel Aberration", false,
        "Sample the input with a per channel pixel offset."));
    attach(page, gAb, defChoice(desc, pn::aberrationChannels, "Aberration Channels", 0,
        {"Red & Green", "Red & Blue", "Green & Blue"}, "Which channel pair is separated."));
    attach(page, gAb, defDouble(desc, pn::pixelOffset, "Pixel Offset", 1.0,
        -1000.0, 1000.0, -5.0, 5.0, "Per channel offset in pixels (relative to frame height)."));
    attach(page, gAb, defBool(desc, pn::aberrationGlow, "Enable Glow Aberration", false,
        "Blur each channel with a different kernel size."));
    attach(page, gAb, defDouble(desc, pn::multR, "Multiply Red", 80.0, 0.0, 200.0, 50.0, 150.0,
        "Red blur radius multiplier in percent."));
    attach(page, gAb, defDouble(desc, pn::multG, "Multiply Green", 100.0, 0.0, 200.0, 50.0, 150.0,
        "Green blur radius multiplier in percent."));
    attach(page, gAb, defDouble(desc, pn::multB, "Multiply Blue", 120.0, 0.0, 200.0, 50.0, 150.0,
        "Blue blur radius multiplier in percent."));

    // ---- Tint ------------------------------------------------------------
    attach(page, gTint, defChoice(desc, pn::tintMode, "Tint Mode", 0,
        {"Off", "Monocolor", "Duocolor"}, "Tint the glow with one or two colours."));
    attach(page, gTint, defChoice(desc, pn::tintBlend, "Tint Blend Mode", 0,
        {"Multiply", "Overlay", "Soft Light"}, "How the tint colour is combined."));
    attach(page, gTint, defRGBA(desc, pn::tintColor, "Color", 1.0, 1.0, 1.0, 1.0,
        "Inner (Monocolor) tint colour."));
    attach(page, gTint, defRGBA(desc, pn::tintColorOuter, "Color Outer", 0.0, 0.0, 0.0, 1.0,
        "Outer tint colour, used by Duocolor."));
    attach(page, gTint, defDouble(desc, pn::tintStrength, "Tint Strength", 100.0,
        0.0, 200.0, 0.0, 200.0, "Tint mix in percent."));

    // ---- Tone mapping ----------------------------------------------------
    attach(page, gTone, defChoice(desc, pn::tonemapMode, "Operation", 0,
        {"None", "ACES Filmic", "Filmic", "Hable Filmic", "Reinhard", "Reinhard 2",
         "Clamp Chroma"}, "Tone mapping curve applied to the glow only."));
    attach(page, gTone, defDouble(desc, pn::tonemapMix, "Mix", 100.0, 0.0, 100.0, 0.0, 100.0,
        "Dry/wet mix of the tone mapper."));

    // ---- Lens dirt -------------------------------------------------------
    attach(page, gDirt, defBool(desc, pn::dirtEnabled, "Enable Lens Dirt", false,
        "Add the optional Lens Dirt input to the glow. Approximated, see docs."));
    attach(page, gDirt, defDouble(desc, pn::dirtOpacity, "Opacity", 100.0, 0.0, 100.0, 0.0, 100.0,
        "Lens dirt opacity in percent."));
    attach(page, gDirt, defDouble(desc, pn::dirtInherit, "Inherit Glow Color", 0.0,
        0.0, 100.0, 0.0, 100.0, "Blend the dirt colour towards the local glow colour."));
    attach(page, gDirt, defDouble(desc, pn::dirtMatteExposure, "Matte Exposure", 0.0,
        -10.0, 10.0, -2.0, 2.0, "Exposure (in stops) applied to the dirt matte."));
    attach(page, gDirt, defDouble(desc, pn::dirtMatteGamma, "Matte Gamma", 1.0,
        0.1, 5.0, 0.1, 2.0, "Gamma applied to the dirt matte."));

    // ---- Quality ---------------------------------------------------------
    attach(page, gQual, defChoice(desc, pn::qualityPreset, "Quality Preset", 3,
        {"Draft", "Low", "Medium", "Standard", "High", "Extreme", "Custom"},
        "Blur buffer resolution per octave."));
    attach(page, gQual, defDouble(desc, pn::resolution, "Resolution", 75.0, 25.0, 100.0, 25.0, 100.0,
        "Working resolution of the glow in percent."));
    attach(page, gQual, defDouble(desc, pn::stepsMultiplier, "Steps Multiplier", 0.5,
        0.01, 3.0, 0.01, 1.0,
        "Tap density of the gaussian kernels. Only used by the Custom preset."));
    attach(page, gQual, defInt(desc, pn::glowIterations, "Glow Iterations", 8, 6, 10,
        "Unused in Exponential mode: the original always uses 10 octaves here. "
        "Kept for UI parity (it only drives Lens Iris, which is not ported)."));
    attach(page, gQual, defDouble(desc, pn::customWeight, "Custom Weight", 0.9,
        0.1, 1.0, 0.1, 1.0, "Per octave buffer scale used by the Custom preset."));

    // ---- Output ----------------------------------------------------------
    attach(page, gOut, defChoice(desc, pn::view, "View", 0,
        {"Final Result", "Glow Input", "Lens Dirt Input", "Lens Dirt Matte", "Lens Dirt Matted"},
        "Debug views of intermediate stages."));
    attach(page, gOut, defDouble(desc, pn::sourceOpacity, "Source Opacity", 100.0,
        0.0, 100.0, 0.0, 100.0, "Opacity of the underlying source before compositing."));
    attach(page, gOut, defBool(desc, pn::unmult, "Unmult", false,
        "(Required for Alpha) Output straight RGBA with an opaque alpha instead of "
        "the premultiplied source alpha."));

    // ---- Debug -----------------------------------------------------------
    attach(page, gDebug, defChoice(desc, pn::debugView, "Debug View", dg::kDebugViewNone,
        {kDebugChoiceLabels[dg::kDebugViewWidth], kDebugChoiceLabels[dg::kDebugViewHeight],
         kDebugChoiceLabels[dg::kDebugViewNone]},
        "Show a single blur axis on its own."));
    attach(page, gDebug, defBool(desc, pn::preProcSolidFill, "Pre Proc Solid Fill", false,
        "Replace the pre-processed input with white, to inspect the blur kernels."));
}

ImageEffect* DeepGlowFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/) {
    return new DeepGlowEffect(handle);
}

} // namespace

void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray& ids) {
    static DeepGlowFactory factory(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&factory);
}
