// Port of Sources/FotufilmCore/SpectralModel.swift: enum SpectralGrid.
#include "SpectralGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "ColorScience.h"

namespace fotufilm {
namespace SpectralGrid {

const std::vector<float>& wavelengths() {
    static const std::vector<float> values = [] {
        // stride(from: 380, through: 780, by: 5) on Float: start + index * stride.
        std::vector<float> v;
        for (int i = 0; i < count; ++i) v.push_back(380.0f + static_cast<float>(i) * stepNM);
        return v;
    }();
    return values;
}

const std::vector<float>& xBar() {
    static const std::vector<float> values = {
        0.001368f, 0.002236f, 0.004243f, 0.00765f, 0.01431f, 0.02319f, 0.04351f, 0.07763f,
        0.13438f, 0.21477f, 0.2839f, 0.3285f, 0.34828f, 0.34806f, 0.3362f, 0.3187f,
        0.2908f, 0.2511f, 0.19536f, 0.1421f, 0.09564f, 0.05795001f, 0.03201f, 0.0147f,
        0.0049f, 0.0024f, 0.0093f, 0.0291f, 0.06327f, 0.1096f, 0.1655f, 0.2257499f,
        0.2904f, 0.3597f, 0.4334499f, 0.5120501f, 0.5945f, 0.6784f, 0.7621f, 0.8425f,
        0.9163f, 0.9786f, 1.0263f, 1.0567f, 1.0622f, 1.0456f, 1.0026f, 0.9384f,
        0.8544499f, 0.7514f, 0.6424f, 0.5419f, 0.4479f, 0.3608f, 0.2835f, 0.2187f,
        0.1649f, 0.1212f, 0.0874f, 0.0636f, 0.04677f, 0.0329f, 0.0227f, 0.01584f,
        0.01135916f, 0.008110916f, 0.005790346f, 0.004109457f, 0.002899327f, 0.00204919f, 0.001439971f, 0.000999949f,
        0.000690079f, 0.000476021f, 0.000332301f, 0.000234826f, 0.000166151f, 0.000117413f, 8.3075e-05f, 5.8707e-05f,
        4.151e-05f,
    };
    return values;
}

const std::vector<float>& yBar() {
    static const std::vector<float> values = {
        3.9e-05f, 6.4e-05f, 0.00012f, 0.000217f, 0.000396f, 0.00064f, 0.00121f, 0.00218f,
        0.004f, 0.0073f, 0.0116f, 0.01684f, 0.023f, 0.0298f, 0.038f, 0.048f,
        0.06f, 0.0739f, 0.09098f, 0.1126f, 0.13902f, 0.1693f, 0.20802f, 0.2586f,
        0.323f, 0.4073f, 0.503f, 0.6082f, 0.71f, 0.7932f, 0.862f, 0.9148501f,
        0.954f, 0.9803f, 0.9949501f, 1.0f, 0.995f, 0.9786f, 0.952f, 0.9154f,
        0.87f, 0.8163f, 0.757f, 0.6949f, 0.631f, 0.5668f, 0.503f, 0.4412f,
        0.381f, 0.321f, 0.265f, 0.217f, 0.175f, 0.1382f, 0.107f, 0.0816f,
        0.061f, 0.04458f, 0.032f, 0.0232f, 0.017f, 0.01192f, 0.00821f, 0.005723f,
        0.004102f, 0.002929f, 0.002091f, 0.001484f, 0.001047f, 0.00074f, 0.00052f, 0.0003611f,
        0.0002492f, 0.0001719f, 0.00012f, 8.48e-05f, 6e-05f, 4.24e-05f, 3e-05f, 2.12e-05f,
        1.499e-05f,
    };
    return values;
}

const std::vector<float>& zBar() {
    static const std::vector<float> values = {
        0.006450001f, 0.01054999f, 0.02005001f, 0.03621f, 0.06785001f, 0.1102f, 0.2074f, 0.3713f,
        0.6456f, 1.0390501f, 1.3856f, 1.62296f, 1.74706f, 1.7826f, 1.77211f, 1.7441f,
        1.6692f, 1.5281f, 1.28764f, 1.0419f, 0.8129501f, 0.6162f, 0.46518f, 0.3533f,
        0.272f, 0.2123f, 0.1582f, 0.1117f, 0.07824999f, 0.05725001f, 0.04216f, 0.02984f,
        0.0203f, 0.0134f, 0.008749999f, 0.005749999f, 0.0039f, 0.002749999f, 0.0021f, 0.0018f,
        0.001650001f, 0.0014f, 0.0011f, 0.001f, 0.0008f, 0.0006f, 0.00034f, 0.00024f,
        0.00019f, 0.0001f, 5e-05f, 3e-05f, 2e-05f, 1e-05f, -0.0f, 0.0f,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0,
    };
    return values;
}

const std::vector<float>& d65() {
    static const std::vector<float> values = {
        49.9755f, 52.3118f, 54.6482f, 68.7015f, 82.7549f, 87.1204f, 91.486f, 92.4589f,
        93.4318f, 90.057f, 86.6823f, 95.7736f, 104.865f, 110.936f, 117.008f, 117.41f,
        117.812f, 116.336f, 114.861f, 115.392f, 115.923f, 112.367f, 108.811f, 109.082f,
        109.354f, 108.578f, 107.802f, 106.296f, 104.79f, 106.239f, 107.689f, 106.047f,
        104.405f, 104.225f, 104.046f, 102.023f, 100.0f, 98.1671f, 96.3342f, 96.0611f,
        95.788f, 92.2368f, 88.6856f, 89.3459f, 90.0062f, 89.8026f, 89.5991f, 88.6489f,
        87.6987f, 85.4936f, 83.2886f, 83.4939f, 83.6992f, 81.863f, 80.0268f, 80.1207f,
        80.2146f, 81.2462f, 82.2778f, 80.281f, 78.2842f, 74.0027f, 69.7213f, 70.6652f,
        71.6091f, 72.979f, 74.349f, 67.9765f, 61.604f, 65.7448f, 69.8856f, 72.4863f,
        75.087f, 69.3398f, 63.5927f, 55.0054f, 46.4182f, 56.6118f, 66.8054f, 65.0941f,
        63.3828f,
    };
    return values;
}

std::vector<float> asymmetricGaussian(float peak, float leftSigma, float rightSigma) {
    const auto& w = wavelengths();
    std::vector<float> out(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        float wavelength = w[i];
        float sigma = wavelength < peak ? leftSigma : rightSigma;
        float x = (wavelength - peak) / swiftMax(sigma, 1.0f);
        out[i] = std::exp(-0.5f * x * x);
    }
    return out;
}

std::vector<float> continuedTails(const std::vector<float>& layer) {
    float peak;
    if (!sequenceMax(layer, peak) || !(peak > 0)) return layer;
    int low = -1, high = -1;
    for (int i = 0; i < static_cast<int>(layer.size()); ++i) {
        if (layer[i] > 0) {
            if (low < 0) low = i;
            high = i;
        }
    }
    if (low < 0 || !(high > low)) return layer;
    const float step = stepNM;
    const float cutoff = std::log10(peak) - tailDecades;
    std::vector<float> result = layer;

    // Terminal slope in log10 per nm over up to 30 nm of trace, positive when falling outward.
    auto decay = [&](int edge, int inward) -> float {
        int reach = static_cast<int>(std::round(30 / stepNM));
        int anchor = inward > 0 ? swiftMin(high, edge + reach) : swiftMax(low, edge - reach);
        if (anchor == edge) return minimumTailDecayPerNM;
        float rise = std::log10(layer[edge]) - std::log10(layer[anchor]);
        float span = static_cast<float>(std::abs(edge - anchor)) * step;
        return swiftMax(-rise / span, minimumTailDecayPerNM);
    };

    int n = static_cast<int>(layer.size());
    if (high < n - 1) {
        float rate = decay(high, -1);
        float value = std::log10(layer[high]);
        for (int i = high + 1; i < n; ++i) {
            value -= rate * step;
            result[i] = value > cutoff ? std::pow(10.0f, value) : 0.0f;
        }
    }
    if (low > 0) {
        float rate = decay(low, 1);
        float value = std::log10(layer[low]);
        for (int i = low - 1; i >= 0; --i) {
            value -= rate * step;
            result[i] = value > cutoff ? std::pow(10.0f, value) : 0.0f;
        }
    }
    return result;
}

std::vector<std::vector<float>> normalizeSensitivities(const std::vector<std::vector<float>>& layers) {
    const auto& light = d65();
    std::vector<std::vector<float>> out;
    out.reserve(layers.size());
    for (const auto& layer : layers) {
        float integral = 0;
        size_t n = std::min(layer.size(), light.size());
        for (size_t i = 0; i < n; ++i) integral = integral + layer[i] * light[i];
        float divisor = swiftMax(integral, 1e-12f);
        std::vector<float> row(layer.size());
        for (size_t i = 0; i < layer.size(); ++i) row[i] = layer[i] / divisor;
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<std::vector<float>> dyes(FilmDyeFamily family) {
    if (family == FilmDyeFamily::monochrome) {
        std::vector<float> third(count, 1.0f / 3.0f);
        return {third, third, third};
    }
    std::vector<float> centers, widths;
    float floor = 0;
    switch (family) {
        case FilmDyeFamily::kodakNegative:
            centers = {665, 548, 445}; widths = {64, 50, 45}; floor = 0.035f;
            break;
        case FilmDyeFamily::fujiNegative:
            centers = {675, 545, 445}; widths = {58, 46, 42}; floor = 0.025f;
            break;
        case FilmDyeFamily::motionNegative:
            centers = {670, 550, 445}; widths = {56, 45, 40}; floor = 0.018f;
            break;
        case FilmDyeFamily::kodachrome:
            centers = {650, 535, 440}; widths = {50, 42, 38}; floor = 0.012f;
            break;
        case FilmDyeFamily::monochrome:
            throw std::runtime_error("handled above");
    }
    std::vector<std::vector<float>> result;
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<float> row = asymmetricGaussian(centers[channel], widths[channel], widths[channel]);
        for (float& v : row) v = v + floor;
        result.push_back(std::move(row));
    }
    for (int i = 0; i < count; ++i) {
        for (int layer = 0; layer < 3; ++layer) result[layer][i] = std::pow(result[layer][i], 6.0f);
    }
    return partition(result);
}

std::vector<std::vector<float>> partition(const std::vector<std::vector<float>>& densities) {
    preconditionCheck(densities.size() == 3, "partition needs three rows");
    std::vector<std::vector<float>> result = densities;
    for (int i = 0; i < count; ++i) {
        float sum = result[0][i] + result[1][i] + result[2][i];
        for (int layer = 0; layer < 3; ++layer) result[layer][i] /= swiftMax(sum, 1e-12f);
    }
    return result;
}

std::vector<float> blackbody(float kelvinK) {
    const float c2 = 14387769.0f;
    const float temperature = swiftMax(kelvinK, 1.0f);
    const auto& w = wavelengths();
    std::vector<float> values(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        float wavelength = w[i];
        values[i] = 1 / (std::pow(wavelength, 5.0f) * (std::exp(c2 / (wavelength * temperature)) - 1));
    }
    float peak = maxOr(values, 1);
    for (float& v : values) v /= peak;
    return values;
}

float interpolate(const std::vector<float>& curve, float wavelength) {
    if (curve.size() != static_cast<size_t>(count)) return 0;
    float position = (wavelength - wavelengths()[0]) / stepNM;
    if (position <= 0) return curve[0];
    if (position >= static_cast<float>(count - 1)) return curve[count - 1];
    int low = static_cast<int>(position);
    float fraction = position - static_cast<float>(low);
    return curve[low] * (1 - fraction) + curve[low + 1] * fraction;
}

Float3 xyz(const std::vector<float>& spectrum) {
    preconditionCheck(spectrum.size() == static_cast<size_t>(count), "spectrum on SpectralGrid");
    const auto& xb = xBar();
    const auto& yb = yBar();
    const auto& zb = zBar();
    Float3 result = Float3::repeating(0);
    for (int i = 0; i < count; ++i) {
        result.x += spectrum[i] * xb[i];
        result.y += spectrum[i] * yb[i];
        result.z += spectrum[i] * zb[i];
    }
    return result;
}

Float3 linearDisplayP3FromXYZ(Float3 v) {
    return Float3(
        2.4934969f * v.x - 0.9313836f * v.y - 0.4027108f * v.z,
        -0.8294890f * v.x + 1.7626641f * v.y + 0.0236247f * v.z,
        0.0358458f * v.x - 0.0761724f * v.y + 0.9568845f * v.z);
}

Float3 linearRec2020FromXYZ(Float3 v) {
    return ColorScience::linearDisplayP3ToRec2020(linearDisplayP3FromXYZ(v));
}

const std::vector<float>& enlarger3200K() {
    static const std::vector<float> values = blackbody(3200);
    return values;
}

const std::vector<float>& equalEnergy() {
    static const std::vector<float> values(count, 1.0f);
    return values;
}

namespace {

Float3 bradfordLMSFromXYZ(Float3 value) {
    return Float3(
        0.8951f * value.x + 0.2664f * value.y - 0.1614f * value.z,
        -0.7502f * value.x + 1.7135f * value.y + 0.0367f * value.z,
        0.0389f * value.x - 0.0685f * value.y + 1.0296f * value.z);
}

Float3 xyzFromBradfordLMS(Float3 value) {
    return Float3(
        0.9869929f * value.x - 0.1470543f * value.y + 0.1599627f * value.z,
        0.4323053f * value.x + 0.5183603f * value.y + 0.0492912f * value.z,
        -0.0085287f * value.x + 0.0400428f * value.y + 0.9684867f * value.z);
}

const Float3& d65WhiteXYZ() {
    static const Float3 white = [] {
        Float3 w = xyz(d65());
        return w / swiftMax(w.y, 1e-12f);
    }();
    return white;
}

const Float3& d65DisplayP3White() {
    static const Float3 white = linearDisplayP3FromXYZ(d65WhiteXYZ());
    return white;
}

}  // namespace

Float3 toLinearDisplayP3(const std::vector<float>& reflectance) { return toLinearDisplayP3(reflectance, d65()); }

Float3 toLinearDisplayP3(const std::vector<float>& reflectance, const std::vector<float>& illuminant) {
    preconditionCheck(reflectance.size() == static_cast<size_t>(count), "reflectance on SpectralGrid");
    std::vector<float> reflected(count, 0.0f);
    for (int i = 0; i < count; ++i) reflected[i] = illuminant[i] * reflectance[i];
    Float3 reflectedXYZ = xyz(reflected);
    Float3 whiteXYZ = xyz(illuminant);
    reflectedXYZ /= swiftMax(whiteXYZ.y, 1e-12f);
    whiteXYZ /= swiftMax(whiteXYZ.y, 1e-12f);
    Float3 sourceLMS = bradfordLMSFromXYZ(whiteXYZ);
    Float3 destinationLMS = bradfordLMSFromXYZ(d65WhiteXYZ());
    Float3 reflectedLMS = bradfordLMSFromXYZ(reflectedXYZ);
    Float3 adaptedLMS(
        reflectedLMS.x * destinationLMS.x / swiftMax(sourceLMS.x, 1e-12f),
        reflectedLMS.y * destinationLMS.y / swiftMax(sourceLMS.y, 1e-12f),
        reflectedLMS.z * destinationLMS.z / swiftMax(sourceLMS.z, 1e-12f));
    Float3 rgb = linearDisplayP3FromXYZ(xyzFromBradfordLMS(adaptedLMS));
    const Float3& white = d65DisplayP3White();
    return Float3(rgb.x / white.x, rgb.y / white.y, rgb.z / white.z);
}

}  // namespace SpectralGrid
}  // namespace fotufilm
