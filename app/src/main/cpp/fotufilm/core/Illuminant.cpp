// Port of Sources/FotufilmCore/Illuminant.swift.
#include "Illuminant.h"

#include <cmath>

#include "SpectralGrid.h"

namespace fotufilm {
namespace Illuminant {

std::vector<float> planckian(float kelvin) {
    std::vector<float> values = SpectralGrid::blackbody(kelvin);
    float anchor = values[anchorIndex];
    if (!(anchor > 0)) return values;
    for (float& v : values) v /= anchor;
    return values;
}

std::vector<float> daylight(float kelvin) {
    float t = clamp(kelvin, 4000.0f, 25000.0f);
    // The locus polynomial is shared with white balance.
    Float2 xy = WhiteBalance::daylightXY(t);
    double x = static_cast<double>(xy.x), y = static_cast<double>(xy.y);
    double m = 0.0241 + 0.2562 * x - 0.7341 * y;
    float m1 = static_cast<float>((-1.3515 - 1.7703 * x + 5.9114 * y) / m);
    float m2 = static_cast<float>((0.0300 - 31.4424 * x + 30.0717 * y) / m);
    const auto& c0 = s0();
    const auto& c1 = s1();
    const auto& c2 = s2();
    std::vector<float> values(SpectralGrid::count);
    for (int i = 0; i < SpectralGrid::count; ++i) values[i] = c0[i] + m1 * c1[i] + m2 * c2[i];
    // S1 and S2 are zero at 560 nm and S0 is 100 there, so the anchor is exactly 100.
    float anchor = values[anchorIndex];
    for (float& v : values) v /= anchor;
    return values;
}

std::vector<float> atLocus(float kelvin) {
    float t = clamp(kelvin, 1000.0f, 25000.0f);
    if (t <= 4000) return planckian(t);
    if (t >= 5000) return daylight(t);
    float s = (t - 4000) / 1000;
    float mix = s * s * (3 - 2 * s);
    std::vector<float> warm = planckian(t), cool = daylight(t);
    std::vector<float> out(SpectralGrid::count);
    for (int i = 0; i < SpectralGrid::count; ++i) out[i] = (1 - mix) * warm[i] + mix * cool[i];
    return out;
}

std::vector<float> spectrum(const WhiteBalance& balance) {
    std::vector<float> base = atLocus(balance.kelvin);
    if (balance.tint == 0) return base;
    return matching(WhiteBalance::chromaticity(balance.kelvin, balance.tint), base);
}

std::vector<float> matching(Float2 xyIn, const std::vector<float>& base) {
    preconditionCheck(std::isfinite(xyIn.x) && std::isfinite(xyIn.y) && xyIn.x > 0 && xyIn.y > 0,
                      "invalid illuminant chromaticity");
    Float2 origin = WhiteBalance::uvFromXY(chromaticity(base));
    Float2 xy = WhiteBalance::boundedChromaticity(origin, WhiteBalance::uvFromXY(xyIn) - origin);
    double x = static_cast<double>(xy.x / xy.y), z = static_cast<double>((1 - xy.x - xy.y) / xy.y);
    const auto& xb = SpectralGrid::xBar();
    const auto& yb = SpectralGrid::yBar();
    const auto& zb = SpectralGrid::zBar();
    std::vector<double> u(SpectralGrid::count), v(SpectralGrid::count);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        u[i] = static_cast<double>(xb[i]) - x * static_cast<double>(yb[i]);
        v[i] = static_cast<double>(zb[i]) - z * static_cast<double>(yb[i]);
    }
    double a = 0.0, b = 0.0;
    std::vector<double> result(base.begin(), base.end());
    for (int iteration = 0; iteration < 128; ++iteration) {
        double f = 0.0, g = 0.0, aa = 0.0, ab = 0.0, bb = 0.0;
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = static_cast<double>(base[i]) * std::exp(a * u[i] + b * v[i]);
            f += result[i] * u[i];
            g += result[i] * v[i];
            aa += result[i] * u[i] * u[i];
            ab += result[i] * u[i] * v[i];
            bb += result[i] * v[i] * v[i];
        }
        if (swiftMax(std::fabs(f), std::fabs(g)) < 1e-10) break;
        double determinant = aa * bb - ab * ab;
        preconditionCheck(determinant > 0, "illuminant chromaticity is outside the spectral support");
        double da = (bb * f - ab * g) / determinant;
        double db = (aa * g - ab * f) / determinant;
        double scale = swiftMin(1.0, 1 / swiftMax(std::fabs(da), std::fabs(db)));
        a -= scale * da;
        b -= scale * db;
    }
    std::vector<float> values(result.size());
    for (size_t i = 0; i < result.size(); ++i) values[i] = static_cast<float>(result[i]);
    Float2 white = chromaticity(values);
    preconditionCheck(std::fabs(white.x - xy.x) < 1e-5f && std::fabs(white.y - xy.y) < 1e-5f,
                      "illuminant chromaticity solve did not converge");
    return values;
}

Float2 chromaticity(const std::vector<float>& spectrum) {
    Float3 xyz = SpectralGrid::xyz(spectrum);
    preconditionCheck(std::isfinite(xyz.sum()) && xyz.sum() > 0, "illuminant must contain visible energy");
    return Float2(xyz.x, xyz.y) / xyz.sum();
}

float luminance(const std::vector<float>& spectrum) {
    preconditionCheck(spectrum.size() == static_cast<size_t>(SpectralGrid::count), "spectrum on SpectralGrid");
    const auto& yb = SpectralGrid::yBar();
    double y = 0;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        y = y + static_cast<double>(spectrum[i]) * static_cast<double>(yb[i]);
    }
    preconditionCheck(std::isfinite(y) && y > 0, "illuminant must contain visible energy");
    return static_cast<float>(y);
}

const std::vector<float>& a() {
    static const std::vector<float> values = planckian(2856);
    return values;
}

const std::vector<float>& d65() { return SpectralGrid::d65(); }

const std::vector<float>& d50() {
    static const std::vector<float> values = daylight(5003);
    return values;
}

const std::vector<float>& measuredXenonProjection() {
    static const std::vector<float> values = {
        0.136746f, 0.206570f, 0.281575f, 0.381492f, 0.477762f,  // 380-400 nm
        0.580290f, 0.680332f, 0.752829f, 0.783793f, 0.796124f,  // 405-425 nm
        0.831295f, 0.891565f, 0.966010f, 1.007482f, 1.079627f,  // 430-450 nm
        1.117596f, 1.188808f, 1.309845f, 1.362901f, 1.240829f,  // 455-475 nm
        1.162528f, 1.170259f, 1.136912f, 1.146093f, 1.091440f,  // 480-500 nm
        1.070819f, 1.058280f, 1.049140f, 1.045223f, 1.038052f,  // 505-525 nm
        1.038052f, 1.031067f, 1.030798f, 1.024933f, 1.014342f,  // 530-550 nm
        1.006031f, 1.000000f, 0.997845f, 1.009409f, 1.012829f,  // 555-575 nm
        1.009534f, 1.004477f, 0.979938f, 0.945534f, 0.913741f,  // 580-600 nm
        0.902674f, 0.902922f, 0.915710f, 0.920497f, 0.905554f,  // 605-625 nm
        0.870881f, 0.821181f, 0.767668f, 0.738798f, 0.728746f,  // 630-650 nm
        0.710694f, 0.716394f, 0.716332f, 0.672787f, 0.560642f,  // 655-675 nm
        0.420808f, 0.292705f, 0.180933f, 0.105948f, 0.061306f,  // 680-700 nm
        0.042653f, 0.031200f, 0.026257f, 0.018528f, 0.017583f,  // 705-725 nm
        0.016041f, 0.014596f, 0.014301f, 0.015295f, 0.014873f,  // 730-750 nm
        0.012305f, 0.010856f, 0.018298f, 0.018653f, 0.021065f,  // 755-775 nm
        0.017534f,                                              // 780 nm
    };
    return values;
}

const std::vector<float>& xenonProjection() {
    static const std::vector<float> values = [] {
        // exp(aq + bq²), q = (λ − 560 nm) / 100 nm, solved for daylight(kelvin: 5400)'s white.
        const float a = 0.13620723f;
        const float b = -0.02311008f;
        const auto& w = SpectralGrid::wavelengths();
        const auto& measured = measuredXenonProjection();
        std::vector<float> out(w.size());
        for (size_t i = 0; i < w.size(); ++i) {
            float q = (w[i] - 560) / 100;
            out[i] = measured[i] * std::exp(a * q + b * q * q);
        }
        float anchor = out[anchorIndex];
        for (float& v : out) v /= anchor;
        return out;
    }();
    return values;
}

const std::vector<float>& s0() {
    static const std::vector<float> values = {
        63.4f, 64.6f, 65.8f, 80.3f, 94.8f, 99.8f, 104.8f, 105.35f,
        105.9f, 101.35f, 96.8f, 105.35f, 113.9f, 119.75f, 125.6f, 125.55f,
        125.5f, 123.4f, 121.3f, 121.3f, 121.3f, 117.4f, 113.5f, 113.3f,
        113.1f, 111.95f, 110.8f, 108.65f, 106.5f, 107.65f, 108.8f, 107.05f,
        105.3f, 104.85f, 104.4f, 102.2f, 100.0f, 98.0f, 96.0f, 95.55f,
        95.1f, 92.1f, 89.1f, 89.8f, 90.5f, 90.4f, 90.3f, 89.35f,
        88.4f, 86.2f, 84.0f, 84.55f, 85.1f, 83.5f, 81.9f, 82.25f,
        82.6f, 83.75f, 84.9f, 83.1f, 81.3f, 76.6f, 71.9f, 73.1f,
        74.3f, 75.35f, 76.4f, 69.85f, 63.3f, 67.5f, 71.7f, 74.35f,
        77.0f, 71.1f, 65.2f, 56.45f, 47.7f, 58.15f, 68.6f, 66.8f,
        65.0f,
    };
    return values;
}

const std::vector<float>& s1() {
    static const std::vector<float> values = {
        38.5f, 36.75f, 35.0f, 39.2f, 43.4f, 44.85f, 46.3f, 45.1f,
        43.9f, 40.5f, 37.1f, 36.9f, 36.7f, 36.3f, 35.9f, 34.25f,
        32.6f, 30.25f, 27.9f, 26.1f, 24.3f, 22.2f, 20.1f, 18.15f,
        16.2f, 14.7f, 13.2f, 10.9f, 8.6f, 7.35f, 6.1f, 5.15f,
        4.2f, 3.05f, 1.9f, 0.95f, 0.0f, -0.8f, -1.6f, -2.55f,
        -3.5f, -3.5f, -3.5f, -4.65f, -5.8f, -6.5f, -7.2f, -7.9f,
        -8.6f, -9.05f, -9.5f, -10.2f, -10.9f, -10.8f, -10.7f, -11.35f,
        -12.0f, -13.0f, -14.0f, -13.8f, -13.6f, -12.8f, -12.0f, -12.65f,
        -13.3f, -13.1f, -12.9f, -11.75f, -10.6f, -11.1f, -11.6f, -11.9f,
        -12.2f, -11.2f, -10.2f, -9.0f, -7.8f, -9.5f, -11.2f, -10.8f,
        -10.4f,
    };
    return values;
}

const std::vector<float>& s2() {
    static const std::vector<float> values = {
        3.0f, 2.1f, 1.2f, 0.05f, -1.1f, -0.8f, -0.5f, -0.6f,
        -0.7f, -0.95f, -1.2f, -1.9f, -2.6f, -2.75f, -2.9f, -2.85f,
        -2.8f, -2.7f, -2.6f, -2.6f, -2.6f, -2.2f, -1.8f, -1.65f,
        -1.5f, -1.4f, -1.3f, -1.25f, -1.2f, -1.1f, -1.0f, -0.75f,
        -0.5f, -0.4f, -0.3f, -0.15f, 0.0f, 0.1f, 0.2f, 0.35f,
        0.5f, 1.3f, 2.1f, 2.65f, 3.2f, 3.65f, 4.1f, 4.4f,
        4.7f, 4.9f, 5.1f, 5.9f, 6.7f, 7.0f, 7.3f, 7.95f,
        8.6f, 9.2f, 9.8f, 10.0f, 10.2f, 9.25f, 8.3f, 8.95f,
        9.6f, 9.05f, 8.5f, 7.75f, 7.0f, 7.3f, 7.6f, 7.8f,
        8.0f, 7.35f, 6.7f, 5.95f, 5.2f, 6.3f, 7.4f, 7.1f,
        6.8f,
    };
    return values;
}

}  // namespace Illuminant
}  // namespace fotufilm
