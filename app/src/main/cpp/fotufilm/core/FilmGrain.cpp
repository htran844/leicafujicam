// Port of Sources/FotufilmCore/FilmGrain.swift.
#include "FilmGrain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include "CoreMath.h"
#include "CrystalGrainModel.h"
#include "FilmGrainAsset.h"
#include "ParallelWork.h"

namespace fotufilm {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kLn2 = 0.693147180559945309417232121458176568;

void check(const CheckCancellation& checkCancellation) {
    if (checkCancellation) checkCancellation();
}
}  // namespace

const std::vector<FilmGrain::CloudTerm>& FilmGrain::dyeCloudTerms() {
    static const std::vector<CloudTerm> terms = {
        {0.1162f, 0.1096f}, {0.3089f, 0.2167f}, {0.7469f, 0.4030f}, {1.732f, 0.2510f}};
    return terms;
}

int FilmGrain::markShape() {
    float dispersion = CrystalGrainModel::Development::markDispersion;
    return static_cast<int>(std::round(1 / (dispersion * dispersion)));
}

double FilmGrain::profileValue(Profile profile, double rho) {
    switch (profile) {
        case Profile::silverGrain: return std::exp(-rho * rho / 2);
        case Profile::dyeCloud: {
            double total = 0;
            for (const CloudTerm& term : dyeCloudTerms()) {
                double s = static_cast<double>(term.sigma);
                total = total + static_cast<double>(term.weight) * std::exp(-rho * rho / (2 * s * s));
            }
            return total;
        }
    }
    return 0;
}

double FilmGrain::profileReach(Profile profile, double peak) {
    double widest = profile == Profile::silverGrain ? 1.0 : static_cast<double>(dyeCloudTerms().back().sigma);
    return widest * std::sqrt(2 * std::log(swiftMax(peak * 8, 1.0) / 1e-7));
}

// MARK: - Sublayer

FilmGrain::Sublayer FilmGrain::Sublayer::make(Profile profile, float coatedPerMM2, float capacity, float edge,
                                              float smallestSigmaMM, float dyePerCloudMM2, float cellMM,
                                              std::vector<float> forming) {
    Sublayer layer;
    layer.profile = profile;
    layer.coatedPerMM2 = coatedPerMM2;
    layer.capacity = capacity;
    layer.edge = edge;
    layer.peakDemand = edge * capacity;
    layer.smallestSigmaMM = smallestSigmaMM;
    layer.dyePerCloudMM2 = dyePerCloudMM2;
    layer.cellMM = cellMM;
    layer.forming = std::move(forming);
    layer.sigmaMM = 0;
    layer.voidIntegralMM2 = 0;
    layer.shape();
    return layer;
}

FilmGrain::Sublayer FilmGrain::Sublayer::restored(Profile profile, float coatedPerMM2, float sigmaMM,
                                                  float peakDemand, float capacity, float edge,
                                                  float smallestSigmaMM, float dyePerCloudMM2, float cellMM,
                                                  std::vector<float> forming, float voidIntegralMM2) {
    Sublayer layer;
    layer.profile = profile;
    layer.coatedPerMM2 = coatedPerMM2;
    layer.sigmaMM = sigmaMM;
    layer.peakDemand = peakDemand;
    layer.capacity = capacity;
    layer.edge = edge;
    layer.smallestSigmaMM = smallestSigmaMM;
    layer.dyePerCloudMM2 = dyePerCloudMM2;
    layer.cellMM = cellMM;
    layer.forming = std::move(forming);
    layer.voidIntegralMM2 = voidIntegralMM2;
    return layer;
}

void FilmGrain::Sublayer::shape() {
    float peak = edge;
    float free = profile == Profile::silverGrain
                     ? std::sqrt(dyePerCloudMM2 / FilmGrain::unitCloudDye(capacity, edge, profile))
                     : 0.0f;
    if (free >= smallestSigmaMM) {
        sigmaMM = free;
    } else {
        sigmaMM = smallestSigmaMM;
        float wanted = std::log(dyePerCloudMM2 / (smallestSigmaMM * smallestSigmaMM));
        auto miss = [&](float x) {
            return std::log(FilmGrain::unitCloudDye(capacity, std::exp(x), profile)) - wanted;
        };
        float low = profile == Profile::silverGrain ? std::log(edge * 1e-5f) : std::log(1e-6f);
        float high = profile == Profile::silverGrain ? std::log(edge) : std::log(1e4f);
        float fLow = miss(low), fHigh = miss(high);
        float x = fLow >= 0 ? low : high;
        if (fLow < 0 && fHigh > 0) {
            int side = 0;
            for (int i = 0; i < 40; ++i) {
                x = (low * fHigh - high * fLow) / (fHigh - fLow);
                float fx = miss(x);
                if (std::fabs(fx) < 1e-6f || high - low < 1e-6f) break;
                if (fx < 0) {
                    low = x;
                    fLow = fx;
                    if (side == -1) fHigh /= 2;
                    side = -1;
                } else {
                    high = x;
                    fHigh = fx;
                    if (side == 1) fLow /= 2;
                    side = 1;
                }
            }
        }
        peak = std::exp(x);
    }
    peakDemand = peak * capacity;
    voidIntegralMM2 = sigmaMM * sigmaMM * FilmGrain::unitVoidIntegral(peak, profile);
}

float FilmGrain::Sublayer::halfCapacityRadiusMM() const {
    double formed = kLn2;
    double ratio = static_cast<double>(peakDemand / swiftMax(capacity, 1e-6f));
    if (!(ratio > formed)) return 0;
    double low = 0.0, high = profileReach(profile, ratio);
    for (int i = 0; i < 50; ++i) {
        double mid = (low + high) / 2;
        if (ratio * profileValue(profile, mid) > formed) low = mid; else high = mid;
    }
    return sigmaMM * static_cast<float>((low + high) / 2);
}

// MARK: - Population

FilmGrain::FilmGrain(std::vector<Record> records_, bool monochrome_, const std::string& identity,
                     std::shared_ptr<const Tiles> tiles)
    : records(std::move(records_)), monochrome(monochrome_), key(populationKey(identity, 1)),
      assetTiles(std::move(tiles)) {}

FilmGrain::FilmGrain(const FilmStock& stock, const FilmStock* reference, float grainScale, bool useCachedAnchor,
                     const CheckCancellation& checkCancellation) {
    check(checkCancellation);
    monochrome = stock.isMonochrome;
    key = populationKey(FilmGrainAsset::identity(stock, reference), grainScale);
    bool silver = stock.grainDensityLaw == GrainDensityLaw::silver;
    std::vector<Anchor> anchors;
    for (int layer = 0; layer < 3; ++layer) {
        CrystalGrainModel model(stock, reference, layer, grainScale);
        anchors.push_back(Anchor{stock.curves[layer].density(model.logExposure(model.readNetDensity)), model.readSigma});
        const CharacteristicCurve& curve = stock.curves[layer];
        float lo = curve.dMin, hi = swiftMax(curve.dMax(), curve.dMin + 1e-3f);
        std::vector<std::vector<float>> fractions;
        for (int i = 0; i < tableSamples; ++i) {
            float density = lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(tableSamples - 1);
            fractions.push_back(model.binFractions(curve.logExposure(density)));
        }
        std::vector<Sublayer> sublayers;
        float fastest = 1;
        for (const auto& bin : model.bins()) {
            if (bin.cloudRadiusMM > 0) {
                fastest = bin.cloudRadiusMM;
                break;
            }
        }
        for (size_t b = 0; b < model.bins().size(); ++b) {
            const auto& bin = model.bins()[b];
            if (!(bin.crystalsPerMM2 > 0 && bin.dyePerCloud > 0)) continue;
            float edge = silver ? silverGrainEdge : 1.0f;
            float smallestSigma =
                silver ? fastestCrystalMM * bin.cloudRadiusMM / fastest /
                             (2 * std::sqrt(2 * std::log(edge / static_cast<float>(kLn2))))
                       : dyeCloudDecayMM;
            float cell = swiftMin(swiftMax(std::sqrt(8 / bin.crystalsPerMM2), 0.00025f), 0.004f);
            std::vector<float> forming;
            for (const auto& row : fractions) forming.push_back(row[b]);
            sublayers.push_back(Sublayer::make(silver ? Profile::silverGrain : Profile::dyeCloud, bin.crystalsPerMM2,
                                               silver ? silverGrainDensity : swiftMax(bin.pool, 0.05f), edge,
                                               smallestSigma, bin.dyePerCloud, cell, std::move(forming)));
        }
        records.push_back(Record{std::move(sublayers), lo, hi});
    }
    anchorToSheet(anchors, key, useCachedAnchor, checkCancellation);
}

namespace {

std::string base64(const std::string& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < bytes.size()) {
        uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                     static_cast<uint8_t>(bytes[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
        i += 3;
    }
    size_t rest = bytes.size() - i;
    if (rest == 1) {
        uint32_t v = static_cast<uint8_t>(bytes[i]) << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += "==";
    } else if (rest == 2) {
        uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

/// Solved records, per stock and grain scale (the last four).
struct AnchorCache {
    std::mutex lock;
    std::deque<std::pair<std::string, std::vector<FilmGrain::Record>>> entries;
};
AnchorCache& anchorCache() {
    static AnchorCache& cache = *new AnchorCache;
    return cache;
}

}  // namespace

std::string FilmGrain::anchorKey(const FilmStock& stock, float grainScale) {
    return populationKey(FilmGrainAsset::identity(stock, nullptr), grainScale);
}

std::string FilmGrain::populationKey(const std::string& identity, float grainScale) {
    uint32_t bits;
    std::memcpy(&bits, &grainScale, sizeof bits);
    return base64(identity) + "|" + std::to_string(bits);
}

void FilmGrain::anchorToSheet(const std::vector<Anchor>& anchors, const std::string& cacheKey, bool useCache,
                              const CheckCancellation& checkCancellation) {
    check(checkCancellation);
    if (useCache) {
        AnchorCache& cache = anchorCache();
        std::lock_guard<std::mutex> guard(cache.lock);
        for (const auto& entry : cache.entries) {
            if (entry.first == cacheKey) {
                records = entry.second;
                return;
            }
        }
    }
    std::vector<float> solved(3, 1.0f);
    std::vector<int> active = monochrome ? std::vector<int>{1} : std::vector<int>{0, 1, 2};
    for (int r : active) {
        if (records[r].sublayers.empty() || !(anchors[r].sigma > 0)) continue;
        float slope = 0.5f;
        std::optional<std::pair<float, float>> last;
        for (int pass = 0; pass < anchorPasses; ++pass) {
            check(checkCancellation);
            float measured = sigma48(r, anchors[r].gross);
            check(checkCancellation);
            if (!(measured > 0)) break;
            float miss = std::log(anchors[r].sigma / measured);
            if (std::fabs(miss) < anchorTolerance) break;
            if (last && std::fabs(std::log(solved[r] / last->first)) > 1e-3f) {
                slope = swiftMin(swiftMax(std::log(measured / last->second) / std::log(solved[r] / last->first), 0.25f),
                                 1.5f);
            }
            last = std::make_pair(solved[r], measured);
            float step = std::exp(miss / slope);
            scale(r, step);
            solved[r] *= step;
        }
    }
    if (monochrome) {
        solved[0] = solved[1];
        solved[2] = solved[1];
        for (int r : {0, 2}) scale(r, solved[r]);
    }
    check(checkCancellation);
    if (useCache) {
        AnchorCache& cache = anchorCache();
        std::lock_guard<std::mutex> guard(cache.lock);
        cache.entries.erase(std::remove_if(cache.entries.begin(), cache.entries.end(),
                                           [&](const auto& entry) { return entry.first == cacheKey; }),
                            cache.entries.end());
        cache.entries.emplace_back(cacheKey, records);
        if (cache.entries.size() > 4) cache.entries.pop_front();
    }
}

void FilmGrain::scale(int r, float factor) {
    if (!(factor > 0) || factor == 1) return;
    for (Sublayer& layer : records[r].sublayers) {
        layer.coatedPerMM2 /= factor;
        layer.dyePerCloudMM2 *= factor;
        layer.shape();
    }
}

float FilmGrain::sigma48(int r, float gross) const { return tileSigma48(tileLight(r, gross, tileSeed)); }

float FilmGrain::unitCloudDye(float capacity, float edge, Profile profile) {
    double shape = static_cast<double>(markShape());
    const int quantiles = 24;
    std::vector<double> marks;
    for (int i = 0; i < quantiles; ++i) {
        marks.push_back(gammaQuantile((static_cast<double>(i) + 0.5) / static_cast<double>(quantiles), shape) / shape);
    }
    const int steps = 800;
    double top = profileReach(profile, static_cast<double>(edge) * (marks.empty() ? 1.0 : marks.back()));
    double dr = top / static_cast<double>(steps);
    std::vector<double> radii(steps), values(steps);
    for (int i = 0; i < steps; ++i) {
        radii[i] = (static_cast<double>(i) + 0.5) * dr;
        values[i] = profileValue(profile, radii[i]);
    }
    double total = 0.0;
    for (double mark : marks) {
        double peak = static_cast<double>(edge) * mark;
        double area = 0.0;
        for (int i = 0; i < steps; ++i) {
            double dye = static_cast<double>(capacity) * (1 - std::exp(-peak * values[i]));
            area += (1 - std::exp(-2.302585092994046 * dye)) * radii[i] * dr;
        }
        total += 0.4342944819 * 2 * kPi * area;
    }
    return static_cast<float>(total / static_cast<double>(quantiles));
}

float FilmGrain::unitVoidIntegral(float edge, Profile profile) {
    double shape = static_cast<double>(markShape());
    const int quantiles = 24;
    double total = 0.0;
    for (int q = 0; q < quantiles; ++q) {
        double mark = gammaQuantile((static_cast<double>(q) + 0.5) / static_cast<double>(quantiles), shape) / shape;
        total += static_cast<double>(occupiedArea(edge * static_cast<float>(mark), profile));
    }
    return static_cast<float>(total / static_cast<double>(quantiles));
}

float FilmGrain::occupiedArea(float peak, Profile profile) {
    double p = static_cast<double>(swiftMax(peak, 0.0f));
    if (profile != Profile::silverGrain) {
        const int steps = 800;
        double dr = profileReach(profile, p) / static_cast<double>(steps);
        double area = 0.0;
        for (int i = 0; i < steps; ++i) {
            double rho = (static_cast<double>(i) + 0.5) * dr;
            area += (1 - std::exp(-p * profileValue(profile, rho))) * rho * dr;
        }
        return static_cast<float>(2 * kPi * area);
    }
    double ein;
    if (p < 6) {
        double term = p, sum = 0.0;
        for (int k = 1; k <= 60; ++k) {
            sum += term / static_cast<double>(k);
            term *= -p / static_cast<double>(k + 1);
            if (std::fabs(term) < 1e-14) break;
        }
        ein = sum;
    } else {
        double e1 = std::exp(-p) / p * (1 - 1 / p + 2 / (p * p) - 6 / (p * p * p));
        ein = e1 + std::log(p) + 0.5772156649;
    }
    return static_cast<float>(2 * kPi * ein);
}

float FilmGrain::pointMean(int r, float gross) const {
    const Record& record = records[r];
    float t = swiftMin(swiftMax((gross - record.dMin) / swiftMax(record.dMax - record.dMin, 1e-6f), 0.0f), 1.0f) *
              static_cast<float>(tableSamples - 1);
    int i = std::min(static_cast<int>(t), tableSamples - 2);
    float f = t - static_cast<float>(i);
    float total = 0;
    for (const Sublayer& layer : record.sublayers) {
        float fraction = layer.forming[i] * (1 - f) + layer.forming[i + 1] * f;
        float developed = layer.coatedPerMM2 * fraction;
        total += layer.capacity * (1 - std::exp(-developed * layer.voidIntegralMM2));
    }
    return total;
}

double FilmGrain::gammaQuantile(double p, double shape) {
    int k = static_cast<int>(shape);
    auto cdf = [k](double x) {
        double term = 1.0, sum = 1.0;
        if (k > 1) {
            for (int n = 1; n < k; ++n) {
                term *= x / static_cast<double>(n);
                sum += term;
            }
        }
        return 1 - std::exp(-x) * sum;
    };
    double lo = 0.0, hi = shape * 20;
    for (int i = 0; i < 80; ++i) {
        double mid = (lo + hi) / 2;
        if (cdf(mid) < p) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
}

int FilmGrain::supersample(float pxPerMM) {
    float pitch = 1 / swiftMax(pxPerMM, 1e-6f);
    return std::min(std::max(static_cast<int>(std::ceil(pitch / finestSampleMM)), 1), maxSupersample);
}

float FilmGrain::lookup(const std::vector<float>& table, float gross, float lo, float hi) {
    float t = swiftMin(swiftMax((gross - lo) / swiftMax(hi - lo, 1e-6f), 0.0f), 1.0f) *
              static_cast<float>(table.size() - 1);
    int i = std::min(static_cast<int>(t), static_cast<int>(table.size()) - 2);
    float f = t - static_cast<float>(i);
    return table[i] * (1 - f) + table[i + 1] * f;
}

float FilmGrain::bilinear(const std::vector<float>& plane, int width, int height, float x, float y) {
    float fx = swiftMin(swiftMax(x, 0.0f), static_cast<float>(width - 1));
    float fy = swiftMin(swiftMax(y, 0.0f), static_cast<float>(height - 1));
    int x0 = std::min(static_cast<int>(fx), std::max(width - 2, 0));
    int y0 = std::min(static_cast<int>(fy), std::max(height - 2, 0));
    int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
    float top = plane[y0 * width + x0] * (1 - ax) + plane[y0 * width + x1] * ax;
    float bottom = plane[y1 * width + x0] * (1 - ax) + plane[y1 * width + x1] * ax;
    return top * (1 - ay) + bottom * ay;
}

std::vector<float> FilmGrain::meanTable(int r, float pxPerMM, int supersampleCount, uint64_t seed) const {
    const Record& record = records[r];
    float pitch = 1 / pxPerMM;
    int side = std::min(std::max(static_cast<int>(std::round(biasPatchMM / pitch)), 24), 1024);
    std::vector<float> table;
    for (int i = 0; i < biasSamples; ++i) {
        float gross = record.dMin + (record.dMax - record.dMin) * static_cast<float>(i) /
                                        static_cast<float>(biasSamples - 1);
        std::vector<float> point;
        std::vector<float> field = render(r, side, side, pxPerMM, supersampleCount, seed ^ 0xB1A5CA11B1A5CA11ull,
                                          [gross](float, float) { return gross; }, &point);
        float correction = 0;
        for (size_t k = 0; k < field.size(); ++k) correction += field[k] - point[k];
        table.push_back(pointMean(r, gross) + correction / static_cast<float>(field.size()));
    }
    return table;
}

std::vector<float> FilmGrain::render(int r, int width, int height, float pxPerMM, int supersampleCount,
                                     uint64_t seed, const GrossAt& grossAt, std::vector<float>* pointMeans,
                                     std::optional<float> periodMM) const {
    const int tile = 48;
    int tilesX = (width + tile - 1) / tile, tilesY = (height + tile - 1) / tile;
    std::vector<float> output(static_cast<size_t>(width) * height, 0.0f);
    std::vector<float> means(pointMeans ? static_cast<size_t>(width) * height : 1, 0.0f);
    const Record& record = records[r];
    float* out = output.data();
    float* meanOut = pointMeans ? means.data() : nullptr;
    ParallelWork::forEach(tilesX * tilesY, [&](int index) {
        int tx = index % tilesX, ty = index / tilesX;
        int x0 = tx * tile, y0 = ty * tile;
        int tw = std::min(tile, width - x0), th = std::min(tile, height - y0);
        renderTile(record, r, x0, y0, tw, th, pxPerMM, supersampleCount, seed, grossAt, out, meanOut, width, periodMM);
    });
    if (pointMeans) *pointMeans = means;
    return output;
}

void FilmGrain::renderTile(const Record& record, int recordIndex, int x0, int y0, int tw, int th, float pxPerMM,
                           int s, uint64_t seed, const GrossAt& grossAt, float* output, float* means, int width,
                           std::optional<float> periodMM) {
    float pitch = 1 / pxPerMM;
    float h = pitch / static_cast<float>(s);
    int sw = tw * s, sh = th * s;
    float fx0 = static_cast<float>(x0) * pitch, fy0 = static_cast<float>(y0) * pitch;
    const float ln10 = 2.302585093f;
    size_t samples = static_cast<size_t>(sw) * sh;
    std::vector<float> logTransmit(samples, 0.0f);
    std::vector<float> density(samples, 0.0f);
    std::vector<float> demand(samples, 0.0f);
    std::vector<float> logUncovered(samples, 0.0f);
    float tableScale = static_cast<float>(tableSamples - 1) / swiftMax(record.dMax - record.dMin, 1e-6f);
    int shape = markShape();
    const float widestTerm = dyeCloudTerms().back().sigma;

    for (size_t b = 0; b < record.sublayers.size(); ++b) {
        const Sublayer& layer = record.sublayers[b];
        std::fill(demand.begin(), demand.end(), 0.0f);
        std::fill(logUncovered.begin(), logUncovered.end(), 0.0f);
        float sigma = layer.sigmaMM;
        float edge = layer.peakDemand / layer.capacity;
        float resolvedShare = swiftMin(swiftMax((sigma / h - 0.5f) / 1.5f, 0.0f), 1.0f);
        bool dye = layer.profile == Profile::dyeCloud;
        int margin = dye ? static_cast<int>(std::ceil(3.5f * widestTerm * sigma / h)) + 1 : 0;
        int gw = sw + 2 * margin, gh = sh + 2 * margin;
        std::vector<float> deposits(dye ? static_cast<size_t>(gw) * gh : 0, 0.0f);
        float reach = dye ? static_cast<float>(margin) * h : (resolvedShare > 0 ? 4.2f * sigma : 3 * sigma + h);
        std::optional<int> periodCells;
        if (periodMM) periodCells = std::max(static_cast<int>(std::round(*periodMM / layer.cellMM)), 1);
        float cell = periodMM ? *periodMM / static_cast<float>(*periodCells) : layer.cellMM;
        std::vector<float> thresholds = FilmRandom::countThresholds(layer.coatedPerMM2 * cell * cell);
        int cx0 = static_cast<int>(std::floor((fx0 - reach) / cell));
        int cx1 = static_cast<int>(std::floor((fx0 + static_cast<float>(sw) * h + reach) / cell));
        int cy0 = static_cast<int>(std::floor((fy0 - reach) / cell));
        int cy1 = static_cast<int>(std::floor((fy0 + static_cast<float>(sh) * h + reach) / cell));
        float inverseTwoSigma2 = 1 / (2 * sigma * sigma);
        for (int cy = cy0; cy <= cy1; ++cy) {
            for (int cx = cx0; cx <= cx1; ++cx) {
                int hx = periodCells ? ((cx % *periodCells) + *periodCells) % *periodCells : cx;
                int hy = periodCells ? ((cy % *periodCells) + *periodCells) % *periodCells : cy;
                FilmRandom counter(seed, recordIndex, static_cast<int>(b), 1, hx, hy);
                int count = FilmRandom::count(counter.uniform(), thresholds);
                FilmRandom rng(seed, recordIndex, static_cast<int>(b), 0, hx, hy);
                for (int n = 0; n < count; ++n) {
                    float px = (static_cast<float>(cx) + rng.uniform()) * cell;
                    float py = (static_cast<float>(cy) + rng.uniform()) * cell;
                    float develop = rng.uniform();
                    float mark = 0;
                    for (int m = 0; m < shape; ++m) mark -= std::log(swiftMax(rng.uniform(), 1e-7f));
                    mark /= static_cast<float>(shape);
                    float gross = grossAt(px * pxPerMM - 0.5f, py * pxPerMM - 0.5f);
                    float t = swiftMin(swiftMax((gross - record.dMin) * tableScale, 0.0f),
                                       static_cast<float>(tableSamples - 1));
                    int ti = std::min(static_cast<int>(t), tableSamples - 2);
                    float tf = t - static_cast<float>(ti);
                    float fraction = layer.forming[ti] * (1 - tf) + layer.forming[ti + 1] * tf;
                    if (!(develop < fraction)) continue;
                    float lx = (px - fx0) / h, ly = (py - fy0) / h;
                    if (dye) {
                        float u = lx - 0.5f + static_cast<float>(margin), v = ly - 0.5f + static_cast<float>(margin);
                        int i0 = static_cast<int>(std::floor(u)), j0 = static_cast<int>(std::floor(v));
                        float au = u - static_cast<float>(i0), av = v - static_cast<float>(j0);
                        float amount = edge * mark;
                        const float wys[2] = {1 - av, av};
                        const float wxs[2] = {1 - au, au};
                        for (int dj = 0; dj < 2; ++dj) {
                            int j = j0 + dj;
                            if (!(j >= 0 && j < gh)) continue;
                            for (int di = 0; di < 2; ++di) {
                                int i = i0 + di;
                                if (!(i >= 0 && i < gw)) continue;
                                deposits[static_cast<size_t>(j) * gw + i] += amount * wxs[di] * wys[dj];
                            }
                        }
                        continue;
                    }
                    if (resolvedShare > 0) {
                        float span = reach / h;
                        int ix0 = std::max(static_cast<int>(std::floor(lx - span)), 0);
                        int ix1 = std::min(static_cast<int>(std::ceil(lx + span)), sw - 1);
                        int iy0 = std::max(static_cast<int>(std::floor(ly - span)), 0);
                        int iy1 = std::min(static_cast<int>(std::ceil(ly + span)), sh - 1);
                        if (ix0 <= ix1 && iy0 <= iy1) {
                            float peak = edge * mark * resolvedShare;
                            for (int j = iy0; j <= iy1; ++j) {
                                float dy = (static_cast<float>(j) + 0.5f - ly) * h;
                                float rowFactor = peak * std::exp(-dy * dy * inverseTwoSigma2);
                                if (rowFactor < 1e-5f) continue;
                                size_t base = static_cast<size_t>(j) * sw;
                                for (int i = ix0; i <= ix1; ++i) {
                                    float dx = (static_cast<float>(i) + 0.5f - lx) * h;
                                    demand[base + i] += rowFactor * std::exp(-dx * dx * inverseTwoSigma2);
                                }
                            }
                        }
                    }
                    if (resolvedShare < 1) {
                        float covering = 1 - resolvedShare;
                        float side = sigma * std::sqrt(occupiedArea(edge * mark, layer.profile)) / h;
                        float left = lx - side / 2, right = lx + side / 2;
                        float top = ly - side / 2, bottom = ly + side / 2;
                        int ix0 = std::max(static_cast<int>(std::floor(left)), 0);
                        int ix1 = std::min(static_cast<int>(std::floor(right)), sw - 1);
                        int iy0 = std::max(static_cast<int>(std::floor(top)), 0);
                        int iy1 = std::min(static_cast<int>(std::floor(bottom)), sh - 1);
                        if (!(ix0 <= ix1 && iy0 <= iy1)) continue;
                        for (int j = iy0; j <= iy1; ++j) {
                            float oy = swiftMin(bottom, static_cast<float>(j + 1)) - swiftMax(top, static_cast<float>(j));
                            if (oy <= 0) continue;
                            size_t base = static_cast<size_t>(j) * sw;
                            for (int i = ix0; i <= ix1; ++i) {
                                float ox = swiftMin(right, static_cast<float>(i + 1)) - swiftMax(left, static_cast<float>(i));
                                if (ox <= 0) continue;
                                logUncovered[base + i] += std::log(swiftMax(1 - covering * ox * oy, 1e-6f));
                            }
                        }
                    }
                }
            }
        }
        if (dye) {
            for (const CloudTerm& term : dyeCloudTerms()) {
                layCloudTerm(deposits.data(), gw, gh, margin, demand.data(), sw, sh, term.sigma * sigma / h,
                             term.weight);
            }
        }
        float capacity = layer.capacity;
        float full = std::exp(-ln10 * capacity);
        for (size_t i = 0; i < samples; ++i) {
            float resolvedDye = demand[i] > 0 ? capacity * (1 - std::exp(-demand[i])) : 0.0f;
            float uncovered = logUncovered[i] < 0 ? std::exp(logUncovered[i]) : 1.0f;
            if (resolvedDye == 0 && uncovered == 1) continue;
            density[i] += resolvedDye * uncovered + capacity * (1 - uncovered);
            float passed = uncovered * std::exp(-ln10 * resolvedDye) + (1 - uncovered) * full;
            logTransmit[i] += std::log(swiftMax(passed, 1e-12f));
        }
    }
    float norm = 1 / static_cast<float>(s * s);
    for (int py = 0; py < th; ++py) {
        for (int px = 0; px < tw; ++px) {
            float transmitted = 0, averaged = 0;
            for (int j = 0; j < s; ++j) {
                size_t base = static_cast<size_t>(py * s + j) * sw + static_cast<size_t>(px) * s;
                for (int i = 0; i < s; ++i) {
                    transmitted += std::exp(logTransmit[base + i]);
                    averaged += density[base + i];
                }
            }
            size_t at = static_cast<size_t>(y0 + py) * width + x0 + px;
            output[at] = -std::log10(swiftMax(transmitted * norm, 1e-6f));
            if (means) means[at] = averaged * norm;
        }
    }
}

void FilmGrain::layCloudTerm(const float* deposits, int gw, int gh, int margin, float* demand, int sw, int sh,
                             float sigma, float weight) {
    float scale = weight * 2 * kFloatPi * sigma * sigma;
    int factor = std::max(static_cast<int>(sigma / 1.6f), 1);
    if (!(factor > 1)) {
        std::vector<float> kernel = gaussianKernel(sigma);
        int radius = static_cast<int>(kernel.size()) / 2;
        std::vector<float> across(static_cast<size_t>(gh) * sw, 0.0f);
        blurRows(deposits, gw, margin - radius, gh, sw, kernel, across.data());
        blurColumns(across.data(), sw, margin - radius, sh, kernel, scale, demand);
        return;
    }
    float f = static_cast<float>(factor);
    float coarse = std::sqrt(sigma * sigma - f * f / 3) / f;
    std::vector<float> kernel = gaussianKernel(coarse);
    int radius = static_cast<int>(kernel.size()) / 2;
    int cw = (gw + factor - 1) / factor + 2 * radius + 1;
    int ch = (gh + factor - 1) / factor + 2 * radius + 1;
    std::vector<float> cells(static_cast<size_t>(cw) * ch, 0.0f);
    int places = std::max(gw, gh);
    std::vector<int> placeIndex(places);
    std::vector<float> placeFraction(places);
    for (int x = 0; x < places; ++x) {
        float u = (static_cast<float>(x) + 0.5f) / f - 0.5f;
        int i = static_cast<int>(std::floor(u));
        placeIndex[x] = i + radius;
        placeFraction[x] = u - static_cast<float>(i);
    }
    for (int y = 0; y < gh; ++y) {
        int j = placeIndex[y];
        float av = placeFraction[y];
        for (int x = 0; x < gw; ++x) {
            float mass = deposits[static_cast<size_t>(y) * gw + x];
            if (mass == 0) continue;
            int i = placeIndex[x];
            float au = placeFraction[x];
            cells[static_cast<size_t>(j) * cw + i] += mass * (1 - au) * (1 - av);
            cells[static_cast<size_t>(j) * cw + i + 1] += mass * au * (1 - av);
            cells[static_cast<size_t>(j + 1) * cw + i] += mass * (1 - au) * av;
            cells[static_cast<size_t>(j + 1) * cw + i + 1] += mass * au * av;
        }
    }
    std::vector<float> across(static_cast<size_t>(ch) * cw, 0.0f);
    std::vector<float> blurred(static_cast<size_t>(ch) * cw, 0.0f);
    blurRows(cells.data(), cw, 0, ch, cw - 2 * radius, kernel, across.data(), cw, radius);
    blurColumns(across.data(), cw, 0, ch - 2 * radius, kernel, 1, blurred.data() + static_cast<size_t>(radius) * cw,
                false);
    float perSample = scale / (f * f);
    for (int j = 0; j < sh; ++j) {
        float v = (static_cast<float>(j + margin) + 0.5f) / f - 0.5f + static_cast<float>(radius);
        int j0 = std::min(std::max(static_cast<int>(std::floor(v)), 0), ch - 2);
        float av = v - static_cast<float>(j0);
        for (int i = 0; i < sw; ++i) {
            float u = (static_cast<float>(i + margin) + 0.5f) / f - 0.5f + static_cast<float>(radius);
            int i0 = std::min(std::max(static_cast<int>(std::floor(u)), 0), cw - 2);
            float au = u - static_cast<float>(i0);
            float top = blurred[static_cast<size_t>(j0) * cw + i0] * (1 - au) +
                        blurred[static_cast<size_t>(j0) * cw + i0 + 1] * au;
            float bottom = blurred[static_cast<size_t>(j0 + 1) * cw + i0] * (1 - au) +
                           blurred[static_cast<size_t>(j0 + 1) * cw + i0 + 1] * au;
            demand[static_cast<size_t>(j) * sw + i] += perSample * (top * (1 - av) + bottom * av);
        }
    }
}

int FilmGrain::tileTermFactor(float sigma, int samples) {
    int factor = std::max(static_cast<int>(sigma / 1.6f), 1);
    while (factor > 1 && samples % factor != 0) --factor;
    return factor;
}

std::vector<float> FilmGrain::gaussianKernel(float sigma) {
    int radius = std::max(static_cast<int>(std::ceil(3.5f * sigma)), 1);
    std::vector<float> kernel;
    for (int t = -radius; t <= radius; ++t) kernel.push_back(std::exp(-static_cast<float>(t * t) / (2 * sigma * sigma)));
    float total = 0;
    for (float v : kernel) total = total + v;
    for (float& v : kernel) v = v / total;
    return kernel;
}

void FilmGrain::blurRows(const float* input, int rowStride, int firstColumn, int rows, int columns,
                         const std::vector<float>& kernel, float* output, std::optional<int> outputStride,
                         int outputOffset) {
    int stride = outputStride.value_or(columns);
    for (int j = 0; j < rows; ++j) {
        float* out = output + static_cast<size_t>(j) * stride + outputOffset;
        std::fill(out, out + columns, 0.0f);
        const float* row = input + static_cast<ptrdiff_t>(j) * rowStride + firstColumn;
        for (size_t t = 0; t < kernel.size(); ++t) {
            float weight = kernel[t];
            const float* source = row + t;
            for (int i = 0; i < columns; ++i) out[i] += weight * source[i];
        }
    }
}

void FilmGrain::blurColumns(const float* input, int columns, int firstRow, int rows, const std::vector<float>& kernel,
                            float scale, float* output, bool accumulate) {
    std::vector<float> line(columns, 0.0f);
    float* sum = line.data();
    for (int j = 0; j < rows; ++j) {
        std::fill(sum, sum + columns, 0.0f);
        for (size_t t = 0; t < kernel.size(); ++t) {
            float weight = kernel[t];
            const float* source = input + static_cast<ptrdiff_t>(firstRow + j + static_cast<int>(t)) * columns;
            for (int i = 0; i < columns; ++i) sum[i] += weight * source[i];
        }
        float* out = output + static_cast<size_t>(j) * columns;
        if (accumulate) {
            for (int i = 0; i < columns; ++i) out[i] += scale * sum[i];
        } else {
            for (int i = 0; i < columns; ++i) out[i] = scale * sum[i];
        }
    }
}

// MARK: - FilmRandom

FilmRandom::FilmRandom(uint64_t seed, int record, int sublayer, int stream, int x, int y) {
    uint32_t base = seed32(seed) ^ (static_cast<uint32_t>(record * 8 + sublayer * 2 + stream) * 0x9E3779B9u);
    key_ = pcgHash(static_cast<uint32_t>(x) ^ pcgHash(static_cast<uint32_t>(y) ^ pcgHash(base)));
}

uint32_t FilmRandom::seed32(uint64_t seed) {
    return pcgHash(static_cast<uint32_t>(seed)) ^ static_cast<uint32_t>(seed >> 32);
}

uint32_t FilmRandom::next() {
    uint32_t h = pcgHash(key_ ^ (counter_ * 0x9E3779B9u));
    counter_ += 1;
    return h;
}

float FilmRandom::uniform() { return (static_cast<float>(next() >> 8) + 0.5f) * (1.0f / 16777216.0f); }

std::vector<float> FilmRandom::countThresholds(float mean) {
    double m = static_cast<double>(swiftMax(mean, 0.0f));
    double term = std::exp(-m), total = term;
    std::vector<float> result;
    for (int k = 0; k < maxCount; ++k) {
        if (k > 0) {
            term *= m / static_cast<double>(k);
            total += term;
        }
        result.push_back(static_cast<float>(swiftMin(total, 1.0)));
    }
    return result;
}

int FilmRandom::count(float u, const std::vector<float>& thresholds) {
    int n = 0;
    for (float t : thresholds) {
        if (t < u) n += 1;
    }
    return n;
}

}  // namespace fotufilm
