// Port of Sources/FotufilmCore/Densitometry.swift.
#include "Densitometry.h"

#include <cmath>

#include "SpectralGrid.h"

namespace fotufilm {

namespace Densitometry {

namespace {
struct Sample {
    float nm;
    float logProduct;
};

/// ISO Status A log10 spectral products at 10 nm, peak 5.000: Hunt, The Reproduction of Colour,
/// 6th ed., Table 14.1, columns P_AR, P_AG, P_AB.
const std::vector<std::vector<Sample>>& statusATenNM() {
    static const std::vector<std::vector<Sample>> values = {
        {{600, 2.568f}, {610, 4.638f}, {620, 5.000f}, {630, 4.871f}, {640, 4.604f},
         {650, 4.286f}, {660, 3.900f}, {670, 3.551f}, {680, 3.165f}, {690, 2.776f},
         {700, 2.383f}, {710, 1.970f}, {720, 1.551f}, {730, 1.141f}, {740, 0.741f},
         {750, 0.341f}},
        {{500, 1.650f}, {510, 3.822f}, {520, 4.782f}, {530, 5.000f}, {540, 4.906f},
         {550, 4.644f}, {560, 4.221f}, {570, 3.609f}, {580, 2.766f}, {590, 1.579f}},
        {{420, 3.602f}, {430, 4.819f}, {440, 5.000f}, {450, 4.912f}, {460, 4.620f},
         {470, 4.040f}, {480, 2.989f}, {490, 1.566f}, {500, 0.165f}},
    };
    return values;
}

const std::vector<float>& statusAIntegral() {
    static const std::vector<float> values = [] {
        std::vector<float> v;
        for (const auto& band : statusA()) {
            float sum = 0;
            for (float x : band) sum += x;
            v.push_back(sum);
        }
        return v;
    }();
    return values;
}
}  // namespace

const std::vector<std::vector<float>>& statusA() {
    static const std::vector<std::vector<float>> values = [] {
        std::vector<std::vector<float>> result;
        for (const auto& band : statusATenNM()) {
            std::vector<float> row;
            for (float wavelength : SpectralGrid::wavelengths()) {
                const Sample& first = band.front();
                const Sample& last = band.back();
                if (!(wavelength >= first.nm && wavelength <= last.nm)) {
                    row.push_back(0);
                    continue;
                }
                bool found = false;
                for (size_t i = 0; i + 1 < band.size(); ++i) {
                    if (!(wavelength <= band[i + 1].nm)) continue;
                    const Sample& low = band[i];
                    const Sample& high = band[i + 1];
                    float t = (wavelength - low.nm) / (high.nm - low.nm);
                    float lowValue = std::pow(10.0f, low.logProduct - 5);
                    float highValue = std::pow(10.0f, high.logProduct - 5);
                    row.push_back(lowValue + t * (highValue - lowValue));
                    found = true;
                    break;
                }
                if (!found) row.push_back(std::pow(10.0f, last.logProduct - 5));
            }
            result.push_back(std::move(row));
        }
        return result;
    }();
    return values;
}

Float3 statusADensity(const std::vector<float>& transmittance) {
    Float3 read = Float3::repeating(0);
    for (int channel = 0; channel < 3; ++channel) {
        float sum = 0;
        const std::vector<float>& band = statusA()[channel];
        for (int i = 0; i < SpectralGrid::count; ++i) sum += band[i] * transmittance[i];
        read[channel] = -std::log10(swiftMax(sum / statusAIntegral()[channel], 1e-12f));
    }
    return read;
}

std::vector<float> transmittance(Float3 amounts, const std::vector<std::vector<float>>& dyes) {
    std::vector<float> result(SpectralGrid::count, 0.0f);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        float density = amounts.x * dyes[0][i] + amounts.y * dyes[1][i] + amounts.z * dyes[2][i];
        result[i] = std::pow(10.0f, -density);
    }
    return result;
}

Float3 statusADensity(Float3 amounts, const std::vector<std::vector<float>>& dyes) {
    return statusADensity(transmittance(amounts, dyes));
}

}  // namespace Densitometry

PrintDyeUnmix::PrintDyeUnmix(const std::vector<std::vector<float>>& dyes) : dyes_(dyes) {
    for (int i = 0; i < SpectralGrid::count; ++i) dye_.push_back(Float3(dyes[0][i], dyes[1][i], dyes[2][i]));
    const auto& statusA = Densitometry::statusA();
    float integrals[3];
    for (int b = 0; b < 3; ++b) {
        float sum = 0;
        for (float x : statusA[b]) sum += x;
        integrals[b] = sum;
    }
    for (int i = 0; i < SpectralGrid::count; ++i) {
        band_.push_back(Float3(statusA[0][i] / integrals[0], statusA[1][i] / integrals[1],
                               statusA[2][i] / integrals[2]));
    }
    std::vector<Float3> columns(3, Float3());
    for (int d = 0; d < 3; ++d) {
        Float3 amounts = Float3::repeating(0);
        amounts[d] = 1;
        columns[d] = Densitometry::statusADensity(amounts, dyes);
    }
    matrix_ = columns;
    if (!invert(columns, inverse_)) {
        inverse_ = {Float3(1, 0, 0), Float3(0, 1, 0), Float3(0, 0, 1)};
    }
}

Float3 PrintDyeUnmix::read(Float3 amounts) const {
    Float3 sum = Float3::repeating(0);
    for (size_t i = 0; i < dye_.size(); ++i) {
        float density = amounts.x * dye_[i].x + amounts.y * dye_[i].y + amounts.z * dye_[i].z;
        sum += band_[i] * std::exp2(-density * log2Of10);
    }
    return Float3(-std::log10(swiftMax(sum.x, 1e-12f)), -std::log10(swiftMax(sum.y, 1e-12f)),
                  -std::log10(swiftMax(sum.z, 1e-12f)));
}

Float3 PrintDyeUnmix::amounts(Float3 target) const {
    Float3 amounts = apply(inverse_, target);
    for (int iteration = 0; iteration < 16; ++iteration) {
        Float3 residual = read(amounts) - target;
        if (swiftMax(std::fabs(residual.x), swiftMax(std::fabs(residual.y), std::fabs(residual.z))) < 1e-6f) break;
        amounts -= apply(inverse_, residual);
    }
    // Clear paper is no dye at all: zero density above base unmixes to exactly zero.
    return Float3(swiftMax(amounts.x, 0.0f), swiftMax(amounts.y, 0.0f), swiftMax(amounts.z, 0.0f));
}

Float3 PrintDyeUnmix::apply(const std::vector<Float3>& m, Float3 v) const { return m[0] * v.x + m[1] * v.y + m[2] * v.z; }

bool PrintDyeUnmix::invert(const std::vector<Float3>& columns, std::vector<Float3>& out) {
    auto cross = [](Float3 a, Float3 b) {
        return Float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    };
    auto dot = [](Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    float determinant = dot(columns[0], cross(columns[1], columns[2]));
    if (!(std::fabs(determinant) > 1e-9f)) return false;
    Float3 r0 = cross(columns[1], columns[2]) / determinant;
    Float3 r1 = cross(columns[2], columns[0]) / determinant;
    Float3 r2 = cross(columns[0], columns[1]) / determinant;
    out = {Float3(r0.x, r1.x, r2.x), Float3(r0.y, r1.y, r2.y), Float3(r0.z, r1.z, r2.z)};
    return true;
}

}  // namespace fotufilm
