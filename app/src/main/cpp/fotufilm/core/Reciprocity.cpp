// Port of Sources/FotufilmCore/Reciprocity.swift.
#include "Reciprocity.h"

#include <cmath>

#include "FilmStock.h"
#include "Simd.h"

namespace fotufilm {

ReciprocityFailure ReciprocityFailure::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& o = json::object(value, path);
    ReciprocityFailure failure;
    failure.thresholdSeconds = json::requiredFloat(o, "thresholdSeconds", path);
    failure.lostStopsPerDecade = json::requiredFloat(o, "lostStopsPerDecade", path);
    failure.statedThroughSeconds = json::optionalFloat(o, "statedThroughSeconds", path);
    failure.layerWeights = json::optionalFloatArray(o, "layerWeights", path);
    return failure;
}

const std::vector<float>& reciprocityLayerWeights() {
    static const std::vector<float> weights = {1.2f, 0.8f, 1.0f};
    return weights;
}

FilmStock FilmStock::reciprocity(float seconds) const {
    if (!reciprocityFailure) return *this;
    const ReciprocityFailure& stated = *reciprocityFailure;
    if (!(seconds > stated.thresholdSeconds && stated.lostStopsPerDecade > 0)) return *this;
    FilmStock met = *this;
    // Past the last stated column the correction holds its final value.
    float covered = swiftMin(seconds, stated.statedThroughSeconds ? *stated.statedThroughSeconds : seconds);
    float decades = std::log10(covered / stated.thresholdSeconds);
    float perStop = static_cast<float>(std::log10(2.0));
    for (int layer = 0; layer < 3; ++layer) {
        float weight = stated.layerWeights ? (*stated.layerWeights)[layer]
                                           : (isMonochrome ? 1.0f : reciprocityLayerWeights()[layer]);
        float lost = stated.lostStopsPerDecade * decades * weight * perStop;
        met.curves[layer].toe += lost;
        met.curves[layer].shoulder += lost;
        met.curves[layer].sampled.reset();
        if (curves[layer].sampled) met.curves[layer].sampled = curves[layer].sampled->shifted(lost);
    }
    return met;
}

}  // namespace fotufilm
