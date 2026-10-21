// Port of Sources/FotufilmCore/CouplerGeometry.swift.
#ifndef FOTUFILM_CORE_COUPLER_GEOMETRY_H
#define FOTUFILM_CORE_COUPLER_GEOMETRY_H

#include <optional>
#include <string>
#include <vector>

#include "Json.h"

namespace fotufilm {

/// Derives a DIR inhibition matrix from per-layer release and per-interlayer transmission.
struct CouplerGeometry {
    /// Centre depth of each sensitive layer below the emulsion surface, in micrometres, R, G, B.
    std::vector<float> layerDepthUM;
    /// Fraction of a donor's inhibitor that survives one coated interlayer, one entry per gap.
    std::vector<float> interlayerTransmission;
    /// Inhibitor released per unit of development, per donor layer, R, G, B.
    std::vector<float> release;
    /// Fraction of a layer's own released inhibitor that still acts on it as a separate effect.
    float selfRetention;

    /// The stack a colour negative is coated in, in micrometres of depth.
    static const std::vector<float>& colorNegativeDepthUM();
    /// The self-retention fitted across the shipped packs.
    static constexpr float houseSelfRetention = 0.091f;

    CouplerGeometry(std::vector<float> layerDepthUM, std::vector<float> interlayerTransmission,
                    std::vector<float> release, float selfRetention = houseSelfRetention);
    /// `CouplerGeometry(interlayerTransmission:release:)` on the colour-negative stack.
    static CouplerGeometry onColorNegativeStack(std::vector<float> interlayerTransmission,
                                                std::vector<float> release,
                                                float selfRetention = houseSelfRetention);

    /// The per-gap transmissions an exponential decay of `rangeUM` through the stack implies.
    static std::vector<float> transmission(float rangeUM, const std::vector<float>& layerDepthUM);
    static std::vector<float> transmission(float rangeUM) { return transmission(rangeUM, colorNegativeDepthUM()); }

    /// The inhibition matrix this geometry implies, indexed [receiver][donor].
    std::vector<std::vector<float>> matrix(const std::optional<std::vector<float>>& gapReachScales = std::nullopt,
                                           float selfScale = 1) const;

    /// Whether this geometry produces any inhibition at all.
    bool isActive() const;

    /// Decodes a pack's `couplerGeometry` object, rejecting malformed layer counts.
    static CouplerGeometry decode(const JsonValue& value, const std::string& path);

    /// The geometry whose `matrix()` comes closest to an authored matrix.
    static CouplerGeometry fit(const std::vector<std::vector<float>>& matrix,
                               const std::vector<float>& layerDepthUM);
    static CouplerGeometry fit(const std::vector<std::vector<float>>& matrix) {
        return fit(matrix, colorNegativeDepthUM());
    }

    /// RMS disagreement between this geometry and an authored matrix.
    float residual(const std::vector<std::vector<float>>& matrix) const;

    /// Per-layer release strengths solved from a stock's measured inter-image effect.
    static std::optional<std::vector<float>> releaseSolvedFromInterImage(
        const std::vector<float>& ratios, const std::vector<float>& activationSlopes,
        const std::vector<float>& interlayerTransmission, float selfRetention = houseSelfRetention,
        const std::vector<float>& layerDepthUM = colorNegativeDepthUM());

    bool operator==(const CouplerGeometry& o) const {
        return layerDepthUM == o.layerDepthUM && interlayerTransmission == o.interlayerTransmission &&
               release == o.release && selfRetention == o.selfRetention;
    }
    bool operator!=(const CouplerGeometry& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_COUPLER_GEOMETRY_H
