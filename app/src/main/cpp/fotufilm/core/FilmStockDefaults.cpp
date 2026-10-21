// Port of Sources/FotufilmCore/FilmStockDefaults.swift.
#include "FilmStockDefaults.h"

#include "FilmStock.h"

namespace fotufilm {
namespace FilmStockDefaults {

const std::vector<std::vector<float>>& grainDensityRecords() {
    static const std::vector<std::vector<float>> records = {
        {20.0f, 0.3373f, 0.2629f, 0.8187f, 1.1811f, 0.3212f},
        {20.0f, 0.3583f, 0.3183f, 1.6514f, 1.4856f, 0.3631f},
        {20.0f, 0.1069f, 0.5457f, 4.2644f, 1.38f, 0.3446f},
    };
    return records;
}

const GrainDensityProfile& grainDensityProfile() {
    static const GrainDensityProfile profile = GrainDensityProfile::fromRecords(grainDensityRecords());
    return profile;
}

}  // namespace FilmStockDefaults
}  // namespace fotufilm
