// Port of Sources/FotufilmCore/SpectralModel.swift: SpectralLUT, MeasuredReflectanceTable,
// ReceiverNeutralCalibration, ScreenToeBalance and SpectralGrid's paper* accessors.
#include "SpectralModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include "SpectralGrid.h"

namespace fotufilm {

// ---- SpectralLUT ----

SpectralLUT::SpectralLUT(int dimension, std::vector<float> values) : dimension_(dimension) {
    preconditionCheck(values.size() == static_cast<size_t>(dimension) * dimension * dimension * 4,
                      "SpectralLUT values must be dimension^3 RGBA");
    values_ = std::make_shared<const std::vector<float>>(std::move(values));
}

Float3 SpectralLUT::sample(Float3 p, const float* values, int d) {
    // Keep the unchecked indexing behind a finite-coordinate boundary.
    if (!(d >= 2 && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))) return Float3();
    Float3 q(clamp(p.x, 0.0f, 1.0f) * static_cast<float>(d - 1), clamp(p.y, 0.0f, 1.0f) * static_cast<float>(d - 1),
             clamp(p.z, 0.0f, 1.0f) * static_cast<float>(d - 1));
    int x0 = std::min(static_cast<int>(q.x), d - 2), y0 = std::min(static_cast<int>(q.y), d - 2);
    int z0 = std::min(static_cast<int>(q.z), d - 2);
    Float3 f(q.x - static_cast<float>(x0), q.y - static_cast<float>(y0), q.z - static_cast<float>(z0));
    auto load = [&](int x, int y, int z) {
        const float* rgba = values + ((static_cast<size_t>(z) * d + y) * d + x) * 4;
        return Float3(rgba[0], rgba[1], rgba[2]);
    };
    Float3 c000 = load(x0, y0, z0);
    Float3 c111 = load(x0 + 1, y0 + 1, z0 + 1);
    if (f.x >= f.y) {
        if (f.y >= f.z) {
            Float3 c100 = load(x0 + 1, y0, z0), c110 = load(x0 + 1, y0 + 1, z0);
            return c000 + f.x * (c100 - c000) + f.y * (c110 - c100) + f.z * (c111 - c110);
        } else if (f.x >= f.z) {
            Float3 c100 = load(x0 + 1, y0, z0), c101 = load(x0 + 1, y0, z0 + 1);
            return c000 + f.x * (c100 - c000) + f.z * (c101 - c100) + f.y * (c111 - c101);
        } else {
            Float3 c001 = load(x0, y0, z0 + 1), c101 = load(x0 + 1, y0, z0 + 1);
            return c000 + f.z * (c001 - c000) + f.x * (c101 - c001) + f.y * (c111 - c101);
        }
    } else if (f.x >= f.z) {
        Float3 c010 = load(x0, y0 + 1, z0), c110 = load(x0 + 1, y0 + 1, z0);
        return c000 + f.y * (c010 - c000) + f.x * (c110 - c010) + f.z * (c111 - c110);
    } else if (f.y >= f.z) {
        Float3 c010 = load(x0, y0 + 1, z0), c011 = load(x0, y0 + 1, z0 + 1);
        return c000 + f.y * (c010 - c000) + f.z * (c011 - c010) + f.x * (c111 - c011);
    } else {
        Float3 c001 = load(x0, y0, z0 + 1), c011 = load(x0, y0 + 1, z0 + 1);
        return c000 + f.z * (c001 - c000) + f.y * (c011 - c001) + f.x * (c111 - c011);
    }
}

// ---- MeasuredReflectanceTable ----

namespace {
std::shared_ptr<const MeasuredReflectanceTable>& installedTable() {
    static std::shared_ptr<const MeasuredReflectanceTable> table;
    return table;
}
}  // namespace

MeasuredReflectanceTable::MeasuredReflectanceTable(int dimension_, int bandCount_, int sourceCount_, float anchor_,
                                                   std::vector<float> data_)
    : dimension(dimension_), bandCount(bandCount_), sourceCount(sourceCount_), anchor(anchor_), data(std::move(data_)) {}

std::shared_ptr<const MeasuredReflectanceTable> MeasuredReflectanceTable::shared() {
    return std::atomic_load(&installedTable());
}

bool MeasuredReflectanceTable::install(const uint8_t* bytes, size_t size) {
    std::shared_ptr<const MeasuredReflectanceTable> table = load(bytes, size);
    if (!table) return false;
    std::atomic_store(&installedTable(), table);
    return true;
}

std::shared_ptr<const MeasuredReflectanceTable> MeasuredReflectanceTable::load(const uint8_t* bytes, size_t size) {
    if (bytes == nullptr || size < 24 || std::memcmp(bytes, "RPR1", 4) != 0) return nullptr;
    auto u32 = [&](size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    auto f32 = [&](size_t offset) {
        uint32_t u = u32(offset);
        float value;
        std::memcpy(&value, &u, sizeof value);
        return value;
    };
    int64_t dimension = u32(4);
    int64_t bandCount = u32(8);
    int64_t sourceCount = u32(12);
    float anchor = f32(16);
    int64_t valueCount = u32(20);
    int64_t expectedCount = 3 * dimension * dimension * bandCount;
    if (!(dimension >= 2 && bandCount == SpectralGrid::count && sourceCount > 0 && anchor > 0 && std::isfinite(anchor) &&
          valueCount == expectedCount && static_cast<int64_t>(size) == 24 + valueCount * 4)) {
        return nullptr;
    }
    size_t offset = 24;
    std::vector<float> data(static_cast<size_t>(valueCount), 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        float value = f32(offset);
        if (!(std::isfinite(value) && value >= 0 && value <= 1)) return nullptr;
        data[i] = value;
        offset += 4;
    }
    return std::make_shared<const MeasuredReflectanceTable>(static_cast<int>(dimension), static_cast<int>(bandCount),
                                                            static_cast<int>(sourceCount), anchor, std::move(data));
}

std::vector<float> MeasuredReflectanceTable::reflectance(Float3 input) const {
    Float3 rgb(swiftMax(input.x, 0.0f), swiftMax(input.y, 0.0f), swiftMax(input.z, 0.0f));
    float peak = swiftMax(rgb.x, rgb.y, rgb.z);
    if (!(peak > 0)) return std::vector<float>(bandCount, 0.0f);
    float rayScale = peak / anchor;
    Float3 anchored = rgb / rayScale;
    int dominant = 0;
    if (anchored.y >= anchored[dominant]) dominant = 1;
    if (anchored.z >= anchored[dominant]) dominant = 2;

    float extent = static_cast<float>(dimension - 1);
    float x = clamp(anchored[(dominant + 1) % 3] / anchor * extent, 0.0f, extent);
    float y = clamp(anchored[(dominant + 2) % 3] / anchor * extent, 0.0f, extent);
    int xi = std::min(static_cast<int>(x), dimension - 2);
    int yi = std::min(static_cast<int>(y), dimension - 2);
    float xf = x - static_cast<float>(xi);
    float yf = y - static_cast<float>(yi);

    auto value = [&](int dx, int dy, int band) {
        size_t index = ((static_cast<size_t>(dominant * dimension + yi + dy) * dimension + xi + dx) * bandCount + band);
        return data[index];
    };
    std::vector<float> spectrum(bandCount, 0.0f);
    for (int band = 0; band < bandCount; ++band) {
        float lower = value(0, 0, band) + xf * (value(1, 0, band) - value(0, 0, band));
        float upper = value(0, 1, band) + xf * (value(1, 1, band) - value(0, 1, band));
        spectrum[band] = swiftMax((lower + yf * (upper - lower)) * rayScale, 0.0f);
    }
    return spectrum;
}

// ---- ReceiverNeutralCalibration ----

ReceiverNeutralCalibration::ReceiverNeutralCalibration(const FilmStock& stock,
                                                       const std::function<Float3(const std::vector<float>&)>& read,
                                                       const std::function<Float3(const std::vector<float>&)>& paperRead) {
    // From past the film base to well past any highlight, in log exposure over mid-grey.
    struct Step {
        Float3 moved;
        Float3 reference;
    };
    std::vector<Step> steps;
    steps.reserve(385);
    for (int i = 0; i <= 384; ++i) {
        float logExposure = -8 + 12 * static_cast<float>(i) / 384;
        std::vector<float> density = {stock.developedDensity(0, logExposure), stock.developedDensity(1, logExposure),
                                      stock.developedDensity(2, logExposure)};
        steps.push_back({read(density), paperRead(density)});
    }
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<Step> sorted = steps;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [channel](const Step& a, const Step& b) { return a.moved[channel] < b.moved[channel]; });
        std::vector<Float2> samples;
        for (const Step& step : sorted) {
            if (samples.empty() || step.moved[channel] > samples.back().x + 1e-5f) {
                samples.push_back(Float2(step.moved[channel], step.reference[channel]));
            }
        }
        wedges.push_back(std::move(samples));
    }
}

Float3 ReceiverNeutralCalibration::operator()(Float3 read) const {
    Float3 out;
    for (int channel = 0; channel < 3; ++channel) {
        const std::vector<Float2>& samples = wedges[channel];
        float v = read[channel];
        if (samples.size() <= 1) {
            out[channel] = v;
            continue;
        }
        const Float2& first = samples.front();
        const Float2& last = samples.back();
        if (v <= first.x) {
            out[channel] = v + first.y - first.x;
            continue;
        }
        if (v >= last.x) {
            out[channel] = v + last.y - last.x;
            continue;
        }
        size_t upper = samples.size() - 1;
        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].x >= v) {
                upper = i;
                break;
            }
        }
        const Float2& a = samples[upper - 1];
        const Float2& b = samples[upper];
        out[channel] = a.y + (b.y - a.y) * (v - a.x) / (b.x - a.x);
    }
    return out;
}

// ---- ScreenToeBalance ----

Float3 ScreenToeBalance::operator()(Float3 read) const {
    if (!wedges) return read;
    Float3 out;
    for (int channel = 0; channel < 3; ++channel) {
        const std::vector<Float2>& samples = (*wedges)[channel];
        float v = read[channel];
        if (samples.empty() || !(samples.back().x > samples.front().x)) {
            out[channel] = v;
            continue;
        }
        const Float2& first = samples.front();
        const Float2& last = samples.back();
        float from = first.x + start * (last.x - first.x);
        if (!(v > from)) {
            out[channel] = v;
            continue;
        }
        float offset = last.y;
        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].x >= v) {
                const Float2& a = samples[i - 1];
                const Float2& b = samples[i];
                offset = a.y + (b.y - a.y) * (v - a.x) / (b.x - a.x);
                break;
            }
        }
        float t = swiftMin((v - from) / (last.x - from), 1.0f);
        out[channel] = v + t * t * (3 - 2 * t) * (offset - first.y);
    }
    return out;
}

// ---- SpectralGrid paper* (SpectralModel.swift) ----

namespace SpectralGrid {
const std::vector<std::vector<float>>& paperDyeAmounts() { return PrintStock::ektacolorEdge().spectra.analyticalDyes; }
const std::vector<std::vector<float>>& paperDyes() { return PrintStock::ektacolorEdge().spectra.dyes; }
const std::vector<std::vector<float>>& paperSensitivity() { return PrintStock::ektacolorEdge().spectra.sensitivity; }
}  // namespace SpectralGrid

}  // namespace fotufilm
