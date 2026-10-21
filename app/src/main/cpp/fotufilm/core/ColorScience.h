// Port of Sources/FotufilmCore/ColorScience.swift.
#ifndef FOTUFILM_CORE_COLOR_SCIENCE_H
#define FOTUFILM_CORE_COLOR_SCIENCE_H

#include "Simd.h"

namespace fotufilm {

namespace ColorScience {

/// Linear-light sRGB into the working space (linear Rec.2020).
Float3 linearSRGBToRec2020(Float3 rgb);
/// Linear-light sRGB to linear Display P3.
Float3 linearSRGBToDisplayP3(Float3 rgb);
/// Linear Display P3 to linear-light sRGB.
Float3 linearDisplayP3ToSRGB(Float3 rgb);
/// Linear Display P3 into the working space.
Float3 linearDisplayP3ToRec2020(Float3 rgb);
/// Working space to linear Display P3.
Float3 linearRec2020ToDisplayP3(Float3 rgb);
/// Linear Rec.2020 into the exposure table's AP0-about-D65 basis. Must match
/// `kRec2020ToExposureDomain` in the GPU stages digit for digit.
Float3 linearRec2020ToExposureDomain(Float3 rgb);
Float3 linearExposureDomainToRec2020(Float3 rgb);

/// CIE Y of the exposure-domain components.
constexpr Float3 exposureDomainLuminanceWeights{0.343172898f, 0.734696400f, -0.077869298f};

float srgbToLinear(float v);
float linearToSrgb(float v);

/// The sRGB transfer's slope where it reaches white.
constexpr float srgbSlopeAtWhite = 1.055f / 2.4f;

/// The grading-space encode and its exact inverse, continued linearly outside 0...1.
float gradingEncode(float linear);
float gradingDecode(float coded);

/// The SDR delivery shoulder.
float displayShoulder(float x, float knee);
/// The same shoulder against an arbitrary ceiling.
float highlightShoulder(float x, float ceiling, float knee = 0.9f);

/// CIE Y weights for linear Rec.2020. Must match `kLuma{R,G,B}` in the GPU stages.
constexpr Float3 luminanceWeights{0.2627002f, 0.6779981f, 0.0593017f};
/// CIE Y weights for linear Display P3.
constexpr Float3 displayP3LuminanceWeights{0.2289746f, 0.6917385f, 0.0792869f};

}  // namespace ColorScience

/// Material-aware SDR delivery.
namespace FilmSDRDelivery {
constexpr float boundedShoulderKnee = 1;
constexpr float transparencyShoulderKnee = 0.7f;
inline float shoulderKnee(bool carriesHeadroom) {
    return carriesHeadroom ? transparencyShoulderKnee : boundedShoulderKnee;
}
}  // namespace FilmSDRDelivery

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_COLOR_SCIENCE_H
