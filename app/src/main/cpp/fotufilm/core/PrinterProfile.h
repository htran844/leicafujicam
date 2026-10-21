// Port of Sources/FotufilmCore/PrinterProfile.swift. Matching (PrinterMatching.swift) is in
// PrinterMatching.h.
#ifndef FOTUFILM_CORE_PRINTER_PROFILE_H
#define FOTUFILM_CORE_PRINTER_PROFILE_H

#include <cstdint>
#include <optional>
#include <vector>

#include "PrintPaper.h"

namespace fotufilm {

struct FilmStock;

/// An illustrative optical colour enlarger, not a measured hardware calibration. Exposure is
/// relative to the fixed 3200 K, M 0.40, Y 0.50 reference setup.
struct PrinterProfile {
    float lampKelvin = 3200;
    float exposureEV = 0;
    /// Synthetic optical-density strengths, not manufacturer filter-dial units.
    float magenta = 0.4f;
    float yellow = 0.5f;

    PrinterProfile() = default;
    PrinterProfile(float lampKelvin_, float exposureEV_, float magenta_, float yellow_)
        : lampKelvin(lampKelvin_), exposureEV(exposureEV_), magenta(magenta_), yellow(yellow_) {}

    static PrinterProfile simulatedTungsten() { return PrinterProfile(); }
    static constexpr float lampRangeLower = 2800, lampRangeUpper = 3600;
    static constexpr float exposureRangeLower = -6, exposureRangeUpper = 6;
    static constexpr float filterRangeLower = 0, filterRangeUpper = 1.2f;

    /// Invalid values use the reference setup; finite values stay within the profile's domain.
    PrinterProfile normalized() const;
    /// Relative radiant spectrum on the grid; the bare lamp is normalized at 560 nm.
    std::vector<float> filteredSpectrum() const;

    static std::optional<PrinterProfile> resolved(const std::optional<PrinterProfile>& profile, const FilmStock& stock,
                                                  PrintPaper paper);

    /// Only spectral controls identify the printing LUT. Exposure changes configuration alone.
    uint64_t spectralSignature() const;

    bool operator==(const PrinterProfile& o) const {
        return lampKelvin == o.lampKelvin && exposureEV == o.exposureEV && magenta == o.magenta && yellow == o.yellow;
    }
    bool operator!=(const PrinterProfile& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINTER_PROFILE_H
