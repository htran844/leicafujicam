// Port of Sources/FotufilmCore/SampledCharacteristicCurve.swift.
#include "SampledCharacteristicCurve.h"

#include <cmath>

#include "Simd.h"

namespace fotufilm {

SampledCharacteristicCurve SampledCharacteristicCurve::make(std::vector<float> logExposure,
                                                            std::vector<float> density) {
    bool valid = logExposure.size() >= 2 && logExposure.size() <= static_cast<size_t>(maximumSamples) &&
                 density.size() == logExposure.size();
    if (valid) {
        for (float v : logExposure) valid = valid && std::isfinite(v);
        for (float v : density) valid = valid && std::isfinite(v);
        for (size_t i = 0; valid && i + 1 < logExposure.size(); ++i) valid = logExposure[i] < logExposure[i + 1];
    }
    if (!valid) {
        throw DecodingError(
            "dataCorrupted: Sampled curves require 2...1024 finite density/exposure pairs with strictly "
            "increasing exposure.");
    }
    std::vector<float> slopes(density.size(), 0.0f);
    for (size_t i = 1; i + 1 < density.size(); ++i) {
        double h0 = static_cast<double>(logExposure[i]) - static_cast<double>(logExposure[i - 1]);
        double h1 = static_cast<double>(logExposure[i + 1]) - static_cast<double>(logExposure[i]);
        double d0 = (static_cast<double>(density[i]) - static_cast<double>(density[i - 1])) / h0;
        double d1 = (static_cast<double>(density[i + 1]) - static_cast<double>(density[i])) / h1;
        if (d0 * d1 > 0) {
            double w0 = 2 * h1 + h0, w1 = h1 + 2 * h0;
            slopes[i] = static_cast<float>((w0 + w1) / (w0 / d0 + w1 / d1));
        }
    }
    for (float s : slopes) {
        if (!std::isfinite(s)) throw DecodingError("dataCorrupted: Sampled curve tangents must be finite.");
    }
    return SampledCharacteristicCurve(std::move(logExposure), std::move(density), std::move(slopes));
}

SampledCharacteristicCurve SampledCharacteristicCurve::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& o = json::object(value, path);
    std::vector<float> logExposure = json::requiredFloatArray(o, "logExposure", path);
    std::vector<float> density = json::requiredFloatArray(o, "density", path);
    return make(std::move(logExposure), std::move(density));
}

SampledCharacteristicCurve SampledCharacteristicCurve::shifted(float shift, float fog) const {
    std::vector<float> le(logExposure.size()), d(density.size());
    for (size_t i = 0; i < logExposure.size(); ++i) le[i] = logExposure[i] + shift;
    for (size_t i = 0; i < density.size(); ++i) d[i] = density[i] + fog;
    return SampledCharacteristicCurve(std::move(le), std::move(d), slopes);
}

SampledCharacteristicCurve SampledCharacteristicCurve::reexposed(float offset) const {
    std::vector<float> le(logExposure.size());
    for (size_t i = 0; i < logExposure.size(); ++i) le[i] = logExposure[i] - offset;
    return SampledCharacteristicCurve(std::move(le), density, slopes);
}

float SampledCharacteristicCurve::value(float x) const {
    if (x <= logExposure[0]) return density[0];
    if (x >= logExposure[logExposure.size() - 1]) return density[density.size() - 1];
    size_t low = 0, high = logExposure.size() - 1;
    while (high - low > 1) {
        size_t mid = (low + high) / 2;
        if (logExposure[mid] <= x) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return hermite(x, logExposure[low], logExposure[high], density[low], density[high], slopes[low], slopes[high]);
}

float SampledCharacteristicCurve::hermite(float x, float x0, float x1, float y0, float y1, float m0, float m1) {
    float h = x1 - x0;
    float t = swiftMin(swiftMax((x - x0) / h, 0.0f), 1.0f);
    float delta = y1 - y0;
    float a = h * m0, b = h * m1;
    return y0 + t * (a + t * (3 * delta - 2 * a - b + t * (-2 * delta + a + b)));
}

}  // namespace fotufilm
