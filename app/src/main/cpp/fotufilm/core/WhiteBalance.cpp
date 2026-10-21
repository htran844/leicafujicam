// Port of Sources/FotufilmCore/WhiteBalance.swift.
#include "WhiteBalance.h"

#include <cmath>
#include <vector>

#include "Illuminant.h"
#include "SpectralGrid.h"

namespace fotufilm {

float WhiteBalance::kelvinToMired(float kelvin) { return 1e6f / swiftMax(kelvin, 1.0f); }

float WhiteBalance::miredToKelvin(float mired) { return 1e6f / swiftMax(mired, 1.0f); }

Float3 WhiteBalance::gains() const {
    if (isNeutral()) return Float3(1, 1, 1);
    Float2 source = chromaticity(kelvin, tint);
    Float2 destination = chromaticity(neutralKelvin, 0);
    Float3 sourceRGB = workingRGB(source);
    Float3 destinationRGB = workingRGB(destination);
    float r = destinationRGB.x / swiftMax(sourceRGB.x, 1e-6f);
    float g = destinationRGB.y / swiftMax(sourceRGB.y, 1e-6f);
    float b = destinationRGB.z / swiftMax(sourceRGB.z, 1e-6f);
    return Float3(r / g, 1, b / g);
}

Float2 WhiteBalance::chromaticity(float kelvin, float tint) {
    float t = clamp(kelvin, 1000.0f, 25000.0f);
    Float2 base = locusXY(t);
    if (tint == 0) return base;

    Float2 uv = uvFromXY(base);
    const float step = 10;
    Float2 ahead = uvFromXY(locusXY(t + step));
    Float2 behind = uvFromXY(locusXY(t - step));
    Float2 tangent = ahead - behind;
    float length = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
    if (!(length > 1e-9f)) return base;
    tangent /= length;
    Float2 normal(-tangent.y, tangent.x);
    Float2 signedNormal = normal.y >= 0 ? normal : -normal;
    Float2 displacement = signedNormal * (tint / 10000);
    return boundedChromaticity(uv, displacement);
}

Float2 WhiteBalance::boundedChromaticity(Float2 uv, Float2 displacement) {
    // Keep the white inside the spectra the engine can represent.
    const auto& xb = SpectralGrid::xBar();
    const auto& yb = SpectralGrid::yBar();
    const auto& zb = SpectralGrid::zBar();
    std::vector<Float2> boundary(SpectralGrid::count);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        Float3 xyz(xb[i], yb[i], zb[i]);
        boundary[i] = uvFromXY(Float2(xyz.x, xyz.y) / xyz.sum());
    }
    auto cross = [](Float2 a, Float2 b) { return a.x * b.y - a.y * b.x; };
    float fraction = 1;
    for (size_t i = 0; i < boundary.size(); ++i) {
        Float2 a = boundary[i] - uv;
        Float2 edge = boundary[(i + 1) % boundary.size()] - boundary[i];
        float denominator = cross(displacement, edge);
        if (!(std::fabs(denominator) > 1e-12f)) continue;
        float distance = cross(a, edge) / denominator;
        float along = cross(a, displacement) / denominator;
        if (distance >= 0 && distance < 1 && along >= 0 && along <= 1) {
            fraction = swiftMin(fraction, distance * 0.95f);
        }
    }
    return xyFromUV(uv + displacement * fraction);
}

Float2 WhiteBalance::locusXY(float kelvin) { return Illuminant::chromaticity(Illuminant::atLocus(kelvin)); }

Float2 WhiteBalance::daylightXY(float kelvin) {
    double t = static_cast<double>(clamp(kelvin, 4000.0f, 25000.0f));
    double x;
    if (t <= 7000) {
        x = 0.244063 + 0.09911e3 / t + 2.9678e6 / (t * t) - 4.6070e9 / (t * t * t);
    } else {
        x = 0.237040 + 0.24748e3 / t + 1.9018e6 / (t * t) - 2.0064e9 / (t * t * t);
    }
    double y = -3.000 * x * x + 2.870 * x - 0.275;
    return Float2(static_cast<float>(x), static_cast<float>(y));
}

Float2 WhiteBalance::planckianXY(float kelvin) {
    std::vector<float> spectrum = SpectralGrid::blackbody(kelvin);
    Float3 xyz = SpectralGrid::xyz(spectrum);
    float sum = xyz.x + xyz.y + xyz.z;
    if (!(sum > 0)) return Float2(0.3127f, 0.3290f);
    return Float2(xyz.x / sum, xyz.y / sum);
}

Float2 WhiteBalance::uvFromXY(Float2 xy) {
    float denominator = -2 * xy.x + 12 * xy.y + 3;
    if (!(std::fabs(denominator) > 1e-9f)) return Float2(0, 0);
    return Float2(4 * xy.x / denominator, 6 * xy.y / denominator);
}

Float2 WhiteBalance::xyFromUV(Float2 uv) {
    float denominator = 2 * uv.x - 8 * uv.y + 4;
    if (!(std::fabs(denominator) > 1e-9f)) return Float2(0.3127f, 0.3290f);
    return Float2(3 * uv.x / denominator, 2 * uv.y / denominator);
}

Float3 WhiteBalance::workingRGB(Float2 xy) {
    if (!(xy.y > 1e-6f)) return Float3(1, 1, 1);
    Float3 xyz(xy.x / xy.y, 1, (1 - xy.x - xy.y) / xy.y);
    return SpectralGrid::linearRec2020FromXYZ(xyz);
}

}  // namespace fotufilm
