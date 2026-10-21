// Port of Sources/FotufilmCore/LabScanFinish.swift.
#ifndef FOTUFILM_CORE_LAB_SCAN_FINISH_H
#define FOTUFILM_CORE_LAB_SCAN_FINISH_H

#include <vector>

#include "Simd.h"

namespace fotufilm {

/// Lab Scan's finish: the colour and tone a minilab scanner's processing gives a frame after it
/// has set it up. Baked into the medium's output table.
namespace LabScanFinish {

constexpr float highlightLift = 0.006f;
constexpr float blackPull = 0.019f;
constexpr Float2 shadowCast{-0.045f, -0.033f};
constexpr float castSaturation = 0.1f;

/// A chroma gain and a hue turn in degrees over a Gaussian window of `width` degrees about `hue`.
struct HueCorrection {
    float hue;
    float width;
    float chroma;
    float turn;
};
const std::vector<HueCorrection>& corrections();

constexpr float correctionFadeLower = 0.08f;
constexpr float correctionFadeUpper = 0.20f;

/// The finished colour of a scan pixel, linear Display P3 in and out.
Float3 apply(Float3 colour, float strength = 1, bool chromatic = true, float floor = 0);
/// The gradation alone: luminance re-placed on a steeper print-like curve, hue untouched.
Float3 graded(Float3 colour, float strength = 1);
Float3 oklab(Float3 rgb);
Float3 linearFromOklab(Float3 lab);

}  // namespace LabScanFinish
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_LAB_SCAN_FINISH_H
