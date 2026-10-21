// Port of Sources/FotufilmCore/FilmStockDefaults.swift.
#ifndef FOTUFILM_CORE_FILM_STOCK_DEFAULTS_H
#define FOTUFILM_CORE_FILM_STOCK_DEFAULTS_H

#include <vector>

namespace fotufilm {

struct GrainDensityProfile;

namespace FilmStockDefaults {
/// Fitted per record against the four published Vision3 granularity-against-density curves.
const std::vector<std::vector<float>>& grainDensityRecords();
const GrainDensityProfile& grainDensityProfile();
}  // namespace FilmStockDefaults

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_STOCK_DEFAULTS_H
