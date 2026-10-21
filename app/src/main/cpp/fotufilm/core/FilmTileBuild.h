// Port of Sources/FotufilmHalide/Pipeline/FilmTileBuild.h and FotufilmFilmTileBuild.cpp
// (`fotufilm_film_tile_build`): the Halide pipeline written as plain C++ loops. Every output
// element is computed by the same expression in the same order whatever thread computes it, so
// the result is the serial one bit for bit.
#ifndef FOTUFILM_CORE_FILM_TILE_BUILD_H
#define FOTUFILM_CORE_FILM_TILE_BUILD_H

#include <cstdint>
#include <vector>

namespace fotufilm {
namespace FilmTileBuild {

/// Sublayers one build lays: `CrystalGrainModel.binCount`.
constexpr int sublayers = 4;
/// Crystals a film cell holds at most: `FilmRandom.maxCount`.
constexpr int maxCount = 48;
/// Film cells a sample's footprint can meet along one axis.
constexpr int cellWindow = 4;
/// Most film cells along a tile side.
constexpr int maxCells = 1024;
/// Per sublayer: cells per tile side, peak demand over capacity, capacity, then the count
/// thresholds `FilmRandom.countThresholds` gives.
constexpr int fields = 3 + maxCount;

/// A normalised Gaussian of `sigma` samples, 3.5 sigma either side: `film_gaussian_kernel`.
std::vector<float> gaussianKernel(float sigma);
/// `film_term_factor`.
int termFactor(float sigma, int samples);

/// One dye-cloud record's periodic tile, as the light each texel passes at `levels` developed
/// densities: `light` holds texels² floats per level, level-major. `sublayerFields` holds
/// `sublayers × fields` floats (an absent sublayer has thresholds of 1); `fractions` holds
/// `levels` developed fractions per sublayer, sublayer-major; `terms` holds each cloud term's
/// sigma in samples, then its weight. Returns 0, or -1 when the arguments are out of range.
int32_t build(int32_t texels, int32_t supersample, int32_t markShape, const float* terms, int32_t termCount,
              const float* sublayerFields, const float* fractions, int32_t levels, uint32_t seed, int32_t record,
              float* light);

}  // namespace FilmTileBuild
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_TILE_BUILD_H
