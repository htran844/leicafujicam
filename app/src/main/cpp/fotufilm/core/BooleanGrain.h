// Port of Sources/FotufilmCore/BooleanGrain.swift.
#ifndef FOTUFILM_CORE_BOOLEAN_GRAIN_H
#define FOTUFILM_CORE_BOOLEAN_GRAIN_H

namespace fotufilm {

/// Covariance of a silver emulsion's grain, modelled as equal-radius opaque discs from a Poisson
/// process.
namespace BooleanGrain {
/// Area shared by two discs of equal radius whose centres are `separation` apart.
double discIntersectionArea(double separation, double radius);
/// Covariance of the coverage indicator at two points `separation` apart.
double coverageCovariance(double separation, double radius, double coverage);
/// Standard deviation of the covered fraction read through a circular aperture.
float granularity(float radiusMM, float coverage, float apertureRadiusMM, int steps = 512);
/// The grain radius a published granularity implies, by bisection on `granularity`.
float radius(float forGranularity, float coverage, float apertureRadiusMM);
}  // namespace BooleanGrain

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_BOOLEAN_GRAIN_H
