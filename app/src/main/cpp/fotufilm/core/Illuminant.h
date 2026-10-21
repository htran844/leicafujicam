// Port of Sources/FotufilmCore/Illuminant.swift.
#ifndef FOTUFILM_CORE_ILLUMINANT_H
#define FOTUFILM_CORE_ILLUMINANT_H

#include <vector>

#include "Simd.h"
#include "WhiteBalance.h"

namespace fotufilm {

/// Reference spectral power distributions on SpectralGrid's 380–780 nm axis.
namespace Illuminant {

/// Index of 560 nm on the grid.
constexpr int anchorIndex = 36;

/// A Planckian SPD normalized to 1 at 560 nm.
std::vector<float> planckian(float kelvin);
/// The CIE daylight series normalized to 1 at 560 nm (input clamped to 4000–25000 K).
std::vector<float> daylight(float kelvin);
/// Planckian through 4000 K, daylight from 5000 K, a smooth blend between.
std::vector<float> atLocus(float kelvin);
/// Smooth, positive approximation to a lamp of the stated chromaticity.
std::vector<float> spectrum(const WhiteBalance& balance);
/// Minimum-relative-entropy deformation of `base` to the chromaticity `xy`.
std::vector<float> matching(Float2 xy, const std::vector<float>& base);

Float2 chromaticity(const std::vector<float>& spectrum);
/// Relative photometric normalization.
float luminance(const std::vector<float>& spectrum);

/// CIE illuminant A, 2856 K Planckian.
const std::vector<float>& a();
/// The published D65 table.
const std::vector<float>& d65();
/// CIE D50 at 5003 K.
const std::vector<float>& d50();
/// Kinoton FP-75 xenon projector measurement (colour-science, BSD-3-Clause).
const std::vector<float>& measuredXenonProjection();
/// Reference cinema screen light: the xenon measurement filtered to the 5400 K daylight white.
const std::vector<float>& xenonProjection();

/// CIE 15 daylight component vectors.
const std::vector<float>& s0();
const std::vector<float>& s1();
const std::vector<float>& s2();

}  // namespace Illuminant
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ILLUMINANT_H
