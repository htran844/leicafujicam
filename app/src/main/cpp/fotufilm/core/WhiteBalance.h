// Port of Sources/FotufilmCore/WhiteBalance.swift.
#ifndef FOTUFILM_CORE_WHITE_BALANCE_H
#define FOTUFILM_CORE_WHITE_BALANCE_H

#include "Simd.h"

namespace fotufilm {

/// Camera white balance, as a correlated colour temperature and a tint away from the
/// blackbody/daylight locus.
struct WhiteBalance {
    /// D65, the renderer's working white.
    static constexpr float neutralKelvin = 6504;

    /// Correlated colour temperature of the scene illuminant, in kelvin.
    float kelvin = neutralKelvin;
    /// Displacement perpendicular to the hybrid locus in CIE 1960 uv, x10000.
    float tint = 0;

    WhiteBalance() = default;
    WhiteBalance(float kelvin_, float tint_ = 0) : kelvin(kelvin_), tint(tint_) {}

    static WhiteBalance neutral() { return WhiteBalance(neutralKelvin, 0); }

    /// The range the UI offers.
    static constexpr float kelvinRangeLower = 2000;
    static constexpr float kelvinRangeUpper = 12000;
    static constexpr float tintRangeLower = -100;
    static constexpr float tintRangeUpper = 100;

    bool isNeutral() const { return kelvin == neutralKelvin && tint == 0; }

    /// Micro-reciprocal degrees, 10^6 / K.
    static float kelvinToMired(float kelvin);
    static float miredToKelvin(float mired);

    float mired() const { return kelvinToMired(kelvin); }
    void setMired(float newValue) { kelvin = miredToKelvin(newValue); }

    /// `kelvinRange` seen from the slider's end: low mired is cool light, high mired is warm.
    static float miredRangeLower() { return kelvinToMired(kelvinRangeUpper); }
    static float miredRangeUpper() { return kelvinToMired(kelvinRangeLower); }

    /// Per-channel gains on scene-linear working-space RGB carrying this illuminant to D65.
    Float3 gains() const;

    /// CIE 1931 xy of the illuminant named by a temperature and a tint.
    static Float2 chromaticity(float kelvin, float tint);

    static Float2 boundedChromaticity(Float2 uv, Float2 displacement);
    /// The chromaticity of the same spectrum used for film exposure.
    static Float2 locusXY(float kelvin);
    /// CIE 15 daylight locus, valid 4000-25000 K.
    static Float2 daylightXY(float kelvin);
    /// Planckian locus by direct integration against the CIE 1931 observer on the 5 nm grid.
    static Float2 planckianXY(float kelvin);
    /// CIE 1960 uv.
    static Float2 uvFromXY(Float2 xy);
    static Float2 xyFromUV(Float2 uv);
    /// Working-space linear RGB of a chromaticity at unit luminance.
    static Float3 workingRGB(Float2 xy);

    bool operator==(const WhiteBalance& other) const { return kelvin == other.kelvin && tint == other.tint; }
    bool operator!=(const WhiteBalance& other) const { return !(*this == other); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_WHITE_BALANCE_H
