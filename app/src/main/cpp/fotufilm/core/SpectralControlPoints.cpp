// Port of Sources/FotufilmCore/SpectralControlPoints.swift.
#include "SpectralControlPoints.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "Simd.h"
#include "SpectralGrid.h"

namespace fotufilm {
namespace SpectralCurve {

std::vector<float> resampled(const std::vector<SpectralControlPoint>& points) {
    std::vector<SpectralControlPoint> sorted = points;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SpectralControlPoint& a, const SpectralControlPoint& b) { return a.nm < b.nm; });
    if (sorted.empty()) return std::vector<float>(SpectralGrid::count, 0.0f);
    if (!(sorted.size() > 1)) return std::vector<float>(SpectralGrid::count, swiftMax(sorted[0].value, 0.0f));

    // Two handles on the same wavelength: the later one wins.
    std::vector<float> xs, ys;
    for (const auto& point : sorted) {
        if (!xs.empty() && point.nm - xs.back() < 0.5f) {
            ys[ys.size() - 1] = point.value;
        } else {
            xs.push_back(point.nm);
            ys.push_back(point.value);
        }
    }
    if (!(xs.size() > 1)) return std::vector<float>(SpectralGrid::count, swiftMax(ys[0], 0.0f));

    const size_t n = xs.size();
    std::vector<float> h(n - 1), delta(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) h[i] = xs[i + 1] - xs[i];
    for (size_t i = 0; i + 1 < n; ++i) delta[i] = (ys[i + 1] - ys[i]) / h[i];

    // Fritsch–Carlson slopes.
    std::vector<float> slope(n, 0.0f);
    slope[0] = delta[0];
    slope[n - 1] = delta[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        if (delta[i - 1] * delta[i] <= 0) {
            slope[i] = 0;
        } else {
            float w1 = 2 * h[i] + h[i - 1];
            float w2 = h[i] + 2 * h[i - 1];
            slope[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
        }
    }

    size_t segment = 0;
    const auto& wavelengths = SpectralGrid::wavelengths();
    std::vector<float> out;
    out.reserve(wavelengths.size());
    for (float wavelength : wavelengths) {
        if (wavelength <= xs[0]) {
            out.push_back(swiftMax(ys[0], 0.0f));
            continue;
        }
        if (wavelength >= xs[n - 1]) {
            out.push_back(swiftMax(ys[n - 1], 0.0f));
            continue;
        }
        while (wavelength > xs[segment + 1]) segment += 1;
        float t = (wavelength - xs[segment]) / h[segment];
        float t2 = t * t, t3 = t2 * t;
        float value = ys[segment] * (2 * t3 - 3 * t2 + 1)
            + slope[segment] * h[segment] * (t3 - 2 * t2 + t)
            + ys[segment + 1] * (-2 * t3 + 3 * t2)
            + slope[segment + 1] * h[segment] * (t3 - t2);
        out.push_back(swiftMax(value, 0.0f));
    }
    return out;
}

std::vector<SpectralControlPoint> controlPoints(const std::vector<float>& samples, float tolerance) {
    preconditionCheck(samples.size() == static_cast<size_t>(SpectralGrid::count), "samples on SpectralGrid");
    float peak = swiftMax(maxOr(samples, 0), 1e-12f);
    std::vector<float> scaled(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) scaled[i] = samples[i] / peak;
    std::vector<bool> keep(samples.size(), false);
    keep[0] = true;
    keep[samples.size() - 1] = true;

    std::function<void(int, int)> simplify = [&](int low, int high) {
        if (!(high > low + 1)) return;
        float span = static_cast<float>(high - low);
        int worst = low;
        float worstDistance = 0;
        for (int i = low + 1; i < high; ++i) {
            float t = static_cast<float>(i - low) / span;
            float line = scaled[low] + t * (scaled[high] - scaled[low]);
            float distance = std::fabs(scaled[i] - line);
            if (distance > worstDistance) {
                worstDistance = distance;
                worst = i;
            }
        }
        if (!(worstDistance > tolerance)) return;
        keep[worst] = true;
        simplify(low, worst);
        simplify(worst, high);
    };
    simplify(0, static_cast<int>(samples.size()) - 1);

    std::vector<SpectralControlPoint> out;
    const auto& wavelengths = SpectralGrid::wavelengths();
    for (size_t i = 0; i < samples.size(); ++i) {
        if (keep[i]) out.push_back(SpectralControlPoint{wavelengths[i], samples[i]});
    }
    return out;
}

}  // namespace SpectralCurve

namespace SpectralGrid {

std::vector<std::vector<float>> familyDyeDensities(FilmDyeFamily family) { return dyes(family); }

std::vector<std::vector<float>> partitionedDyes(const std::vector<std::vector<float>>& densities) {
    return partition(densities);
}

}  // namespace SpectralGrid
}  // namespace fotufilm
