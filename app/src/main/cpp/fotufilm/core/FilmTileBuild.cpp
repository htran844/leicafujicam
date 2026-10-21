// Port of Sources/FotufilmHalide/Pipeline/FilmTileBuild.h and FotufilmFilmTileBuild.cpp.
#include "FilmTileBuild.h"

#include <algorithm>
#include <cmath>

#include "CoreMath.h"
#include "ParallelWork.h"
#include "Simd.h"

namespace fotufilm {
namespace FilmTileBuild {

namespace {

constexpr uint32_t kGolden = 0x9E3779B9u;

inline float uniform(uint32_t hash) { return (static_cast<float>(hash >> 8) + 0.5f) * (1.0f / 16777216.0f); }
inline uint32_t cellKey(uint32_t base, int hx, int hy) {
    return pcgHash(static_cast<uint32_t>(hx) ^ pcgHash(static_cast<uint32_t>(hy) ^ pcgHash(base)));
}
inline uint32_t draw(uint32_t key, int index) { return pcgHash(key ^ (static_cast<uint32_t>(index) * kGolden)); }
/// Halide's integer modulo: non-negative for a positive divisor.
inline int wrap(int v, int period) {
    int r = v % period;
    return r < 0 ? r + period : r;
}

struct Crystal {
    float x;
    float y;
    float develop;
    float peak;
};

/// Rows of output samples handled by one task of the deposit stage.
constexpr int kBandRows = 32;

}  // namespace

std::vector<float> gaussianKernel(float sigma) {
    int radius = std::max(static_cast<int>(std::ceil(3.5f * sigma)), 1);
    std::vector<float> kernel;
    float total = 0;
    for (int t = -radius; t <= radius; ++t) {
        float v = std::exp(-static_cast<float>(t * t) / (2 * sigma * sigma));
        kernel.push_back(v);
        total += v;
    }
    for (float& v : kernel) v /= total;
    return kernel;
}

int termFactor(float sigma, int samples) {
    int factor = std::max(static_cast<int>(sigma / 1.6f), 1);
    while (factor > 1 && samples % factor != 0) --factor;
    return factor;
}

int32_t build(int32_t texels, int32_t supersample, int32_t markShape, const float* terms, int32_t termCount,
              const float* sublayerFields, const float* fractions, int32_t levels, uint32_t seed, int32_t record,
              float* light) {
    if (!terms || !sublayerFields || !fractions || !light || texels < 16 || texels > 1024 || supersample < 1 ||
        supersample > 8 || markShape < 1 || markShape > 16 || termCount < 1 || termCount > 8 || levels < 1 ||
        levels > 64 || record < 0 || record > 2) {
        return -1;
    }
    for (int i = 0; i < termCount; ++i) {
        if (!std::isfinite(terms[2 * i]) || terms[2 * i] <= 0 || !std::isfinite(terms[2 * i + 1])) return -1;
    }
    for (int i = 0; i < fields * sublayers; ++i) {
        if (!std::isfinite(sublayerFields[i])) return -1;
    }
    for (int b = 0; b < sublayers; ++b) {
        float cells = sublayerFields[b * fields];
        if (cells < 1 || cells > maxCells || cells != std::floor(cells) ||
            static_cast<float>(texels * supersample) / cells < 0.75f) {
            return -1;
        }
    }

    const int n = texels * supersample;
    const size_t plane = static_cast<size_t>(n) * n;
    const float pi = kFloatPi;
    // dye(x, y, l), level-major planes.
    std::vector<float> dye(plane * levels, 0.0f);
    std::vector<float> deposit(plane * levels);
    std::vector<float> demand(plane * levels);

    for (int b = 0; b < sublayers; ++b) {
        const float* fieldsOf = sublayerFields + b * fields;
        const float* fractionsOf = fractions + static_cast<size_t>(b) * levels;
        // A sublayer no level develops lays capacity * (1 - exp(-0)) = 0 everywhere.
        bool any = false;
        for (int l = 0; l < levels; ++l) {
            if (fractionsOf[l] > 0) any = true;
        }
        if (!any) continue;
        const int cells = static_cast<int>(fieldsOf[0]);
        const float edge = fieldsOf[1], capacity = fieldsOf[2];
        const float* thresholds = fieldsOf + 3;
        int limit = 0;
        for (int k = 0; k < maxCount; ++k) {
            if (thresholds[k] < 1.0f) limit = k + 1;
        }
        limit = std::max(limit, 1);
        const float cellSize = static_cast<float>(n) / static_cast<float>(cells);
        const uint32_t stream = static_cast<uint32_t>(record * 8 + b * 2);
        const uint32_t crystalBase = seed ^ (stream * kGolden);
        const uint32_t countBase = seed ^ ((stream + 1) * kGolden);

        // Crystals per cell: the cell's count draw against the Poisson thresholds.
        std::vector<int> count(static_cast<size_t>(cells) * cells);
        ParallelWork::forEach(cells, [&](int cy) {
            for (int cx = 0; cx < cells; ++cx) {
                float u = uniform(draw(cellKey(countBase, cx, cy), 0));
                int total = 0;
                for (int k = 0; k < maxCount; ++k) total += thresholds[k] < u ? 1 : 0;
                count[static_cast<size_t>(cy) * cells + cx] = std::min(total, limit);
            }
        });
        auto crystalOf = [&](int cx, int cy, int c) {
            uint32_t hash = cellKey(crystalBase, cx, cy);
            int first = c * (3 + markShape);
            float mark = 0.0f;
            for (int i = 0; i < markShape; ++i) mark = mark - std::log(swiftMax(uniform(draw(hash, first + 3 + i)), 1.0e-7f));
            return Crystal{uniform(draw(hash, first)), uniform(draw(hash, first + 1)), uniform(draw(hash, first + 2)),
                           edge * (mark / static_cast<float>(markShape))};
        };

        // Each sample's share of the peak demand of every crystal whose bilinear footprint
        // covers it, per level. A band of sample rows reads the crystals of the cell rows its
        // footprints meet, drawn once for the band.
        std::fill(deposit.begin(), deposit.end(), 0.0f);
        const int bands = (n + kBandRows - 1) / kBandRows;
        ParallelWork::forEach(bands, [&](int band) {
            int yStart = band * kBandRows, yEnd = std::min(n, yStart + kBandRows);
            int rowLow = static_cast<int>(std::floor((static_cast<float>(yStart) + 0.5f - 1.0f) / cellSize));
            int rowHigh = static_cast<int>(std::floor((static_cast<float>(yEnd - 1) + 0.5f + 1.0f) / cellSize));
            int rowCount = rowHigh - rowLow + 1;
            // Crystals of cell rows rowLow...rowHigh (unwrapped), every column.
            std::vector<int> offsets(static_cast<size_t>(rowCount) * cells + 1, 0);
            for (int r = 0; r < rowCount; ++r) {
                int hy = std::min(std::max(wrap(rowLow + r, cells), 0), cells - 1);
                for (int cx = 0; cx < cells; ++cx) {
                    size_t slot = static_cast<size_t>(r) * cells + cx;
                    offsets[slot + 1] = offsets[slot] + count[static_cast<size_t>(hy) * cells + cx];
                }
            }
            std::vector<Crystal> crystals(offsets.back());
            for (int r = 0; r < rowCount; ++r) {
                int hy = std::min(std::max(wrap(rowLow + r, cells), 0), cells - 1);
                for (int cx = 0; cx < cells; ++cx) {
                    size_t slot = static_cast<size_t>(r) * cells + cx;
                    for (int c = 0; c < offsets[slot + 1] - offsets[slot]; ++c) {
                        crystals[offsets[slot] + c] = crystalOf(cx, hy, c);
                    }
                }
            }
            for (int y = yStart; y < yEnd; ++y) {
                float sy = static_cast<float>(y) + 0.5f;
                int cy0 = static_cast<int>(std::floor((sy - 1.0f) / cellSize));
                int cy1 = static_cast<int>(std::floor((sy + 1.0f) / cellSize));
                for (int x = 0; x < n; ++x) {
                    float sx = static_cast<float>(x) + 0.5f;
                    int cx0 = static_cast<int>(std::floor((sx - 1.0f) / cellSize));
                    int cx1 = static_cast<int>(std::floor((sx + 1.0f) / cellSize));
                    size_t at = static_cast<size_t>(y) * n + x;
                    for (int ry = 0; ry < cellWindow; ++ry) {
                        if (!(ry <= cy1 - cy0)) continue;
                        int uy = cy0 + ry;
                        int localRow = uy - rowLow;
                        for (int rx = 0; rx < cellWindow; ++rx) {
                            if (!(rx <= cx1 - cx0)) continue;
                            int ux = cx0 + rx;
                            int hx = std::min(std::max(wrap(ux, cells), 0), cells - 1);
                            size_t slot = static_cast<size_t>(localRow) * cells + hx;
                            for (int c = offsets[slot]; c < offsets[slot + 1]; ++c) {
                                const Crystal& crystal = crystals[c];
                                float px = (static_cast<float>(ux) + crystal.x) * cellSize;
                                float py = (static_cast<float>(uy) + crystal.y) * cellSize;
                                float share = swiftMax(0.0f, 1.0f - std::fabs(px - sx)) *
                                              swiftMax(0.0f, 1.0f - std::fabs(py - sy));
                                for (int l = 0; l < levels; ++l) {
                                    if (crystal.develop < fractionsOf[l]) {
                                        deposit[static_cast<size_t>(l) * plane + at] += share * crystal.peak;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });

        // The demand: every term of the cloud, times its weight · 2π σ².
        std::fill(demand.begin(), demand.end(), 0.0f);
        for (int i = 0; i < termCount; ++i) {
            const float sigma = terms[2 * i];
            const float scale = terms[2 * i + 1] * 2 * pi * sigma * sigma;
            const int factor = termFactor(sigma, n);
            if (factor == 1) {
                std::vector<float> kernel = gaussianKernel(sigma);
                int radius = static_cast<int>(kernel.size()) / 2;
                std::vector<float> across(plane * levels);
                ParallelWork::forEach(n * levels, [&](int task) {
                    int l = task / n, y = task % n;
                    const float* source = deposit.data() + static_cast<size_t>(l) * plane + static_cast<size_t>(y) * n;
                    float* out = across.data() + static_cast<size_t>(l) * plane + static_cast<size_t>(y) * n;
                    for (int x = 0; x < n; ++x) {
                        float sum = 0.0f;
                        for (int t = 0; t < static_cast<int>(kernel.size()); ++t) {
                            sum += kernel[t] * source[wrap(x + t - radius + n, n)];
                        }
                        out[x] = sum;
                    }
                });
                ParallelWork::forEach(n * levels, [&](int task) {
                    int l = task / n, y = task % n;
                    const float* source = across.data() + static_cast<size_t>(l) * plane;
                    float* out = demand.data() + static_cast<size_t>(l) * plane + static_cast<size_t>(y) * n;
                    for (int x = 0; x < n; ++x) {
                        float down = 0.0f;
                        for (int t = 0; t < static_cast<int>(kernel.size()); ++t) {
                            down += kernel[t] * source[static_cast<size_t>(wrap(y + t - radius + n, n)) * n + x];
                        }
                        out[x] += scale * down;
                    }
                });
                continue;
            }
            // Deposits shared linearly into cells `factor` samples a side, blurred there by what
            // is left of the term after the two tents, and read back linearly.
            const int m = n / factor;
            const float f = static_cast<float>(factor);
            std::vector<float> tent;
            for (int t = 0; t < 3 * factor; ++t) {
                tent.push_back(std::max(0.0f, 1.0f - std::fabs((static_cast<float>(t) + 0.5f) / f - 1.5f)));
            }
            const size_t coarsePlane = static_cast<size_t>(m) * m;
            std::vector<float> rows(static_cast<size_t>(m) * n * levels);
            ParallelWork::forEach(n * levels, [&](int task) {
                int l = task / n, y = task % n;
                const float* source = deposit.data() + static_cast<size_t>(l) * plane + static_cast<size_t>(y) * n;
                float* out = rows.data() + (static_cast<size_t>(l) * n + y) * m;
                for (int i0 = 0; i0 < m; ++i0) {
                    float rowSum = 0.0f;
                    for (int t = 0; t < static_cast<int>(tent.size()); ++t) {
                        if (tent[t] > 0) rowSum += tent[t] * source[wrap(factor * i0 - factor + t + n, n)];
                    }
                    out[i0] = rowSum;
                }
            });
            std::vector<float> cellsum(coarsePlane * levels);
            ParallelWork::forEach(m * levels, [&](int task) {
                int l = task / m, j0 = task % m;
                float* out = cellsum.data() + static_cast<size_t>(l) * coarsePlane + static_cast<size_t>(j0) * m;
                for (int i0 = 0; i0 < m; ++i0) {
                    float cellSum = 0.0f;
                    for (int t = 0; t < static_cast<int>(tent.size()); ++t) {
                        if (tent[t] > 0) {
                            int y = wrap(factor * j0 - factor + t + n, n);
                            cellSum += tent[t] * rows[(static_cast<size_t>(l) * n + y) * m + i0];
                        }
                    }
                    out[i0] = cellSum;
                }
            });
            std::vector<float> kernel = gaussianKernel(std::sqrt(sigma * sigma - f * f / 3) / f);
            int radius = static_cast<int>(kernel.size()) / 2;
            std::vector<float> across(coarsePlane * levels), blurred(coarsePlane * levels);
            ParallelWork::forEach(m * levels, [&](int task) {
                int l = task / m, j0 = task % m;
                const float* source = cellsum.data() + static_cast<size_t>(l) * coarsePlane + static_cast<size_t>(j0) * m;
                float* out = across.data() + static_cast<size_t>(l) * coarsePlane + static_cast<size_t>(j0) * m;
                for (int i0 = 0; i0 < m; ++i0) {
                    float sum = 0.0f;
                    for (int t = 0; t < static_cast<int>(kernel.size()); ++t) {
                        sum += kernel[t] * source[wrap(i0 + t - radius + m, m)];
                    }
                    out[i0] = sum;
                }
            });
            ParallelWork::forEach(m * levels, [&](int task) {
                int l = task / m, j0 = task % m;
                const float* source = across.data() + static_cast<size_t>(l) * coarsePlane;
                float* out = blurred.data() + static_cast<size_t>(l) * coarsePlane + static_cast<size_t>(j0) * m;
                for (int i0 = 0; i0 < m; ++i0) {
                    float down = 0.0f;
                    for (int t = 0; t < static_cast<int>(kernel.size()); ++t) {
                        down += kernel[t] * source[static_cast<size_t>(wrap(j0 + t - radius + m, m)) * m + i0];
                    }
                    out[i0] = down;
                }
            });
            const float perSample = scale / (f * f);
            ParallelWork::forEach(n * levels, [&](int task) {
                int l = task / n, y = task % n;
                const float* source = blurred.data() + static_cast<size_t>(l) * coarsePlane;
                float* out = demand.data() + static_cast<size_t>(l) * plane + static_cast<size_t>(y) * n;
                float vf = (static_cast<float>(y) + 0.5f) / f - 0.5f;
                int iv = static_cast<int>(std::floor(vf));
                float av = vf - static_cast<float>(iv);
                int b0 = wrap(iv + m, m), b1 = wrap(iv + 1 + m, m);
                for (int x = 0; x < n; ++x) {
                    float uf = (static_cast<float>(x) + 0.5f) / f - 0.5f;
                    int iu = static_cast<int>(std::floor(uf));
                    float au = uf - static_cast<float>(iu);
                    int a0 = wrap(iu + m, m), a1 = wrap(iu + 1 + m, m);
                    float read = (source[static_cast<size_t>(b0) * m + a0] * (1.0f - au) +
                                  source[static_cast<size_t>(b0) * m + a1] * au) *
                                     (1.0f - av) +
                                 (source[static_cast<size_t>(b1) * m + a0] * (1.0f - au) +
                                  source[static_cast<size_t>(b1) * m + a1] * au) *
                                     av;
                    out[x] += perSample * read;
                }
            });
        }
        for (size_t i = 0; i < plane * levels; ++i) dye[i] += capacity * (1.0f - std::exp(-demand[i]));
    }

    // A texel passes the mean of its samples' transmittance.
    const float norm = static_cast<float>(supersample * supersample);
    ParallelWork::forEach(texels * levels, [&](int task) {
        int l = task / texels, Y = task % texels;
        const float* source = dye.data() + static_cast<size_t>(l) * plane;
        float* out = light + (static_cast<size_t>(l) * texels + Y) * texels;
        for (int X = 0; X < texels; ++X) {
            float sum = 0.0f;
            for (int j = 0; j < supersample; ++j) {
                for (int i = 0; i < supersample; ++i) {
                    sum += std::exp(-2.302585093f *
                                    source[static_cast<size_t>(supersample * Y + j) * n + supersample * X + i]);
                }
            }
            out[X] = sum / norm;
        }
    });
    return 0;
}

}  // namespace FilmTileBuild
}  // namespace fotufilm
