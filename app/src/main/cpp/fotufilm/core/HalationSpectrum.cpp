// Port of Sources/FotufilmCore/HalationSpectrum.swift.
#include "HalationSpectrum.h"

#include <cmath>

#include "Simd.h"
#include "SpectralControlPoints.h"
#include "SpectralGrid.h"

namespace fotufilm {
namespace HalationSpectrum {

const std::vector<float>& handleNM() {
    static const std::vector<float> values = {400, 450, 500, 550, 600, 650, 700};
    return values;
}

std::vector<float> neutral() { return std::vector<float>(handleNM().size(), neutralStops); }

bool isNeutral(const std::vector<float>& handles) {
    if (handles.size() != handleNM().size()) return true;
    for (float h : handles) {
        if (!(std::fabs(h - neutralStops) < 1e-4f)) return false;
    }
    return true;
}

std::vector<float> resampled(const std::vector<float>& handles) {
    if (handles.size() != handleNM().size() || isNeutral(handles)) return {};
    const float floor = travelStopsLower;
    std::vector<SpectralControlPoint> points;
    for (size_t i = 0; i < handles.size(); ++i) {
        points.push_back(SpectralControlPoint{handleNM()[i],
                                              swiftMin(swiftMax(handles[i], floor), travelStopsUpper) - floor});
    }
    std::vector<float> result;
    for (float v : SpectralCurve::resampled(points)) result.push_back(std::exp2(v + floor));
    return result;
}

std::vector<float> recordGain(const std::vector<float>& spectrum, const std::vector<std::vector<float>>& sensitivity,
                              const std::vector<float>& illuminant) {
    std::vector<float> rest(sensitivity.size(), 1.0f);
    if (static_cast<int>(spectrum.size()) != SpectralGrid::count ||
        static_cast<int>(illuminant.size()) != SpectralGrid::count) {
        return rest;
    }
    std::vector<float> result;
    for (const std::vector<float>& record : sensitivity) {
        if (static_cast<int>(record.size()) != SpectralGrid::count) {
            result.push_back(1);
            continue;
        }
        float weight = 0, weighted = 0;
        for (int band = 0; band < SpectralGrid::count; ++band) {
            float w = swiftMax(illuminant[band], 0.0f) * swiftMax(record[band], 0.0f);
            weight += w;
            weighted += w * swiftMax(spectrum[band], 0.0f);
        }
        result.push_back(weight > 0 ? weighted / weight : 1.0f);
    }
    return result;
}

std::vector<float> recordGain(const std::vector<float>& spectrum, const std::vector<std::vector<float>>& sensitivity) {
    return recordGain(spectrum, sensitivity, SpectralGrid::d65());
}

}  // namespace HalationSpectrum
}  // namespace fotufilm
