// Port of Sources/FotufilmCore/HalationSpectrum.swift.
#ifndef FOTUFILM_CORE_HALATION_SPECTRUM_H
#define FOTUFILM_CORE_HALATION_SPECTRUM_H

#include <vector>

namespace fotufilm {

/// Wavelength-dependent gain applied to the stock's halation return spectrum.
namespace HalationSpectrum {
/// Editor handle wavelengths at 50 nm intervals.
const std::vector<float>& handleNM();
/// Where a handle rests: no stops added.
constexpr float neutralStops = 0;
/// Handle range in stops.
constexpr float travelStopsLower = -6;
constexpr float travelStopsUpper = 6;
/// Every handle at rest.
std::vector<float> neutral();
/// Whether these handles say nothing the film does not already say.
bool isNeutral(const std::vector<float>& handles);
/// Handles resampled onto the spectral grid as linear gain, or empty for a neutral curve.
std::vector<float> resampled(const std::vector<float>& handles);
/// `sum(illuminant × sensitivity × gain) / sum(illuminant × sensitivity)` per record; invalid or
/// zero-weight input returns 1. `illuminant` empty means D65.
std::vector<float> recordGain(const std::vector<float>& spectrum, const std::vector<std::vector<float>>& sensitivity,
                              const std::vector<float>& illuminant);
std::vector<float> recordGain(const std::vector<float>& spectrum, const std::vector<std::vector<float>>& sensitivity);
}  // namespace HalationSpectrum

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HALATION_SPECTRUM_H
