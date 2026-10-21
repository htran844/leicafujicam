// Port of Sources/FotufilmCore/SampledCharacteristicCurve.swift.
#ifndef FOTUFILM_CORE_SAMPLED_CHARACTERISTIC_CURVE_H
#define FOTUFILM_CORE_SAMPLED_CHARACTERISTIC_CURVE_H

#include <string>
#include <vector>

#include "FotufilmConfigLayout.h"
#include "Json.h"

namespace fotufilm {

/// A runtime density record with C1 Hermite interpolation through every sample. Local extrema are
/// retained; endpoint tangents are zero, joining constant extrapolation continuously.
struct SampledCharacteristicCurve {
    static constexpr int maximumSamples = FOTUFILM_SAMPLED_CURVE_MAX_SAMPLES;

    std::vector<float> logExposure;
    std::vector<float> density;
    std::vector<float> slopes;

    /// Validating initialiser; throws DecodingError for a record outside 2...1024 finite pairs
    /// with strictly increasing exposure, or with non-finite tangents.
    static SampledCharacteristicCurve make(std::vector<float> logExposure, std::vector<float> density);

    /// Decodes `{logExposure, density}`.
    static SampledCharacteristicCurve decode(const JsonValue& value, const std::string& path);

    /// Translates an already validated record for speed loss and base fog.
    SampledCharacteristicCurve shifted(float logExposureShift, float fog = 0) const;
    /// The record on a shifted exposure axis: it forms at x the density it formed at x + offset.
    SampledCharacteristicCurve reexposed(float offset) const;

    float value(float x) const;

    static float hermite(float x, float x0, float x1, float y0, float y1, float m0, float m1);

private:
    SampledCharacteristicCurve(std::vector<float> logExposure_, std::vector<float> density_,
                               std::vector<float> slopes_)
        : logExposure(std::move(logExposure_)), density(std::move(density_)), slopes(std::move(slopes_)) {}
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SAMPLED_CHARACTERISTIC_CURVE_H
