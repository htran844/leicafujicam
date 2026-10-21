// Port of Sources/FotufilmCore/ExpiredFilm.swift: room-temperature ageing as speed loss, base fog
// and increased granularity. `FilmStock::expired` is declared in FilmStock.h and defined in
// ExpiredFilm.cpp.
#ifndef FOTUFILM_CORE_EXPIRED_FILM_H
#define FOTUFILM_CORE_EXPIRED_FILM_H

#include <vector>

namespace fotufilm {
namespace ExpiredFilm {

/// The lab rule: one stop per decade at room temperature.
constexpr float speedLossStopsPerDecade = 1;
/// How each layer weathers, R, G, B: blue fastest, red slowest.
const std::vector<float>& layerWeights();
/// Fog density a layer gains per stop of speed it loses.
constexpr float fogPerStopLost = 0.08f;
/// Additional grain amplitude per unit of mean fog.
constexpr float grainPerFog = 1;

}  // namespace ExpiredFilm
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_EXPIRED_FILM_H
