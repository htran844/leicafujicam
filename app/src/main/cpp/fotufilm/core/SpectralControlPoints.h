// Port of Sources/FotufilmCore/SpectralControlPoints.swift. The `SpectralGrid` extension
// (familyDyeDensities, partitionedDyes) is declared in SpectralGrid.h and defined in the .cpp.
#ifndef FOTUFILM_CORE_SPECTRAL_CONTROL_POINTS_H
#define FOTUFILM_CORE_SPECTRAL_CONTROL_POINTS_H

#include <vector>

namespace fotufilm {

/// One handle of a hand-drawn spectral curve.
struct SpectralControlPoint {
    float nm;
    float value;
    bool operator==(const SpectralControlPoint& o) const { return nm == o.nm && value == o.value; }
};

/// A hand-drawn spectral curve and the conversions between its handles and the full grid.
namespace SpectralCurve {
/// The drawn curve on the engine's own grid: monotone cubic (Fritsch–Carlson) between handles,
/// held flat outside them.
std::vector<float> resampled(const std::vector<SpectralControlPoint>& points);
/// Handles for a sampled row (Ramer–Douglas–Peucker against the row's own peak).
std::vector<SpectralControlPoint> controlPoints(const std::vector<float>& samples, float tolerance = 0.01f);
}  // namespace SpectralCurve

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SPECTRAL_CONTROL_POINTS_H
