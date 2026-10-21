// Port of Sources/FotufilmCore/AutoAdjustment.swift.
#ifndef FOTUFILM_CORE_AUTO_ADJUSTMENT_H
#define FOTUFILM_CORE_AUTO_ADJUSTMENT_H

#include <cmath>
#include <optional>
#include <vector>

#include "PrintPaper.h"

namespace fotufilm {

struct FilmStock;

/// One-tap development settings: exposure and tone recovery solved so the scene lands inside the
/// film's own latitude.
namespace AutoAdjustment {

struct Solution {
    /// Exposure compensation, in stops; bounded to +/- 3.
    float exposureEV;
    /// Highlight recovery, -1...0.
    float highlights;
    /// Shadow recovery, 0...1.
    float shadows;

    static Solution neutral() { return Solution{0, 0, 0}; }
    bool operator==(const Solution& o) const {
        return exposureEV == o.exposureEV && highlights == o.highlights && shadows == o.shadows;
    }
};

/// Scene stops that survive to the print with tonal separation, relative to metered mid-grey.
struct Latitude {
    float shadows;
    float highlights;
};

Latitude latitude(const FilmStock& stock, float printCorrection = 0, PrintPaper paper = kDefaultPrintPaper,
                  float callier = 1);

/// Fraction of the mid-grey gradient below which a tone no longer separates from its neighbours.
constexpr float kSlopeFloor = 0.2f;

/// The three order statistics the solve reads a scene through, taken once.
struct SceneStops {
    float median;
    float bright;
    float dark;

    SceneStops(float median_, float bright_, float dark_) : median(median_), bright(bright_), dark(dark_) {}
    /// Nil for an empty scene.
    static std::optional<SceneStops> fromRegionStops(const std::vector<float>& regionStops);
    /// For a caller that has already sorted them.
    static std::optional<SceneStops> fromSortedRegionStops(const std::vector<float>& sorted);

    bool operator==(const SceneStops& o) const {
        return median == o.median && bright == o.bright && dark == o.dark;
    }
};

/// Solves exposure, highlights and shadows for a scene described by its regional log-luminances.
Solution solve(const std::vector<float>& regionStops, const FilmStock& stock, float printCorrection = 0);
/// The same solve, for a caller that already has the scene's statistics and the film's window.
Solution solve(const SceneStops& scene, Latitude window);

/// How completely the recovery shift applies at `stops` from mid-grey.
float toneMask(float stops);

/// Where an SDR source's diffuse white sits, in stops above metered mid-grey: -log2(0.18).
float kDiffuseWhiteStops();

/// The highlight recovery that fits a source's declared headroom into the film's window.
float headroomHighlights(float contentHeadroom, Latitude window);

/// Where the scene's median region is metered to, in stops from mid-grey.
constexpr float kMedianTarget = -2.0f / 3.0f;
/// How far inside the latitude edge recovery lands the brightest regions.
constexpr float kHighlightHeadroom = 1.0f;
/// The shadow-side margin.
constexpr float kShadowHeadroom = 0.5f;

}  // namespace AutoAdjustment
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_AUTO_ADJUSTMENT_H
