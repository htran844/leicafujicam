// Port of Sources/FotufilmCore/Reciprocity.swift. `FilmStock::reciprocity` is declared in
// FilmStock.h and defined in Reciprocity.cpp.
#ifndef FOTUFILM_CORE_RECIPROCITY_H
#define FOTUFILM_CORE_RECIPROCITY_H

#include <optional>
#include <string>
#include <vector>

#include "Json.h"

namespace fotufilm {

/// Where a particular emulsion leaves the reciprocity law, as its datasheet says.
struct ReciprocityFailure {
    /// Exposures at or under this many seconds are inside the law.
    float thresholdSeconds;
    /// Stops lost per decade past the threshold.
    float lostStopsPerDecade;
    /// Last exposure covered by the source data; the correction holds beyond it.
    std::optional<float> statedThroughSeconds;
    /// Per-record rate multipliers in R, G, B order with mean 1.
    std::optional<std::vector<float>> layerWeights;

    static ReciprocityFailure decode(const JsonValue& value, const std::string& path);

    bool operator==(const ReciprocityFailure& o) const {
        return thresholdSeconds == o.thresholdSeconds && lostStopsPerDecade == o.lostStopsPerDecade &&
               statedThroughSeconds == o.statedThroughSeconds && layerWeights == o.layerWeights;
    }
    bool operator!=(const ReciprocityFailure& o) const { return !(*this == o); }
};

/// Relative failure rate of the R, G, B records for a stated colour table without its own weights.
const std::vector<float>& reciprocityLayerWeights();

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_RECIPROCITY_H
