// Port of Sources/FotufilmCore/BooleanGrain.swift.
#include "BooleanGrain.h"

#include <cmath>

#include "Simd.h"

namespace fotufilm {
namespace BooleanGrain {

namespace {
constexpr double kPi = 3.141592653589793;
}

double discIntersectionArea(double h, double r) {
    if (!(h < 2 * r)) return 0;
    double half = h / 2;
    return 2 * r * r * std::acos(swiftMin(half / r, 1.0)) - half * std::sqrt(swiftMax(4 * r * r - h * h, 0.0));
}

double coverageCovariance(double h, double r, double a) {
    double coverage = swiftMin(swiftMax(a, 1e-6), 1 - 1e-6);
    double lambda = -std::log(1 - coverage) / (kPi * r * r);
    return (1 - coverage) * (1 - coverage) * std::expm1(lambda * discIntersectionArea(h, r));
}

float granularity(float r, float a, float R, int steps) {
    if (!(r > 0 && R > 0)) return 0;
    double radius = static_cast<double>(r), aperture = static_cast<double>(R);
    double area = kPi * aperture * aperture;
    double top = swiftMin(2 * radius, 2 * aperture);
    double step = top / static_cast<double>(steps);
    double total = 0.0;
    for (int i = 0; i < steps; ++i) {
        double h = top * (static_cast<double>(i) + 0.5) / static_cast<double>(steps);
        total += coverageCovariance(h, radius, static_cast<double>(a)) * discIntersectionArea(h, aperture) * 2 * kPi *
                 h * step;
    }
    return static_cast<float>(std::sqrt(swiftMax(total, 0.0)) / area);
}

float radius(float target, float a, float R) {
    if (!(target > 0 && R > 0)) return 0;
    double low = 1e-6, high = static_cast<double>(R) * 2;
    for (int i = 0; i < 60; ++i) {
        double mid = (low + high) / 2;
        if (granularity(static_cast<float>(mid), a, R) < target) low = mid; else high = mid;
    }
    return static_cast<float>((low + high) / 2);
}

}  // namespace BooleanGrain
}  // namespace fotufilm
