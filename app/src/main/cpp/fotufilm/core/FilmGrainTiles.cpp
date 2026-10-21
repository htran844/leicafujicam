// Port of Sources/FotufilmCore/FilmGrainTiles.swift. The builder chain [.metal, .halide] becomes
// FilmTileBuild (the Halide pipeline as C++), with the reference render laying whatever it cannot.
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>

#include "CoreMath.h"
#include "FilmGrain.h"
#include "FilmGrainAsset.h"
#include "FilmTileBuild.h"
#include "ParallelWork.h"

namespace fotufilm {

namespace {
constexpr double kPi = 3.141592653589793;

void check(const CheckCancellation& checkCancellation) {
    if (checkCancellation) checkCancellation();
}
}  // namespace

// MARK: - Tiles

FilmGrain::Tiles::Tiles(std::vector<std::vector<TileLevel>> levels_, std::vector<float> dMin_,
                        std::vector<float> dMax_)
    : levels(std::move(levels_)), dMin(std::move(dMin_)), dMax(std::move(dMax_)) {}

FilmGrain::PitchTables FilmGrain::Tiles::tables(float pitch, const std::function<PitchTables()>& measure) const {
    {
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& entry : pitches_) {
            if (entry.first == pitch) return entry.second;
        }
    }
    PitchTables measured = measure();
    std::lock_guard<std::mutex> guard(lock_);
    pitches_.erase(std::remove_if(pitches_.begin(), pitches_.end(), [&](const auto& e) { return e.first == pitch; }),
                   pitches_.end());
    pitches_.emplace_back(pitch, measured);
    if (pitches_.size() > 8) pitches_.erase(pitches_.begin());
    return measured;
}

std::vector<float> FilmGrain::Tiles::packed() const {
    const size_t entries = static_cast<size_t>(tileSide + 1) * (tileSide + 1);
    std::vector<float> out(entries * tileLevels * 3, 0.0f);
    for (int r = 0; r < 3; ++r) {
        for (size_t k = 0; k < levels[r].size(); ++k) {
            size_t start = (static_cast<size_t>(r) * tileLevels + k) * entries;
            std::copy(levels[r][k].sums.begin(), levels[r][k].sums.end(), out.begin() + start);
        }
    }
    return out;
}

namespace {

struct TileCache {
    std::mutex lock;
    std::deque<std::pair<std::string, std::shared_ptr<const FilmGrain::Tiles>>> entries;
};
TileCache& tileCache() {
    static TileCache& cache = *new TileCache;
    return cache;
}

}  // namespace

std::shared_ptr<const FilmGrain::Tiles> FilmGrain::tiles(const CheckCancellation& checkCancellation) const {
    check(checkCancellation);
    if (assetTiles) return assetTiles;
    TileCache& cache = tileCache();
    {
        std::lock_guard<std::mutex> guard(cache.lock);
        for (const auto& entry : cache.entries) {
            if (entry.first == key) return entry.second;
        }
    }
    std::shared_ptr<const Tiles> built = buildTiles(tileSeed, true, checkCancellation);
    std::lock_guard<std::mutex> guard(cache.lock);
    cache.entries.erase(std::remove_if(cache.entries.begin(), cache.entries.end(),
                                       [&](const auto& entry) { return entry.first == key; }),
                        cache.entries.end());
    cache.entries.emplace_back(key, built);
    if (cache.entries.size() > 4) cache.entries.pop_front();
    return built;
}

std::vector<float> FilmGrain::tileLight(int r, float gross, uint64_t seed) const {
    std::optional<std::vector<std::vector<float>>> built = builtLights(r, {gross}, seed);
    if (built) return (*built)[0];
    return referenceTileLight(r, gross, seed);
}

std::vector<float> FilmGrain::referenceTileLight(int r, float gross, uint64_t seed) const {
    const int side = tileSide;
    float pxPerMM = 1 / tileTexelMM;
    std::vector<float> field = render(r, side, side, pxPerMM, supersample(pxPerMM), seed,
                                      [gross](float, float) { return gross; }, nullptr,
                                      static_cast<float>(side) * tileTexelMM);
    for (float& v : field) v = std::pow(10.0f, -v);
    return field;
}

std::optional<std::vector<std::vector<float>>> FilmGrain::builtLights(int r, const std::vector<float>& grosses,
                                                                      uint64_t seed) const {
    const Record& record = records[r];
    const int sublayerSlots = 4;
    if (record.sublayers.empty() || static_cast<int>(record.sublayers.size()) > sublayerSlots) return std::nullopt;
    float decay = record.sublayers[0].sigmaMM;
    for (const Sublayer& layer : record.sublayers) {
        if (layer.profile != Profile::dyeCloud || layer.sigmaMM != decay) return std::nullopt;
    }
    const int side = tileSide;
    const int supersampleCount = supersample(1 / tileTexelMM);
    float h = tileTexelMM / static_cast<float>(supersampleCount);
    float period = static_cast<float>(side) * tileTexelMM;
    std::vector<float> flatTerms;
    for (const CloudTerm& term : dyeCloudTerms()) {
        flatTerms.push_back(term.sigma * decay / h);
        flatTerms.push_back(term.weight);
    }
    float tableScale = static_cast<float>(tableSamples - 1) / swiftMax(record.dMax - record.dMin, 1e-6f);

    // One sublayer of a build: cells along a tile side, peak demand over capacity, capacity, the
    // count thresholds and the developed fraction at each level.
    const int fieldCount = FilmTileBuild::fields;
    std::vector<float> packed(static_cast<size_t>(sublayerSlots) * fieldCount, 1.0f);
    std::vector<std::vector<float>> layerFractions;
    for (size_t b = 0; b < record.sublayers.size(); ++b) {
        const Sublayer& layer = record.sublayers[b];
        int cells = std::max(static_cast<int>(std::round(period / layer.cellMM)), 1);
        float cell = period / static_cast<float>(cells);
        std::vector<float> fractions;
        for (float gross : grosses) {
            float t = swiftMin(swiftMax((gross - record.dMin) * tableScale, 0.0f), static_cast<float>(tableSamples - 1));
            int ti = std::min(static_cast<int>(t), tableSamples - 2);
            float tf = t - static_cast<float>(ti);
            fractions.push_back(layer.forming[ti] * (1 - tf) + layer.forming[ti + 1] * tf);
        }
        layerFractions.push_back(fractions);
        packed[b * fieldCount] = static_cast<float>(cells);
        packed[b * fieldCount + 1] = layer.peakDemand / layer.capacity;
        packed[b * fieldCount + 2] = layer.capacity;
        std::vector<float> thresholds = FilmRandom::countThresholds(layer.coatedPerMM2 * cell * cell);
        std::copy(thresholds.begin(), thresholds.end(), packed.begin() + b * fieldCount + 3);
    }
    // The Halide builder holds every sublayer's deposits per level, so it lays a few at a time.
    const int perCall = 4;
    const int texels = side;
    const size_t texelCount = static_cast<size_t>(texels) * texels;
    int total = static_cast<int>(grosses.size());
    std::vector<float> light;
    light.reserve(texelCount * total);
    uint32_t seed32 = FilmRandom::seed32(seed);
    for (int start = 0; start < total; start += perCall) {
        int levels = std::min(perCall, total - start);
        std::vector<float> fractions(static_cast<size_t>(sublayerSlots) * levels, 0.0f);
        for (size_t b = 0; b < layerFractions.size(); ++b) {
            for (int l = 0; l < levels; ++l) fractions[b * levels + l] = layerFractions[b][start + l];
        }
        std::vector<float> part(texelCount * levels, 0.0f);
        int32_t status = FilmTileBuild::build(texels, supersampleCount, markShape(), flatTerms.data(),
                                              static_cast<int32_t>(dyeCloudTerms().size()), packed.data(),
                                              fractions.data(), levels, seed32, r, part.data());
        if (status != 0) return std::nullopt;
        light.insert(light.end(), part.begin(), part.end());
    }
    std::vector<std::vector<float>> result;
    for (int k = 0; k < total; ++k) {
        result.emplace_back(light.begin() + k * texelCount, light.begin() + (k + 1) * texelCount);
    }
    return result;
}

std::shared_ptr<const FilmGrain::Tiles> FilmGrain::buildTiles(uint64_t seed, bool parallel,
                                                              const CheckCancellation& checkCancellation) const {
    check(checkCancellation);
    std::vector<int> active;
    for (int r : (monochrome ? std::vector<int>{1} : std::vector<int>{0, 1, 2})) {
        if (!records[r].sublayers.empty()) active.push_back(r);
    }
    std::vector<std::vector<TileLevel>> levels(3);
    auto grosses = [&](int r) {
        std::vector<float> values;
        for (int k = 0; k < tileLevels; ++k) {
            values.push_back(records[r].dMin + (records[r].dMax - records[r].dMin) * static_cast<float>(k) /
                                                   static_cast<float>(tileLevels - 1));
        }
        return values;
    };
    for (int r : active) {
        check(checkCancellation);
        std::optional<std::vector<std::vector<float>>> built = builtLights(r, grosses(r), seed);
        if (built) {
            for (const auto& light : *built) levels[r].push_back(tileLevel(light));
        }
    }
    active.erase(std::remove_if(active.begin(), active.end(), [&](int r) { return !levels[r].empty(); }),
                 active.end());
    int count = static_cast<int>(active.size()) * tileLevels;
    std::vector<TileLevel> results(count);
    int workers = std::min(parallel ? 2 : 1, count);
    if (workers > 0) {
        for (int start = 0; start < count; start += workers) {
            check(checkCancellation);
            ParallelWork::forEach(std::min(workers, count - start), [&](int offset) {
                int index = start + offset;
                int r = active[index / tileLevels], k = index % tileLevels;
                const Record& record = records[r];
                float gross = record.dMin + (record.dMax - record.dMin) * static_cast<float>(k) /
                                                static_cast<float>(tileLevels - 1);
                results[index] = tileLevel(referenceTileLight(r, gross, seed));
            });
        }
    }
    check(checkCancellation);
    for (size_t index = 0; index < active.size(); ++index) {
        int r = active[index];
        for (int k = 0; k < tileLevels; ++k) levels[r].push_back(results[index * tileLevels + k]);
    }
    std::vector<float> dMin, dMax;
    for (const Record& record : records) {
        dMin.push_back(record.dMin);
        dMax.push_back(record.dMax);
    }
    return std::make_shared<const Tiles>(std::move(levels), std::move(dMin), std::move(dMax));
}

FilmGrain::TileLevel FilmGrain::tileLevel(const std::vector<float>& light) {
    const int n = tileSide;
    double total = 0.0;
    for (float t : light) total += static_cast<double>(t);
    double mean = total / static_cast<double>(light.size());
    std::vector<float> sums(static_cast<size_t>(n + 1) * (n + 1), 0.0f);
    std::vector<double> column(n, 0.0);
    for (int y = 0; y < n; ++y) {
        double run = 0.0;
        for (int x = 0; x < n; ++x) {
            column[x] += static_cast<double>(light[static_cast<size_t>(y) * n + x]) - mean;
            run += column[x];
            sums[static_cast<size_t>(y + 1) * (n + 1) + x + 1] = static_cast<float>(run);
        }
    }
    return TileLevel{std::move(sums), static_cast<float>(mean)};
}

FilmGrain::PitchTables FilmGrain::pitchTables(const Tiles& tiles, float footprint) const {
    return tiles.tables(footprint, [&] { return measurePitch(tiles, static_cast<double>(footprint)); });
}

FilmGrain::PitchTables FilmGrain::measurePitch(const Tiles& tiles, double pitch, int side) {
    std::vector<std::pair<double, double>> pixels;
    pixels.reserve(static_cast<size_t>(side) * side);
    for (int i = 0; i < side * side; ++i) {
        pixels.emplace_back(static_cast<double>((i % side) * 7919 % 4096) * pitch,
                            static_cast<double>((i / side) * 104729 % 4096) * pitch);
    }
    std::vector<std::vector<float>> means(3), scales(3);
    for (int r = 0; r < 3; ++r) {
        if (tiles.levels[r].empty()) continue;
        const auto& levels = tiles.levels[r];
        std::vector<std::vector<double>> fields(levels.size());
        for (size_t k = 0; k < levels.size(); k += 2) {
            size_t next = std::min(k + 1, levels.size() - 1);
            std::vector<double> a(pixels.size()), b(pixels.size());
            for (size_t i = 0; i < pixels.size(); ++i) {
                const auto& p = pixels[i];
                std::pair<float, float> lights =
                    blockLight(levels[k], levels[next], p.first, p.first + pitch, p.second, p.second + pitch,
                               static_cast<int>(std::floor((p.first + pitch / 2) / blockTexels())),
                               static_cast<int>(std::floor((p.second + pitch / 2) / blockTexels())), 0x5A3F1E1Du, 0,
                               r);
                a[i] = -std::log10(swiftMax(static_cast<double>(lights.first), 1e-9));
                b[i] = -std::log10(swiftMax(static_cast<double>(lights.second), 1e-9));
            }
            fields[k] = a;
            fields[next] = b;
        }
        std::vector<double> m;
        for (const auto& field : fields) {
            double sum = 0;
            for (double v : field) sum = sum + v;
            m.push_back(sum / static_cast<double>(field.size()));
        }
        for (double v : m) means[r].push_back(static_cast<float>(v));
        for (size_t k = 0; k + 1 < fields.size(); ++k) {
            double aa = 0.0, bb = 0.0, ab = 0.0;
            for (size_t i = 0; i < pixels.size(); ++i) {
                double a = fields[k][i] - m[k], b = fields[k + 1][i] - m[k + 1];
                aa += a * a;
                bb += b * b;
                ab += a * b;
            }
            scales[r].push_back(static_cast<float>(
                aa > 0 && bb > 0 ? swiftMin(swiftMax(ab / std::sqrt(aa * bb), -1.0), 1.0) : 1.0));
        }
    }
    return PitchTables{means, scales};
}

float FilmGrain::blend(float a, float b, float w, float rho) {
    float kept = (1 - w) * (1 - w) + w * w + 2 * w * (1 - w) * rho;
    return (a + (b - a) * w) / std::sqrt(swiftMax(kept, 1e-4f));
}

double FilmGrain::sumAt(const std::vector<float>& sums, double x, double y) {
    const int n = tileSide, stride = n + 1;
    int ix = std::min(std::max(static_cast<int>(x), 0), n - 1), iy = std::min(std::max(static_cast<int>(y), 0), n - 1);
    double ax = x - static_cast<double>(ix), ay = y - static_cast<double>(iy);
    double s00 = sums[static_cast<size_t>(iy) * stride + ix], s10 = sums[static_cast<size_t>(iy) * stride + ix + 1];
    double s01 = sums[static_cast<size_t>(iy + 1) * stride + ix];
    double s11 = sums[static_cast<size_t>(iy + 1) * stride + ix + 1];
    return s00 + (s10 - s00) * ax + (s01 - s00) * ay + (s11 - s10 - s01 + s00) * ax * ay;
}

double FilmGrain::boxSum(const std::vector<float>& sums, double x0, double x1, double y0, double y1) {
    return sumAt(sums, x1, y1) - sumAt(sums, x0, y1) - sumAt(sums, x1, y0) + sumAt(sums, x0, y0);
}

double FilmGrain::wrappedSumAt(const std::vector<float>& sums, double x, double y) {
    double n = static_cast<double>(tileSide);
    double px = x >= n ? x - n : x, py = y >= n ? y - n : y;
    double sum = sumAt(sums, px, py);
    if (x >= n) sum += sumAt(sums, n, py);
    if (y >= n) sum += sumAt(sums, px, n);
    if (x >= n && y >= n) sum += sumAt(sums, n, n);
    return sum;
}

double FilmGrain::wrappedBoxSum(const std::vector<float>& sums, double x0, double x1, double y0, double y1) {
    return wrappedSumAt(sums, x1, y1) - wrappedSumAt(sums, x0, y1) - wrappedSumAt(sums, x1, y0) +
           wrappedSumAt(sums, x0, y0);
}

float FilmGrain::tileSigma48(const std::vector<float>& light) {
    const int n = tileSide;
    double total = 0.0;
    for (float t : light) total += static_cast<double>(t);
    double mean = total / static_cast<double>(n * n);
    std::vector<double> d(light.size());
    for (size_t i = 0; i < light.size(); ++i) d[i] = static_cast<double>(light[i]) - mean;
    const int reach = 16;
    const int lagSide = reach * 2 + 1;
    std::vector<double> sums(static_cast<size_t>(lagSide) * lagSide, 0.0);
    ParallelWork::forEach(lagSide, [&](int rowIndex) {
        int dy = rowIndex - reach;
        for (int dx = -reach; dx <= reach; ++dx) {
            double c = 0.0;
            for (int y = 0; y < n; ++y) {
                size_t row = static_cast<size_t>(y) * n, other = static_cast<size_t>((y + dy + n) % n) * n;
                for (int x = 0; x < n; ++x) c += d[row + x] * d[other + (x + dx + n) % n];
            }
            sums[static_cast<size_t>(rowIndex) * lagSide + dx + reach] = c / static_cast<double>(n * n);
        }
    });
    double power = 0.0;
    for (int rowIndex = 0; rowIndex < lagSide; ++rowIndex) {
        for (int column = 0; column < lagSide; ++column) {
            double shared = blockSharing(rowIndex - reach) * blockSharing(column - reach);
            power += sums[static_cast<size_t>(rowIndex) * lagSide + column] * shared;
        }
    }
    double radius = static_cast<double>(FilmStock::granularityApertureRadiusMM / tileTexelMM);
    double lightVariance = swiftMax(power, 0.0) / (kPi * radius * radius);
    return static_cast<float>(std::sqrt(lightVariance) / (mean * 2.302585093));
}

FilmGrain::Placement FilmGrain::blockPlacement(uint32_t seed, int layer, int record, int bx, int by) {
    uint32_t stream = (tileStream + static_cast<uint32_t>(3 * layer + record)) * 0x9E3779B9u;
    uint32_t h = pcgHash(static_cast<uint32_t>(bx) ^ pcgHash(static_cast<uint32_t>(by) ^ pcgHash(seed ^ stream)));
    uint32_t period = static_cast<uint32_t>(tileSide);
    return Placement{static_cast<double>(h % period), static_cast<double>((h >> 8) % period),
                     static_cast<int>((h >> 16) & 7)};
}

double FilmGrain::blockTexels() { return static_cast<double>(std::round(tileBlockMM / tileTexelMM)); }
double FilmGrain::marginTexels() { return static_cast<double>(std::round(tileMarginMM / tileTexelMM)); }
double FilmGrain::seamTexels() { return static_cast<double>(std::round(tileSeamMM / tileTexelMM)); }

double FilmGrain::layingShift(int layer) {
    return blockTexels() * static_cast<double>(layer) / static_cast<double>(tileLayers);
}

double FilmGrain::blockWeight(double c, int b) {
    double start = static_cast<double>(b) * blockTexels(), seam = seamTexels();
    return swiftMin(swiftMax((c - start + seam) / (2 * seam), 0.0), 1.0) *
           swiftMin(swiftMax((start + blockTexels() + seam - c) / (2 * seam), 0.0), 1.0);
}

double FilmGrain::blockSharing(int lag) {
    static const std::vector<double> table = [] {
        int block = static_cast<int>(blockTexels()), samples = 16;
        std::vector<double> values;
        for (int lagValue = 0; lagValue <= block; ++lagValue) {
            double total = 0.0;
            for (int i = 0; i < block * samples; ++i) {
                double c = (static_cast<double>(i) + 0.5) / static_cast<double>(samples);
                double d = c + static_cast<double>(lagValue);
                double shared = 0.0, normC = 0.0, normD = 0.0;
                for (int b = -1; b <= 1 + (lagValue + block - 1) / block; ++b) {
                    double wc = blockWeight(c, b), wd = blockWeight(d, b);
                    shared += wc * wd;
                    normC += wc * wc;
                    normD += wd * wd;
                }
                total += shared / std::sqrt(normC * normD);
            }
            values.push_back(total / static_cast<double>(block * samples));
        }
        return values;
    }();
    int index = std::abs(lag);
    return index < static_cast<int>(table.size()) ? table[index] : 0.0;
}

std::pair<float, float> FilmGrain::blockLight(const TileLevel& a, const TileLevel& b, double x0, double x1,
                                              double y0, double y1, int bx, int by, uint32_t seed, int layer,
                                              int record) {
    double block = blockTexels(), margin = marginTexels();
    double left = static_cast<double>(bx) * block, top = static_cast<double>(by) * block;
    double u0 = swiftMax(x0, left - margin) - left, u1 = swiftMin(x1, left + block + margin) - left;
    double v0 = swiftMax(y0, top - margin) - top, v1 = swiftMin(y1, top + block + margin) - top;
    double area = swiftMax((u1 - u0) * (v1 - v0), 1e-6);
    Placement placement = blockPlacement(seed, layer, record, bx, by);
    double p0 = u0, p1 = u1, q0 = v0, q1 = v1;
    if (placement.turn & 1) {
        p0 = block - u1;
        p1 = block - u0;
    }
    if (placement.turn & 2) {
        q0 = block - v1;
        q1 = block - v0;
    }
    if (placement.turn & 4) {
        std::swap(p0, q0);
        std::swap(p1, q1);
    }
    double sx = margin + placement.ox, sy = margin + placement.oy;
    double sumA = wrappedBoxSum(a.sums, p0 + sx, p1 + sx, q0 + sy, q1 + sy);
    double sumB = wrappedBoxSum(b.sums, p0 + sx, p1 + sx, q0 + sy, q1 + sy);
    return {a.meanLight + static_cast<float>(sumA / area), b.meanLight + static_cast<float>(sumB / area)};
}

// MARK: - Look

FilmGrain::Look::Geometry FilmGrain::Look::geometry(float pxPerMM) const {
    float pitch = (1 / pxPerMM) / FilmGrain::tileTexelMM / swiftMin(swiftMax(size, sizeRangeLower), sizeRangeUpper);
    float soft = swiftMin(swiftMax(softness, softnessRangeLower), softnessRangeUpper);
    return Geometry{pitch, pitch * soft};
}

std::vector<float> FilmGrain::Look::recordAmounts(float amount) const {
    float clampedSize = swiftMin(swiftMax(size, sizeRangeLower), sizeRangeUpper);
    std::vector<float> result;
    for (int i = 0; i < 3; ++i) result.push_back(amount * swiftMax(layers[i], 0.0f) / clampedSize);
    return result;
}

FilmGrain::Look::Mix FilmGrain::Look::mix(float colour) {
    float own = swiftMin(swiftMax(colour, 0.0f), 1.0f);
    return Mix{own, std::sqrt(3 - 2 * own * own) - own};
}

// MARK: - The kernel's inputs

std::vector<float> FilmGrain::configurationBlock(float pxPerMM, float amount, const Look& look, int32_t id,
                                                 const Tiles* supplied) const {
    std::shared_ptr<const Tiles> owned;
    const Tiles* tilesIn = supplied;
    if (!tilesIn) {
        owned = tiles();
        tilesIn = owned.get();
    }
    Look::Geometry geometry = look.geometry(pxPerMM);
    PitchTables tables = pitchTables(*tilesIn, geometry.footprint);
    const int levels = tileLevels;
    std::vector<float> block = {geometry.pitch, geometry.footprint, static_cast<float>(id)};
    for (float v : look.recordAmounts(amount)) block.push_back(v);
    block.push_back(monochrome ? 1.0f : look.colour);
    block.insert(block.end(), tilesIn->dMin.begin(), tilesIn->dMin.end());
    block.insert(block.end(), tilesIn->dMax.begin(), tilesIn->dMax.end());
    for (int r = 0; r < 3; ++r) {
        if (tilesIn->levels[r].empty()) {
            block.insert(block.end(), levels, 1.0f);
            block.insert(block.end(), levels, 0.0f);
            block.insert(block.end(), levels, 1.0f);
            continue;
        }
        for (const TileLevel& level : tilesIn->levels[r]) block.push_back(level.meanLight);
        block.insert(block.end(), tables.meanDensity[r].begin(), tables.meanDensity[r].end());
        block.insert(block.end(), tables.blendCorrelation[r].begin(), tables.blendCorrelation[r].end());
        block.push_back(1);
    }
    return block;
}

// MARK: - Binding

FilmGrain::TileBinding::TileBinding(std::shared_ptr<const FilmGrain> grain_, std::shared_ptr<const Tiles> tiles,
                                    int32_t id_)
    : grain(std::move(grain_)), id(id_), tiles_(std::move(tiles)) {}

std::vector<float> FilmGrain::TileBinding::configurationBlock(float pxPerMM, float amount, const Look& look) const {
    return grain->configurationBlock(pxPerMM, amount, look, id, tiles_.get());
}

std::shared_ptr<const std::vector<float>> FilmGrain::TileBinding::packed() const {
    std::lock_guard<std::mutex> guard(packLock_);
    if (!packed_) packed_ = std::make_shared<const std::vector<float>>(tiles_->packed());
    return packed_;
}

std::string FilmGrain::Population::identity() const {
    return FilmGrainAsset::identity(stock, reference ? &*reference : nullptr);
}

namespace {

class Registry {
public:
    std::shared_ptr<const FilmGrain::TileBinding> entry(const FilmStock& stock, const FilmStock* reference,
                                                        const CheckCancellation& checkCancellation) {
        std::string key = FilmGrainAsset::identity(stock, reference);
        if (auto hit = cached(key)) return hit;
        if (std::shared_ptr<FilmGrain> provided = FilmGrainAsset::provided(key)) {
            std::shared_ptr<const FilmGrain::Tiles> tiles = provided->tiles(checkCancellation);
            return publish(key, provided, tiles);
        }
        {
            std::unique_lock<std::mutex> guard(lock_);
            while (true) {
                check(checkCancellation);
                for (const auto& entry : entries_) {
                    if (entry.first == key) return entry.second;
                }
                if (!preparing_) break;
                condition_.wait_for(guard, std::chrono::milliseconds(20));
            }
            preparing_ = true;
        }
        struct Release {
            Registry& registry;
            ~Release() {
                std::lock_guard<std::mutex> guard(registry.lock_);
                registry.preparing_ = false;
                registry.condition_.notify_all();
            }
        } release{*this};
        auto grain = std::make_shared<const FilmGrain>(stock, reference, 1.0f, true, checkCancellation);
        check(checkCancellation);
        std::shared_ptr<const FilmGrain::Tiles> tiles = grain->tiles(checkCancellation);
        check(checkCancellation);
        return publish(key, grain, tiles);
    }

    bool isPrepared(const std::string& key) { return cached(key) != nullptr || FilmGrainAsset::isProvided(key); }

    std::shared_ptr<const FilmGrain::TileBinding> binding(int32_t id) {
        std::lock_guard<std::mutex> guard(lock_);
        auto found = live_.find(id);
        return found == live_.end() ? nullptr : found->second.lock();
    }

private:
    std::shared_ptr<const FilmGrain::TileBinding> cached(const std::string& key) {
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& entry : entries_) {
            if (entry.first == key) return entry.second;
        }
        return nullptr;
    }

    std::shared_ptr<const FilmGrain::TileBinding> publish(const std::string& key,
                                                          std::shared_ptr<const FilmGrain> grain,
                                                          std::shared_ptr<const FilmGrain::Tiles> tiles) {
        int32_t id;
        {
            std::lock_guard<std::mutex> guard(lock_);
            id = nextID_++;
        }
        auto binding = std::make_shared<const FilmGrain::TileBinding>(std::move(grain), std::move(tiles), id);
        std::lock_guard<std::mutex> guard(lock_);
        for (const auto& entry : entries_) {
            if (entry.first == key) return entry.second;
        }
        for (auto it = live_.begin(); it != live_.end();) {
            if (it->second.expired()) it = live_.erase(it); else ++it;
        }
        live_[binding->id] = binding;
        entries_.emplace_back(key, binding);
        if (entries_.size() > 4) entries_.pop_front();
        return binding;
    }

    std::mutex lock_;
    std::condition_variable condition_;
    std::deque<std::pair<std::string, std::shared_ptr<const FilmGrain::TileBinding>>> entries_;
    std::map<int32_t, std::weak_ptr<const FilmGrain::TileBinding>> live_;
    int32_t nextID_ = 1;
    bool preparing_ = false;
};

Registry& registry() {
    static Registry& value = *new Registry;
    return value;
}

}  // namespace

std::shared_ptr<const FilmGrain::TileBinding> FilmGrain::binding(const FilmStock& stock, const FilmStock* reference,
                                                                 const CheckCancellation& checkCancellation) {
    return registry().entry(stock, reference, checkCancellation);
}

bool FilmGrain::isPrepared(const Population& population) { return registry().isPrepared(population.identity()); }

void FilmGrain::prepare(const Population& population, const CheckCancellation& checkCancellation) {
    (void)registry().entry(population.stock, population.reference ? &*population.reference : nullptr,
                           checkCancellation);
}

std::shared_ptr<const std::vector<float>> FilmGrain::registeredTiles(const std::vector<float>& configuration) {
    const size_t base = FOTUFILM_CONFIG_FILM_TILE;
    if (!(configuration.size() > base + 2) || configuration[FOTUFILM_CONFIG_GRAIN_MODE] != 1) return nullptr;
    float raw = configuration[base + 2];
    if (!(std::isfinite(raw) && raw == std::floor(raw) && std::fabs(raw) < 2147483648.0f)) return nullptr;
    std::shared_ptr<const TileBinding> found = registry().binding(static_cast<int32_t>(raw));
    return found ? found->packed() : nullptr;
}

}  // namespace fotufilm
