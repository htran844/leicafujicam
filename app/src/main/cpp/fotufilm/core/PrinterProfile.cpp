// Port of Sources/FotufilmCore/PrinterProfile.swift.
#include "PrinterProfile.h"

#include <cmath>
#include <cstring>

#include "Enlarger.h"
#include "Illuminant.h"
#include "SpectralGrid.h"

namespace fotufilm {

namespace {
uint64_t bits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}
float held(float value, float lower, float upper, float fallback) {
    return std::isfinite(value) ? swiftMin(swiftMax(value, lower), upper) : fallback;
}
}  // namespace

PrinterProfile PrinterProfile::normalized() const {
    return PrinterProfile(held(lampKelvin, lampRangeLower, lampRangeUpper, 3200),
                          held(exposureEV, exposureRangeLower, exposureRangeUpper, 0),
                          held(magenta, filterRangeLower, filterRangeUpper, 0.4f),
                          held(yellow, filterRangeLower, filterRangeUpper, 0.5f));
}

std::vector<float> PrinterProfile::filteredSpectrum() const {
    PrinterProfile p = normalized();
    std::vector<float> lamp = Illuminant::planckian(p.lampKelvin);
    const std::vector<float>& wavelengths = SpectralGrid::wavelengths();
    std::vector<float> result(wavelengths.size());
    for (size_t i = 0; i < wavelengths.size(); ++i) {
        float wavelength = wavelengths[i];
        float m = std::exp(-0.5f * std::pow((wavelength - 550) / 45, 2.0f));
        float y = 1 / (1 + std::exp((wavelength - 500) / 12));
        result[i] = lamp[i] * std::pow(10.0f, -p.magenta * m - p.yellow * y);
    }
    return result;
}

std::optional<PrinterProfile> PrinterProfile::resolved(const std::optional<PrinterProfile>& profile,
                                                       const FilmStock& stock, PrintPaper paper) {
    if (!EnlargerOptics::illuminates(stock, paper) || !profile) return std::nullopt;
    return profile->normalized();
}

uint64_t PrinterProfile::spectralSignature() const {
    PrinterProfile p = normalized();
    uint64_t h = 0x5052494E54455201ULL;
    for (float value : {p.lampKelvin, p.magenta, p.yellow}) h = (h ^ bits(value)) * 0x100000001b3ULL;
    return h;
}

}  // namespace fotufilm
