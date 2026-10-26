// Deep Glow 2 — OFX port
// DGPipeline.h : the recovered render pipeline (CPU implementation).
//
//   source ─► resample ─► preProcess ─► ┌ per octave: resample → blur H → blur V ┐
//                                        └ additively upsample the pyramid ───────┘
//            ─► postProcess ─► [lens dirt] ─► screen/add composite with source
//
// See docs/04-pipeline.md for the mapping between this code and the original
// OpenGL pass sequence.
#pragma once

#include <functional>

#include "DGColor.h"
#include "DGImage.h"
#include "DGParams.h"

namespace dg {

struct RenderResult {
    ImageF image;          // final RGBA (straight or premultiplied, see Params::unmult)
    int passes = 0;
    int workW = 0;
    int workH = 0;
};

// Render the effect.  `mask` and `dirt` may be null; they must be the same
// size as `source` when supplied.
RenderResult renderGlow(const ImageF& source,
                        const Params& p,
                        const ImageF* mask = nullptr,
                        const ImageF* dirt = nullptr,
                        const std::function<bool()>& abortFn = nullptr);

} // namespace dg
