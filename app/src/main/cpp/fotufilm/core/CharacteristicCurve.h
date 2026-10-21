// Port of Sources/FotufilmCore/CharacteristicCurve.swift.
#ifndef FOTUFILM_CORE_CHARACTERISTIC_CURVE_H
#define FOTUFILM_CORE_CHARACTERISTIC_CURVE_H

#include <optional>

#include "SampledCharacteristicCurve.h"

namespace fotufilm {

/// One additional emulsion population contributing density to a characteristic curve.
struct CharacteristicCurveComponent {
    float gamma;
    float toe;
    float toeWidth;
    float shoulder;
    float shoulderWidth;

    CharacteristicCurveComponent(float gamma, float toe, float toeWidth, float shoulder, float shoulderWidth);

    float densityRange() const { return gamma * (shoulder - toe); }
    /// The population on a shifted exposure axis, as `CharacteristicCurve::reexposed`.
    CharacteristicCurveComponent reexposed(float offset) const;
    float density(float logExposure) const;
};

/// Hurter–Driffield characteristic curve: dye density as a function of log10 exposure.
struct CharacteristicCurve {
    /// Minimum density: film base + fog (+ mask for color negative layers).
    float dMin;
    /// Straight-line slope.
    float gamma;
    /// Log-exposure position of the toe (relative to mid-gray at 0).
    float toe;
    /// Softness of the toe transition in log-exposure units.
    float toeWidth;
    /// Log-exposure position of the shoulder.
    float shoulder;
    /// Softness of the shoulder transition.
    float shoulderWidth;
    /// A second coated speed group; nullopt is the single-population curve, bit-identically.
    std::optional<CharacteristicCurveComponent> secondary;
    /// Optional point-based formation record. Takes precedence over the analytic fit.
    std::optional<SampledCharacteristicCurve> sampled;

    CharacteristicCurve(float dMin, float gamma, float toe, float toeWidth, float shoulder, float shoulderWidth,
                        std::optional<CharacteristicCurveComponent> secondary = std::nullopt,
                        std::optional<SampledCharacteristicCurve> sampled = std::nullopt);

    /// Maximum achievable density.
    float dMax() const;
    float density(float logExposure) const;
    /// The curve on a shifted exposure axis: it forms at x the density it formed at x + offset.
    CharacteristicCurve reexposed(float offset) const;
    /// Finds an exposure for the requested density via bisection.
    float logExposure(float density) const;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_CHARACTERISTIC_CURVE_H
