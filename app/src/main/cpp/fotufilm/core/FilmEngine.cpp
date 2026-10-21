// Port of Sources/FotufilmCore/FilmEngine.swift.
#include "FilmEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AutoAdjustment.h"
#include "BooleanGrain.h"
#include "ColorScience.h"
#include "DigitalReferenceReceiver.h"
#include "HDRHighlightRange.h"
#include "HalationReturn.h"
#include "HalationSpectrum.h"
#include "ApproximateNegativeScan.h"
#include "LabScanTiming.h"
#include "ParallelWork.h"
#include "PrintPaperTables.h"

namespace fotufilm {

namespace {

constexpr double kPi = 3.141592653589793;
/// `LensFilterStack.none.addedVeilingGlare`: lens filters are not ported, so no filter glass adds
/// glare.
constexpr float kNoLensFilterGlare = 0;

void check(const CheckCancellation& checkCancellation) {
    if (checkCancellation) checkCancellation();
}

template <typename T>
void append(std::vector<float>& to, const std::vector<T>& values) {
    for (const T& v : values) to.push_back(static_cast<float>(v));
}

float maxIntOr(const std::vector<int>& values, int fallback) {
    if (values.empty()) return static_cast<float>(fallback);
    int result = values[0];
    for (size_t i = 1; i < values.size(); ++i) {
        if (result < values[i]) result = values[i];
    }
    return static_cast<float>(result);
}

int maxInt(const std::vector<int>& values, int fallback) { return static_cast<int>(maxIntOr(values, fallback)); }

}  // namespace

// MARK: - FilmOutputTransform / FilmInputTransform

FilmOutputTransform::FilmOutputTransform(std::vector<float> matrix_, Transfer transfer_,
                                         std::vector<float> coefficients_, bool premultiplied_,
                                         std::optional<float> shoulderKnee_, std::optional<Float3> gamutLuminance_)
    : matrix(std::move(matrix_)),
      transfer(transfer_),
      coefficients(std::move(coefficients_)),
      premultiplied(premultiplied_),
      shoulderKnee(shoulderKnee_),
      gamutLuminance(gamutLuminance_) {
    preconditionCheck(matrix.size() == 9 && coefficients.size() == 6, "output transform takes 9 + 6 values");
}

FilmInputTransform::FilmInputTransform(std::vector<float> matrix_, Transfer transfer_,
                                       std::vector<float> coefficients_, bool premultiplied_)
    : matrix(std::move(matrix_)),
      transfer(transfer_),
      coefficients(std::move(coefficients_)),
      premultiplied(premultiplied_) {
    preconditionCheck(matrix.size() == 9 && coefficients.size() == 6, "input transform takes 9 + 6 values");
}

std::vector<float> FilmInputTransform::parameters() const {
    std::vector<float> packed(17, 0.0f);
    std::copy(matrix.begin(), matrix.end(), packed.begin());
    packed[9] = static_cast<float>(static_cast<int32_t>(transfer));
    std::copy(coefficients.begin(), coefficients.end(), packed.begin() + 10);
    packed[16] = premultiplied ? 1.0f : 0.0f;
    return packed;
}

// MARK: - FilmFlareFrame

FilmFlareFrame FilmFlareFrame::linearRec2020RGBA(const float* pixels, int width, int height) {
    return FilmFlareFrame(width, height, [pixels](size_t index) {
        return Float3(pixels[index * 4], pixels[index * 4 + 1], pixels[index * 4 + 2]);
    });
}

FilmFlareFrame FilmFlareFrame::encodedDisplayP3RGBA(const uint8_t* pixels, int width, int height) {
    return encoded(pixels, width, height, false);
}

FilmFlareFrame FilmFlareFrame::srgbRGBA(const uint8_t* pixels, int width, int height) {
    return encoded(pixels, width, height, true);
}

FilmFlareFrame FilmFlareFrame::encoded(const uint8_t* pixels, int width, int height, bool convertFromSRGB) {
    static const std::vector<float> decode = [] {
        std::vector<float> table;
        for (int i = 0; i < 256; ++i) table.push_back(ColorScience::srgbToLinear(static_cast<float>(i) / 255));
        return table;
    }();
    return FilmFlareFrame(width, height, [pixels, convertFromSRGB](size_t index) {
        size_t offset = index * 4;
        uint8_t alpha = pixels[offset + 3];
        float denominator = alpha > 0 && alpha < 255 ? static_cast<float>(alpha) : 255.0f;
        bool whole = alpha == 0 || alpha == 255;
        auto channel = [&](size_t c) {
            return whole ? decode[pixels[offset + c]]
                         : ColorScience::srgbToLinear(swiftMin(static_cast<float>(pixels[offset + c]) / denominator, 1.0f));
        };
        Float3 decoded(channel(0), channel(1), channel(2));
        return convertFromSRGB ? ColorScience::linearSRGBToRec2020(decoded)
                               : ColorScience::linearDisplayP3ToRec2020(decoded);
    });
}

FilmFlareFrame::FilmFlareFrame(int width, int height, const std::function<Float3(size_t)>& sample)
    : width_(width), height_(height) {
    preconditionCheck(width > 0 && height > 0, "flare frame needs a positive size");
    const int edge = 64;
    int longSide = std::max(width, height);
    int gridWidth = std::min(width, std::max(1, (width * edge + longSide / 2) / longSide));
    int gridHeight = std::min(height, std::max(1, (height * edge + longSide / 2) / longSide));
    gridWidth_ = gridWidth;
    size_t cells = static_cast<size_t>(gridWidth) * gridHeight;
    std::vector<Double3> sums(cells);
    std::vector<int64_t> counts(cells, 0);
    auto band = [&](int cellY) {
        int start = (cellY * height + gridHeight - 1) / gridHeight;
        int end = cellY == gridHeight - 1 ? height : ((cellY + 1) * height + gridHeight - 1) / gridHeight;
        for (int y = start; y < end; ++y) {
            for (int x = 0; x < width; ++x) {
                int cellX = std::min(static_cast<int>(static_cast<int64_t>(x) * gridWidth / width), gridWidth - 1);
                size_t cell = static_cast<size_t>(cellY) * gridWidth + cellX;
                Float3 rgb = sample(static_cast<size_t>(y) * width + x);
                sums[cell] += Double3(rgb.x, rgb.y, rgb.z);
                counts[cell] += 1;
            }
        }
    };
    bool parallel = static_cast<int64_t>(width) * height >= (1 << 18);
    if (parallel) {
        ParallelWork::forEach(gridHeight, band);
    } else {
        for (int cellY = 0; cellY < gridHeight; ++cellY) band(cellY);
    }
    counts_ = counts;
    for (size_t cell = 0; cell < cells; ++cell) {
        double count = static_cast<double>(std::max<int64_t>(counts[cell], 1));
        averages_.push_back(Float3(static_cast<float>(sums[cell].x / count), static_cast<float>(sums[cell].y / count),
                                   static_cast<float>(sums[cell].z / count)));
        int cellX = static_cast<int>(cell % gridWidth), cellY = static_cast<int>(cell / gridWidth);
        positions_.push_back(Position{std::min(((2 * cellX + 1) * width) / (2 * gridWidth), width - 1),
                                      std::min(((2 * cellY + 1) * height) / (2 * gridHeight), height - 1)});
    }
}

FilmFlareFrame FilmFlareFrame::reprojected(int frameWidth, int frameHeight) const {
    FilmFlareFrame copy = *this;
    int gridHeight = gridWidth_ > 0 ? static_cast<int>(averages_.size()) / gridWidth_ : 0;
    for (size_t cell = 0; cell < copy.positions_.size(); ++cell) {
        int cellX = static_cast<int>(cell % gridWidth_), cellY = static_cast<int>(cell / gridWidth_);
        copy.positions_[cell] = Position{std::min(((2 * cellX + 1) * frameWidth) / (2 * gridWidth_), frameWidth - 1),
                                         std::min(((2 * cellY + 1) * frameHeight) / (2 * gridHeight), frameHeight - 1)};
    }
    return copy;
}

// MARK: - Grain lattice mathematics

namespace {

float gaussianLatticeCovariance(float t, float sigma) {
    double position = static_cast<double>(t);
    double s = std::sqrt(swiftMax(2 * static_cast<double>(sigma) * static_cast<double>(sigma), 1e-8));
    auto phi = [](double x) { return std::exp(-0.5 * x * x) / std::sqrt(2 * kPi); };
    auto cdf = [](double x) { return 0.5 * (1 + std::erf(x / std::sqrt(2.0))); };
    auto z = [&](double x) { return (x - position) / s; };
    return static_cast<float>((1 + position) * (cdf(z(0)) - cdf(z(-1))) + (1 - position) * (cdf(z(1)) - cdf(z(0))) +
                              s * (phi(z(-1)) - 2 * phi(z(0)) + phi(z(1))));
}

float solvedSigma(float target) {
    const float floor = FilmEngineInvocation::grainSigmaFloorPixels;
    if (!(target > 0)) return floor;
    float high = 1;
    while (FilmEngineInvocation::sampledGaussianCorrelation(high) < target && high < 64) high *= 2;
    float low = floor;
    if (!(FilmEngineInvocation::sampledGaussianCorrelation(low) < target)) return low;
    for (int i = 0; i < 40; ++i) {
        float middle = 0.5f * (low + high);
        if (FilmEngineInvocation::sampledGaussianCorrelation(middle) < target) low = middle; else high = middle;
    }
    return 0.5f * (low + high);
}

template <typename Read>
float foldMixture(const Read& read, float fold, float keep) {
    float kept = swiftMin(swiftMax(keep, 0.0f), 1.0f);
    return kept * kept * read(0.0f) + 2 * kept * (1 - kept) * read(fold) +
           (1 - kept) * (1 - kept) * read(fold * 1.4142135f);
}

}  // namespace

float FilmEngineInvocation::sampledGaussianCorrelation(float sigma) {
    int radius = gaussianRadius(sigma);
    if (!(radius > 0 && sigma > 0)) return 0;
    std::vector<double> taps;
    for (int o = -radius; o <= radius; ++o) {
        taps.push_back(std::exp(-static_cast<double>(o * o) / (2 * static_cast<double>(sigma) * static_cast<double>(sigma))));
    }
    double zero = 0, one = 0;
    for (size_t i = 0; i < taps.size(); ++i) zero += taps[i] * taps[i];
    for (size_t i = 0; i + 1 < taps.size(); ++i) one += taps[i] * taps[i + 1];
    return zero > 0 ? static_cast<float>(one / zero) : 0.0f;
}

float FilmEngineInvocation::gaussianClumpCorrelation(float clump, float fold, float keep) {
    auto read = [clump](float t, float f) { return gaussianLatticeCovariance(t, std::sqrt(clump * clump + f * f / 2)); };
    float r0 = foldMixture([&](float f) { return read(0, f); }, fold, keep);
    float r1 = foldMixture([&](float f) { return read(1, f); }, fold, keep);
    return r0 > 0 ? r1 / r0 : 0.0f;
}

float FilmEngineInvocation::discreteGrainSigma(float clump, float fold, float keep) {
    return solvedSigma(gaussianClumpCorrelation(clump, fold, keep));
}

float FilmEngineInvocation::booleanGrainCorrelation(float r, float coverageIn, float fold, float keep) {
    if (!(r > 0)) return 0;
    double radius = static_cast<double>(r);
    double coverage = static_cast<double>(swiftMin(swiftMax(coverageIn, 1e-4f), 0.99f));
    auto covariance = [&](double h) { return BooleanGrain::coverageCovariance(h, radius, coverage); };
    double span = swiftMin(2 * radius, 2 + 6 * static_cast<double>(fold));
    int steps = std::min(std::max(static_cast<int>(std::ceil(2 * span / 0.03)), 64), 256) / 2 * 2;
    double step = 2 * span / static_cast<double>(steps);
    std::vector<double> axis(steps);
    for (int i = 0; i < steps; ++i) axis[i] = -span + (static_cast<double>(i) + 0.5) * step;
    std::vector<float> grid(static_cast<size_t>(steps) * steps, 0.0f);
    for (int i = steps / 2; i < steps; ++i) {
        for (int j = steps / 2; j < steps; ++j) {
            float value = static_cast<float>(covariance(std::sqrt(axis[i] * axis[i] + axis[j] * axis[j])));
            grid[static_cast<size_t>(i) * steps + j] = value;
            grid[static_cast<size_t>(steps - 1 - i) * steps + j] = value;
            grid[static_cast<size_t>(i) * steps + (steps - 1 - j)] = value;
            grid[static_cast<size_t>(steps - 1 - i) * steps + (steps - 1 - j)] = value;
        }
    }
    const int radialSteps = 512;
    double radial = 0.0;
    for (int index = 0; index < radialSteps; ++index) {
        double h = 2 * radius * (static_cast<double>(index) + 0.5) / static_cast<double>(radialSteps);
        radial += covariance(h) * h;
    }
    float integral = static_cast<float>(2 * kPi * radial * 2 * radius / static_cast<double>(radialSteps));
    if (!(integral > 0)) return 0;
    auto response = [&](float t, float f) {
        float sigma = f * 0.7071068f;
        std::vector<float> across(steps), down(steps);
        for (int i = 0; i < steps; ++i) {
            across[i] = gaussianLatticeCovariance(t - static_cast<float>(axis[i]), sigma);
            down[i] = gaussianLatticeCovariance(-static_cast<float>(axis[i]), sigma);
        }
        float total = 0;
        for (int i = 0; i < steps; ++i) {
            if (across[i] == 0) continue;
            float row = 0;
            for (int j = 0; j < steps; ++j) row += grid[static_cast<size_t>(i) * steps + j] * down[j];
            total += across[i] * row;
        }
        return total * static_cast<float>(step * step) / integral;
    };
    float r0 = swiftMin(foldMixture([&](float f) { return response(0, f); }, fold, keep), 1.0f);
    float r1 = foldMixture([&](float f) { return response(1, f); }, fold, keep);
    return r0 > 0 ? r1 / r0 : 0.0f;
}

float FilmEngineInvocation::silverEquivalentClumpPixels(float r, float coverage, float fold, float keep) {
    float target = booleanGrainCorrelation(r, coverage, fold, keep);
    if (!(target > 0)) return 0;
    auto correlation = [&](float clump) { return gaussianClumpCorrelation(clump, fold, keep); };
    float high = 1;
    while (correlation(high) < target && high < 64) high *= 2;
    float low = 0;
    if (!(correlation(low) < target)) return 0;
    for (int i = 0; i < 40; ++i) {
        float middle = 0.5f * (low + high);
        if (correlation(middle) < target) low = middle; else high = middle;
    }
    return 0.5f * (low + high);
}

float FilmEngineInvocation::discreteSilverGrainSigma(float r, float coverage, float fold, float keep) {
    return discreteGrainSigma(silverEquivalentClumpPixels(r, coverage, fold, keep), fold, keep);
}

int FilmEngineInvocation::gaussianRadius(float sigma) {
    if (!(sigma > 0.15f)) return 0;
    float variance = 2 * sigma * sigma;
    int limit = static_cast<int>(std::ceil(4 * sigma)) + 1;
    std::vector<float> tail;
    for (int i = 1; i <= limit; ++i) tail.push_back(std::exp(-static_cast<float>(i * i) / variance));
    float tailSum = 0;
    for (float v : tail) tailSum = tailSum + v;
    float total = 1 + 2 * tailSum;
    float dropped = 0;
    for (int radius = limit; radius >= 1; --radius) {
        dropped += 2 * tail[radius - 1];
        if (dropped > 0.0027f * total) return radius;
    }
    return 1;
}

int FilmEngineInvocation::halationBoxRadius(float sigmaPixels) {
    if (!(sigmaPixels > 0.3f)) return 0;
    float boxWidth = std::sqrt(12 * sigmaPixels * sigmaPixels / 3 + 1);
    return static_cast<int>((boxWidth - 1) / 2);
}

int FilmEngineInvocation::boxPyramidSupport(int radius, int stride, float ringRadius) {
    int gridRadius = fotufilm_halation_strided_radius(radius, stride);
    int reach = 3 * gridRadius + static_cast<int>(std::ceil(ringRadius / static_cast<float>(stride)));
    return stride == 1 ? reach + 1 : stride * (reach + 2);
}

float FilmEngineInvocation::sampledGaussianVariance(float sigma) {
    int radius = gaussianRadius(sigma);
    if (!(radius > 0 && sigma > 0)) return 0;
    float total = 0, moment = 0;
    for (int offset = -radius; offset <= radius; ++offset) {
        float weight = std::exp(-static_cast<float>(offset * offset) / (2 * sigma * sigma));
        total += weight;
        moment += weight * static_cast<float>(offset * offset);
    }
    return total > 0 ? moment / total : 0.0f;
}

int32_t FilmEngineInvocation::sceneSideFeatures() {
    using namespace FilmEngineFeature;
    return flare | mtf | mtfLuma | halation | couplers | couplerDiffusion | adjacency | grain | grainMottle |
           donorLayer | annularHalation;
}

int32_t FilmEngineInvocation::staged(int32_t mask, PipelineStage stage) {
    switch (stage) {
        case PipelineStage::full: return mask;
        case PipelineStage::negative: return mask | FilmEngineFeature::densityOut;
        case PipelineStage::print: return (mask & ~sceneSideFeatures()) | FilmEngineFeature::densityIn;
        case PipelineStage::texture: return mask | FilmEngineFeature::texture;
    }
    return mask;
}

float FilmEngineInvocation::effectiveCouplerScale(float requested) {
    float nonnegative = swiftMax(requested, 0.0f);
    if (!(nonnegative > 1)) return nonnegative;
    float overdrive = nonnegative - 1;
    return 1 + overdrive / (1 + overdrive);
}

std::vector<float> FilmEngineInvocation::parameters(const CharacteristicCurve& curve) {
    float gamma = !curve.sampled ? curve.gamma : (curve.dMax() - curve.dMin) / (curve.shoulder - curve.toe);
    return {curve.dMin, gamma, curve.toe, curve.toeWidth, curve.shoulder, curve.shoulderWidth};
}

std::vector<float> FilmEngineInvocation::sampledParameters(const CharacteristicCurve& curve) {
    std::vector<float> record(sampledCurveStride, 0.0f);
    if (curve.sampled) {
        const SampledCharacteristicCurve& sampled = *curve.sampled;
        record[0] = static_cast<float>(sampled.logExposure.size());
        for (size_t i = 0; i < sampled.logExposure.size(); ++i) {
            record[1 + i * 3] = sampled.logExposure[i];
            record[2 + i * 3] = sampled.density[i];
            record[3 + i * 3] = sampled.slopes[i];
        }
    }
    return record;
}

std::vector<float> FilmEngineInvocation::secondaryParameters(const CharacteristicCurve& curve) {
    if (curve.sampled) return {0, 0, 1, 0, 1};
    if (!curve.secondary) return {0, 0, 1, 0, 1};
    const CharacteristicCurveComponent& s = *curve.secondary;
    return {s.gamma, s.toe, s.toeWidth, s.shoulder, s.shoulderWidth};
}

std::optional<float> FilmEngineInvocation::sampledFilmDensity(const std::vector<float>& configuration, int channel,
                                                              float x) {
    if (!(channel >= 0 && channel < 3) || static_cast<int>(configuration.size()) != configurationCount) {
        return std::nullopt;
    }
    size_t base = static_cast<size_t>(sampledCurvesOffset) + static_cast<size_t>(channel) * sampledCurveStride;
    if (!(std::isfinite(configuration[base]) && configuration[base] >= 2 &&
          configuration[base] <= static_cast<float>(SampledCharacteristicCurve::maximumSamples))) {
        return std::nullopt;
    }
    int count = static_cast<int>(configuration[base]);
    auto value = [&](int index, int field) { return configuration[base + 1 + index * 3 + field]; };
    if (x <= value(0, 0)) return value(0, 1);
    if (x >= value(count - 1, 0)) return value(count - 1, 1);
    int low = 0, high = count - 1;
    while (high - low > 1) {
        int mid = (low + high) / 2;
        if (value(mid, 0) <= x) low = mid; else high = mid;
    }
    return SampledCharacteristicCurve::hermite(x, value(low, 0), value(high, 0), value(low, 1), value(high, 1),
                                               value(low, 2), value(high, 2));
}

std::vector<float> FilmEngineInvocation::couplerWarp(const std::vector<CharacteristicCurve>& curves,
                                                     const std::vector<std::vector<float>>& inhibition, float scale,
                                                     const std::vector<float>& releaseGamma,
                                                     const DonorCaptureLayer* donor) {
    const int samples = couplerWarpSamples;
    bool anyInhibition = false;
    for (const auto& row : inhibition) {
        for (float v : row) anyInhibition = anyInhibition || v != 0;
    }
    bool anyDonor = false;
    if (donor) {
        for (float v : donor->inhibition) anyDonor = anyDonor || v != 0;
    }
    if (!(scale > 0) || !(anyInhibition || anyDonor)) return std::vector<float>(3 * samples, 0.0f);
    const int solveCount = 4 * samples;
    const float solveLow = couplerWarpMin - 1;
    const float solveStep = ((couplerWarpMax + 1) - solveLow) / static_cast<float>(solveCount - 1);

    std::vector<std::vector<float>> activation(3, std::vector<float>(solveCount, 0.0f));
    for (int layer = 0; layer < 3; ++layer) {
        const CharacteristicCurve& curve = curves[layer];
        float range = curve.dMax() - curve.dMin;
        if (!(range > 0)) continue;
        for (int i = 0; i < solveCount; ++i) {
            float l = solveLow + static_cast<float>(i) * solveStep;
            float developed = (curve.density(l) - curve.dMin) / range;
            activation[layer][i] = inhibitorRelease(developed, releaseGamma[layer]);
        }
    }
    std::vector<float> donorActivation(solveCount, 0.0f);
    if (donor && donor->curve.dMax() > donor->curve.dMin) {
        float range = donor->curve.dMax() - donor->curve.dMin;
        for (int i = 0; i < solveCount; ++i) {
            float l = solveLow + static_cast<float>(i) * solveStep;
            float developed = (donor->curve.density(l) - donor->curve.dMin) / range;
            donorActivation[i] = inhibitorRelease(developed, donor->releaseGamma);
        }
    }
    std::vector<float> table(3 * samples, 0.0f);
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<float> u(solveCount, 0.0f), offset(solveCount, 0.0f);
        for (int i = 0; i < solveCount; ++i) {
            float released = 0;
            for (int donorLayer = 0; donorLayer < 3; ++donorLayer) {
                released += inhibition[channel][donorLayer] * activation[donorLayer][i];
            }
            if (donor) released += donor->inhibition[channel] * donorActivation[i];
            offset[i] = scale * released;
            u[i] = solveLow + static_cast<float>(i) * solveStep - offset[i];
        }
        for (int i = 1; i < solveCount; ++i) {
            if (u[i] <= u[i - 1]) u[i] = u[i - 1] + 1e-6f;
        }
        int cursor = 0;
        for (int k = 0; k < samples; ++k) {
            float target = couplerWarpMin +
                           (couplerWarpMax - couplerWarpMin) * static_cast<float>(k) / static_cast<float>(samples - 1);
            while (cursor + 2 < solveCount && u[cursor + 1] < target) cursor += 1;
            float span = u[cursor + 1] - u[cursor];
            float t = span > 0 ? swiftMin(swiftMax((target - u[cursor]) / span, 0.0f), 1.0f) : 0.0f;
            table[channel * samples + k] = offset[cursor] + t * (offset[cursor + 1] - offset[cursor]);
        }
    }
    return table;
}

float FilmEngineInvocation::inhibitorRelease(float activation, float gamma) {
    float a = swiftMin(swiftMax(activation, 0.0f), 1.0f);
    if (gamma == 1) return a;
    float released = static_cast<float>(std::pow(static_cast<double>(a), static_cast<double>(gamma)));
    float retained = static_cast<float>(std::pow(static_cast<double>(1 - a), static_cast<double>(gamma)));
    return released / swiftMax(released + retained, std::numeric_limits<float>::denorm_min());
}

const SpectralPipelineTables& FilmEngineInvocation::unusedSpectralTables() {
    static const SpectralPipelineTables& tables = *[] {
        const int dimension = SpectralRuntime::lutDimension;
        SpectralLUT empty(dimension, std::vector<float>(static_cast<size_t>(dimension) * dimension * dimension * 4, 0.0f));
        return new SpectralPipelineTables{empty, empty, std::nullopt};
    }();
    return tables;
}

// MARK: - The invocation

FilmEngineInvocation::FilmEngineInvocation(const FilmStock& sourceStock, const FotufilmEngine::Options& requested,
                                           int width, int height, uint64_t frameIndex, bool noFilm,
                                           bool bindsFilmGrain, const CheckCancellation& checkCancellation)
    : spectral(unusedSpectralTables()) {
    check(checkCancellation);
    FotufilmEngine::Options options = requested;
    // A larger piece of film than the aperture prints on the photograph's own levels.
    if (options.unexposedEdge && options.unexposedEdge->sceneHighlightStops) {
        options.sceneHighlightStops = options.unexposedEdge->sceneHighlightStops;
    }
    if (options.unexposedEdge && options.unexposedEdge->sceneToneStops) {
        options.sceneToneStops = options.unexposedEdge->sceneToneStops;
    }
    if (options.unexposedEdge && options.unexposedEdge->sceneChannelMedians) {
        options.sceneChannelMedians = options.unexposedEdge->sceneChannelMedians;
    }
    // A development condition supplies the fresh roll's curves before age and reciprocity.
    FilmStock developed = sourceStock.pushed(options.developmentEV);
    std::optional<FilmStock> referenceRoll;
    if (options.developmentEV != 0) {
        referenceRoll = sourceStock.expired(swiftMax(options.expiredYears, 0.0f))
                            .reciprocity(options.shutterSeconds.value_or(0.0f));
    }
    const FilmStock stock =
        developed.expired(swiftMax(options.expiredYears, 0.0f)).reciprocity(options.shutterSeconds.value_or(0.0f));
    uint64_t animatedSeed = options.seed + frameIndex * 0x9E3779B97F4A7C15ull;
    PrintPaper printMedium = options.paperFor(stock);
    bool printing = !noFilm && writesPrint(options.stage) && !options.negativeViewing;
    std::optional<PrinterProfile> printer =
        printing ? PrinterProfile::resolved(options.printer, stock, printMedium) : std::nullopt;
    std::optional<PrinterLights> printerLights =
        printing ? PrinterLights::resolved(std::optional<PrinterLights>(options.printerLights), stock, printMedium)
                 : std::nullopt;
    float pxPerMM = options.pixelsPerMM(width, height);
    auto selected = [&](TextureStages spatial) {
        return options.stage != PipelineStage::texture || options.textureStages.contains(spatial);
    };
    std::optional<DiffusionHalo> diffusionHalo;
    if (options.diffusionFilter) {
        float focal = options.focalLengthMM.value_or(options.format.normalFocalLengthMM());
        DiffusionHalo halo = options.diffusionFilter->halo(stock, focal, 1 / pxPerMM,
                                                          static_cast<float>(std::min(width, height)) / 8);
        if (!halo.isIdentity()) diffusionHalo = halo;
    }
    std::vector<int> diffusionRadii = {0, 0, 0};
    if (diffusionHalo) {
        diffusionRadii.clear();
        for (float sigma : diffusionHalo->sigmasPixels) diffusionRadii.push_back(gaussianRadius(sigma));
    }

    std::vector<float> mtfSigmas;
    for (float mm : stock.emulsionDiffusionMM) mtfSigmas.push_back(mm * pxPerMM);
    std::vector<int> mtfRadii;
    for (float sigma : mtfSigmas) mtfRadii.push_back(selected(TextureStages::emulsionMTF()) ? gaussianRadius(sigma) : 0);
    std::vector<float> mtfSecondarySigmas;
    for (float mm : stock.emulsionDiffusionSecondaryMM) mtfSecondarySigmas.push_back(mm * pxPerMM);
    std::vector<int> mtfSecondaryRadii;
    for (float sigma : mtfSecondarySigmas) {
        mtfSecondaryRadii.push_back(selected(TextureStages::emulsionMTF()) ? gaussianRadius(sigma) : 0);
    }
    std::vector<float> mtfPrimaryShare;
    for (float share : stock.emulsionDiffusionPrimaryShare) mtfPrimaryShare.push_back(swiftMin(swiftMax(share, 0.0f), 1.0f));

    // The look scale rides here rather than in the sheet's strengths.
    std::vector<float> returned;
    for (float ratio : HalationReturn::ratios(stock, noFilm ? std::nullopt : options.halationReturnRatio)) {
        returned.push_back(swiftMax(ratio, 0.0f) * options.halationScale * stock.halationLookScale);
    }
    if (!options.halationReturnGain.empty()) {
        std::vector<float> gain =
            HalationSpectrum::recordGain(options.halationReturnGain, stock.spectralProfile.layerSensitivity);
        if (gain.size() == returned.size()) {
            for (size_t i = 0; i < returned.size(); ++i) returned[i] = swiftMax(returned[i] * gain[i], 0.0f);
        }
    }
    float top;
    if (options.halationSourceColour > 0 && sequenceMax(returned, top)) {
        float t = swiftMin(options.halationSourceColour, 1.0f);
        for (float& v : returned) v = v + t * (top - v);
    }
    std::optional<HalationProfile> halationProfile = stock.resolvedHalationProfile(options.useEstimatedHalationProfile);
    Halation::Kernel halation =
        HalationRuntime::kernel(returned, options.format.base, halationProfile,
                                options.halationHazeMM.value_or(stock.halationHazeMM), stock.halationReturnMatrix);
    std::vector<float> halationMix =
        selected(TextureStages::halation()) ? halation.mix : std::vector<float>(halation.mix.size(), 0.0f);
    std::vector<int> halationRadii;
    for (float mm : halation.sigmaMM) halationRadii.push_back(halationBoxRadius(mm * pxPerMM));
    std::vector<float> halationRingRadii;
    for (float mm : halation.ringRadiusMM) halationRingRadii.push_back(swiftMax(mm * pxPerMM, 0.0f));

    float mtfLumaSigma = stock.lumaDiffusionMM * pxPerMM;
    int mtfLumaRadius = selected(TextureStages::emulsionMTF()) ? gaussianRadius(mtfLumaSigma) : 0;

    float couplerSigma =
        selected(TextureStages::adjacency()) ? stock.couplerDiffusionMM * pxPerMM : noSpatialReachPixels;
    int couplerRadius = gaussianRadius(couplerSigma);
    float adjacencyReferenceSigma =
        selected(TextureStages::adjacency()) ? stock.adjacencyRadiusMM * pxPerMM : noSpatialReachPixels;
    bool screenedAdjacency = options.adjacencyModel.value_or(stock.adjacencyModel) == AdjacencyModel::screenedDiffusion;
    float adjacencySigma =
        adjacencyReferenceSigma * (screenedAdjacency ? AdjacencyModelConstants::screenedPrimarySigma : 1.0f);
    float adjacencySecondarySigma =
        screenedAdjacency ? adjacencyReferenceSigma * AdjacencyModelConstants::screenedSecondarySigma : 0.0f;
    int adjacencySecondaryRadius = gaussianRadius(adjacencySecondarySigma);
    int adjacencyRadius = gaussianRadius(adjacencySigma);
    float adjacencyStrength = selected(TextureStages::adjacency()) ? stock.adjacencyStrength : 0.0f;
    float grainScale = selected(TextureStages::grain()) ? options.grainScale : 0.0f;
    std::vector<float> clumpSigmaMM;
    for (float ratio : stock.grainLayerSizeRatio) clumpSigmaMM.push_back(stock.grainClumpSigmaMM() * ratio);
    std::optional<NegativeViewing> negativeViewing = options.negativeViewing;
    if (!negativeViewing && isNegative(printMedium)) negativeViewing = NegativeViewing::lightBox;
    bool showingNegative = writesPrint(options.stage) && negativeViewing && !stock.isReversal;
    float enlargerSigmaPixels = enlargerBlurMM(printMedium) * pxPerMM;
    float enlargerFoldPixels = 0;
    if (!showingNegative && !viewsFilmDirectly(printMedium, stock) && selected(TextureStages::enlarger()) &&
        enlargerSigmaPixels > 0) {
        float shortfall = enlargerSigmaPixels * enlargerSigmaPixels - sampledGaussianVariance(enlargerSigmaPixels);
        enlargerFoldPixels = std::sqrt(swiftMax(shortfall, 0.0f));
    }
    float enlargerFoldKeep =
        enlargerFoldPixels > 0 ? swiftMin(swiftMax(scanSharpening(printMedium), 0.0f), 1.0f) : 1.0f;
    bool silver = stock.grainDensityLaw == GrainDensityLaw::silver;
    float coverage = silver ? stock.grainAnchorCoverage() : 0.0f;
    std::vector<float> renderedClumpMM;
    for (float clump : clumpSigmaMM) {
        renderedClumpMM.push_back(silver ? silverEquivalentClumpPixels(2 * clump * pxPerMM, coverage,
                                                                       enlargerFoldPixels, enlargerFoldKeep) /
                                               pxPerMM
                                         : clump);
    }
    std::vector<float> grainSigmaLayer;
    for (float clump : renderedClumpMM) {
        grainSigmaLayer.push_back(discreteGrainSigma(clump * pxPerMM, enlargerFoldPixels, enlargerFoldKeep));
    }
    float grainSigma = maxOr(grainSigmaLayer, grainSigmaFloorPixels);
    int grainRadius = gaussianRadius(grainSigma);
    const float sqrtPi = std::sqrt(kFloatPi);
    std::vector<float> apertureScale;
    for (float sigma : renderedClumpMM) {
        apertureScale.push_back(sqrtPi * FilmStock::granularityApertureRadiusMM * pxPerMM /
                                FilmStock::granularityApertureResponse(sigma));
    }
    bool filmActive = options.grainModel == GrainModel::film && grainScale > 0 && stock.grainStrength > 0;
    float mottleShare = options.stage != PipelineStage::full || filmActive
                            ? 0.0f
                            : swiftMin(swiftMax(options.grainMottleShare.value_or(stock.grainMottleShare), 0.0f), 0.9f);
    float mottleSizeRatio =
        swiftMin(swiftMax(options.grainMottleSizeRatio.value_or(stock.grainMottleSizeRatio), 1.0f), 8.0f);
    std::vector<float> mottleClumpMM;
    for (float clump : clumpSigmaMM) mottleClumpMM.push_back(clump * mottleSizeRatio);
    std::vector<float> renderedMottleMM;
    for (float clump : mottleClumpMM) {
        renderedMottleMM.push_back(silver ? silverEquivalentClumpPixels(2 * clump * pxPerMM, coverage,
                                                                        enlargerFoldPixels, enlargerFoldKeep) /
                                                pxPerMM
                                          : clump);
    }
    std::vector<float> mottleSigmaLayer;
    for (float mottle : renderedMottleMM) {
        mottleSigmaLayer.push_back(discreteGrainSigma(mottle * pxPerMM, enlargerFoldPixels, enlargerFoldKeep));
    }
    float mottleSigma = maxOr(mottleSigmaLayer, grainSigmaFloorPixels);
    int mottleRadius = gaussianRadius(mottleSigma);
    std::vector<float> mottleApertureScale;
    for (float sigma : renderedMottleMM) {
        mottleApertureScale.push_back(sqrtPi * FilmStock::granularityApertureRadiusMM * pxPerMM /
                                      FilmStock::granularityApertureResponse(sigma));
    }
    std::vector<float> grainStrength, mottleStrength;
    for (size_t layer = 0; layer < stock.grainLayerWeights.size(); ++layer) {
        float weight = stock.grainLayerWeights[layer];
        grainStrength.push_back(stock.grainStrength * grainScale * weight * apertureScale[layer] *
                                std::sqrt(1 - mottleShare));
    }
    for (size_t layer = 0; layer < stock.grainLayerWeights.size(); ++layer) {
        float weight = stock.grainLayerWeights[layer];
        mottleStrength.push_back(stock.grainStrength * grainScale * weight * mottleApertureScale[layer] *
                                 std::sqrt(mottleShare));
    }
    float clumpsPerPixel = stock.grainClumpsPerMM2() / (pxPerMM * pxPerMM);
    float mottleLambda = clumpsPerPixel / (mottleSizeRatio * mottleSizeRatio);
    bool printMTFActive = !showingNegative && !viewsFilmDirectly(printMedium, stock) &&
                          enlargerBlurMM(printMedium) > 0 && options.stage != PipelineStage::negative &&
                          selected(TextureStages::enlarger());
    float printMTFSigma = swiftMax(enlargerBlurMM(printMedium) * pxPerMM, grainSigmaFloorPixels);
    int printMTFRadius = printMTFActive ? gaussianRadius(printMTFSigma) : 0;
    std::vector<float> contrast = !printer ? stock.printingContrastScale(options.printCorrection, printMedium)
                                           : std::vector<float>(3, 1.0f);
    bool levelsPos = levelsPositive(printMedium, stock, options.digitalReference) &&
                     options.stage != PipelineStage::texture;
    std::optional<float> white;
    if (!noFilm && printMedium == PrintPaper::screen && options.digitalReference == DigitalReferenceStyle::autoLevels &&
        readsScene(options.stage) && DigitalReferenceReceiver::keysTone(stock)) {
        white = options.sceneHighlightStops;
    }
    float filmBoost = white ? DigitalReferenceReceiver::filmBoost(stock, *white, options.exposureEV) : 0.0f;
    bool labScanReading = !noFilm && printMedium == PrintPaper::labScan && readsScene(options.stage) &&
                          !stock.isReversal && !stock.isReflectionPrint && options.sceneHighlightStops.has_value();
    std::optional<LabScanTiming::Dodge> dodge;
    if (labScanReading && options.sceneToneStops) {
        Float3 tone = *options.sceneToneStops;
        dodge = LabScanTiming::dodge(AutoAdjustment::SceneStops(tone.x, tone.z, tone.y), options.labScanDodging);
    }
    std::optional<float> toneKey;
    if (white) {
        toneKey = DigitalReferenceReceiver::toneKey(*white) + filmBoost;
    } else if (dodge) {
        toneKey = dodge->key;
    }
    std::optional<DigitalReferenceReceiver::ToneCompression> compression;
    if (white && options.sceneToneStops) {
        Float3 tone = *options.sceneToneStops;
        compression =
            DigitalReferenceReceiver::toneCompression(stock, AutoAdjustment::SceneStops(tone.x, tone.z, tone.y), *white);
    }
    if (!compression && dodge) compression = DigitalReferenceReceiver::ToneCompression{dodge->hold, dodge->lift};
    LabScanTiming::Setup labScanSetup = LabScanTiming::Setup::identity();
    if (printMedium == PrintPaper::labScan && !noFilm && !showingNegative) {
        std::optional<float> median;
        if (options.sceneToneStops) median = options.sceneToneStops->x;
        labScanSetup = LabScanTiming::setup(stock, labScanReading ? options.sceneHighlightStops : std::nullopt, median,
                                            options.exposureEV, options.screenExposureEV, options.screenCMY);
    }
    std::vector<CharacteristicCurve> filmCurves = labScanSetup.curves(stock);
    Float3 levelsScale, levelsShift;
    if (printMedium == PrintPaper::screen && (levelsPos || !stock.isReversal)) {
        std::optional<float> highlight;
        if (options.sceneHighlightStops) highlight = *options.sceneHighlightStops + filmBoost;
        ReceiverLevels screen =
            DigitalReferenceReceiver::levels(stock, options.digitalReference, highlight, options.exposureEV);
        levelsScale = Float3::repeating(screen.scale);
        levelsShift = Float3::repeating(screen.shift);
    } else {
        levelsScale = labScanSetup.scale;
        levelsShift = labScanSetup.print;
    }
    Float3 autoColour;
    if (printMedium == PrintPaper::screen && options.digitalReference == DigitalReferenceStyle::autoLevels) {
        std::optional<Float3> medians;
        if (options.sceneChannelMedians) medians = *options.sceneChannelMedians + filmBoost;
        autoColour = DigitalReferenceReceiver::autoColourShift(stock, medians, options.exposureEV);
    }
    for (int c = 0; c < 3; ++c) contrast[c] = contrast[c] * levelsScale[c];
    std::vector<float> masking;
    for (float v : contrast) masking.push_back(v * exposureDirection(printMedium));
    std::vector<CharacteristicCurve> paperCurves = printCurves(printMedium, stock, options.digitalReference);
    const CharacteristicCurve& paper = paperCurves[1];
    Float3 screenShift = levelsShift + SpectralRuntime::screenExposureShift(options.screenExposureEV, stock, printMedium,
                                                                            options.digitalReference,
                                                                            options.screenGrade);
    std::vector<float> xMids = printExposureMidpoints(printMedium, stock, options.digitalReference);
    for (int c = 0; c < 3; ++c) xMids[c] = xMids[c] + exposureDirection(printMedium) * (screenShift[c] + autoColour[c]);
    Float3 colourShift = DigitalReferenceReceiver::colourShift(options.screenCMY, stock, printMedium);
    for (int c = 0; c < 3; ++c) xMids[c] += colourShift[c];
    if (printer && printer->exposureEV != 0) {
        float shift = exposureDirection(printMedium) * printer->exposureEV * std::log10(2.0f);
        for (float& v : xMids) v = v + shift;
    }
    float xMid = xMids[1];

    // Two lenses' worth of veiling glare, and only the taking lens's is optional.
    float veilingGlare = swiftMin(stock.flare * swiftMax(0.0f, options.flareScale) + kNoLensFilterGlare, 0.99f);

    int32_t mask = 0;
    if (veilingGlare > 0) mask |= FilmEngineFeature::flare;
    if (diffusionHalo && maxInt(diffusionRadii, 0) > 0) mask |= FilmEngineFeature::diffusion;
    bool primaryMTFActive = false, secondaryMTFActive = false;
    for (size_t i = 0; i < std::min(mtfRadii.size(), mtfPrimaryShare.size()); ++i) {
        if (mtfRadii[i] > 0 && mtfPrimaryShare[i] > 0) primaryMTFActive = true;
    }
    for (size_t i = 0; i < std::min(mtfSecondaryRadii.size(), mtfPrimaryShare.size()); ++i) {
        if (mtfSecondaryRadii[i] > 0 && mtfPrimaryShare[i] < 1) secondaryMTFActive = true;
    }
    if (primaryMTFActive || secondaryMTFActive) mask |= FilmEngineFeature::mtf;
    bool mtfLumaActive = (mask & FilmEngineFeature::mtf) != 0 && stock.mtfLumaShare > 0 && mtfLumaRadius > 0;
    bool mtfProfileActive = (mask & FilmEngineFeature::mtf) != 0 && (mtfLumaActive || secondaryMTFActive);
    if (mtfProfileActive) mask |= FilmEngineFeature::mtfLuma;
    bool anyMix = false, anyRadius = false;
    for (float v : halationMix) anyMix = anyMix || v > 0;
    for (int v : halationRadii) anyRadius = anyRadius || v > 0;
    for (float v : halationRingRadii) anyRadius = anyRadius || v > 0;
    if (anyMix && anyRadius) mask |= FilmEngineFeature::halation;
    std::vector<std::vector<float>> inhibition = stock.couplerInhibition;
    if (stock.couplerGeometry()) {
        const CouplerGeometry& geometry = *stock.couplerGeometry();
        size_t gaps = geometry.layerDepthUM.size() - 1;
        std::vector<float> reach =
            options.couplerGapReachScales.value_or(std::vector<float>(gaps, options.couplerRangeScale));
        inhibition = geometry.matrix(reach, options.couplerSelfScale);
    }
    float couplerScale = effectiveCouplerScale(options.couplerScale);
    bool anyInhibition = false, hasCrossLayerInhibition = false;
    for (size_t receiver = 0; receiver < inhibition.size(); ++receiver) {
        for (size_t donor = 0; donor < inhibition[receiver].size(); ++donor) {
            if (inhibition[receiver][donor] != 0) {
                anyInhibition = true;
                if (receiver != donor) hasCrossLayerInhibition = true;
            }
        }
    }
    bool couplersActive = couplerScale > 0 && anyInhibition;
    float requestedFringeAmount = options.chromaticFringeAmount.value_or(stock.chromaticFringeAmount);
    float requestedFringeMM = options.chromaticFringeRadiusMM.value_or(stock.chromaticFringeRadiusMM);
    float fringeAmount =
        std::isfinite(requestedFringeAmount) ? swiftMin(swiftMax(requestedFringeAmount, 0.0f), 1.0f) : 0.0f;
    float fringeMM = std::isfinite(requestedFringeMM) ? swiftMin(swiftMax(requestedFringeMM, 0.0f), 2.0f) : 0.0f;
    bool fringeActive = selected(TextureStages::adjacency()) && couplersActive && !stock.isMonochrome &&
                        hasCrossLayerInhibition && fringeAmount > 0 && fringeMM > stock.couplerDiffusionMM &&
                        gaussianRadius(fringeMM * pxPerMM) > 0;
    float fringeSigma = fringeActive ? fringeMM * pxPerMM : noSpatialReachPixels;
    int fringeRadius = fringeActive ? gaussianRadius(fringeSigma) : 0;
    if (couplersActive) mask |= FilmEngineFeature::couplers;
    const DonorCaptureLayer* donor = stock.donorLayers.empty() ? nullptr : &stock.donorLayers[0];
    bool donorInhibits = false;
    if (donor) {
        for (float v : donor->inhibition) donorInhibits = donorInhibits || v != 0;
    }
    bool donorActive = couplerScale > 0 && donorInhibits && SpectralRuntime::hasReconstructionModel() &&
                       !stock.isMonochrome && stock.grainDensityLaw != GrainDensityLaw::silver;
    if (donorActive) mask |= FilmEngineFeature::donorLayer;
    if ((couplersActive || donorActive) && std::max(couplerRadius, fringeRadius) > 0) {
        mask |= FilmEngineFeature::couplerDiffusion;
    }
    if (couplerScale > 0 && adjacencyStrength > 0 && std::max(adjacencyRadius, adjacencySecondaryRadius) > 0) {
        mask |= FilmEngineFeature::adjacency;
    }
    if (grainScale > 0 && stock.grainStrength > 0) {
        mask |= FilmEngineFeature::grain;
        if (mottleShare > 0) mask |= FilmEngineFeature::grainMottle;
    }
    if (printMTFActive && printMTFRadius > 0) mask |= FilmEngineFeature::printMTF;
    if ((viewsFilmDirectly(printMedium, stock) && !levelsPos) || showingNegative) mask |= FilmEngineFeature::reversal;
    if (stock.isMonochrome) mask |= FilmEngineFeature::monochrome;
    mask = staged(mask, options.stage);
    if (noFilm) mask = FilmEngineFeature::noFilm;

    std::vector<float> config;
    config.reserve(configurationCount);
    for (const CharacteristicCurve& curve : filmCurves) append(config, parameters(curve));
    append(config, halationMix);
    for (const auto& row : inhibition) append(config, row);
    append(config, grainStrength);
    append(config, parameters(paper));
    append(config, masking);
    append(config, mtfSigmas);
    append(config, mtfRadii);
    append(config, halationRadii);
    append(config, std::vector<float>{couplerSigma, static_cast<float>(couplerRadius)});
    append(config, std::vector<float>{adjacencySigma, static_cast<float>(adjacencyRadius)});
    append(config, std::vector<float>{grainSigma, static_cast<float>(grainRadius)});
    append(config, std::vector<float>{veilingGlare, couplerScale, stock.adjacencyStrength * couplerScale,
                                      clumpsPerPixel, std::exp2(options.exposureEV + filmBoost), xMid});
    append(config, std::vector<float>{-1, -1, -1});
    std::vector<std::vector<float>> warpInhibition = inhibition;
    if (!couplersActive) {
        for (auto& row : warpInhibition) std::fill(row.begin(), row.end(), 0.0f);
    }
    append(config, couplerWarp(stock.curves, warpInhibition, (couplersActive || donorActive) ? couplerScale : 0.0f,
                               stock.couplerReleaseGamma, donorActive ? donor : nullptr));
    Float3 balance = noFilm ? options.sceneLightGains() : Float3(1, 1, 1);
    append(config, std::vector<float>{balance.x, balance.y, balance.z});
    float highlights = options.highlights;
    float keptHeadroom = HDRHighlightRange::keptHeadroom(options.sceneHeadroom, options.hdrRange);
    if (keptHeadroom > 1) {
        AutoAdjustment::Latitude window =
            noFilm ? AutoAdjustment::Latitude{0, AutoAdjustment::kDiffuseWhiteStops()}
                   : AutoAdjustment::latitude(stock, options.printCorrection, printMedium,
                                              callierCoefficient(options.enlarger, stock, printMedium));
        highlights = swiftMax(-1.0f, swiftMin(1.0f, highlights + options.hdrRollOff *
                                                                     AutoAdjustment::headroomHighlights(keptHeadroom,
                                                                                                        window)));
    }
    float shadowLift = swiftMin(compression ? compression->lift : 0.0f, swiftMax(1 - options.shadows, 0.0f));
    float held = swiftMax(highlights - (compression ? compression->hold : 0.0f), -1.0f);
    append(config, std::vector<float>{held, options.shadows + shadowLift, options.saturation, options.vibrance});
    for (const auto& row : halation.weights) append(config, row);
    append(config, std::vector<float>{stock.grainLumaCorrelation});
    append(config, std::vector<float>{mtfLumaActive ? stock.mtfLumaShare : 0.0f, mtfLumaSigma,
                                      static_cast<float>(mtfLumaRadius)});
    append(config, options.grade.packed());
    append(config, std::vector<float>{static_cast<float>(width), static_cast<float>(height)});
    append(config, std::vector<float>{1, 1});
    std::vector<float> toneGrid(2 * toneGridCells, 0.0f);
    toneGrid[0] = 1;
    toneGrid[toneGridCells] = -(toneKey.value_or(0.0f));
    append(config, toneGrid);

    // The film grain model needs no variant: its tiles are a runtime input.
    append(config, std::vector<float>{filmActive ? 1.0f : 0.0f});
    append(config, std::vector<float>{options.gradeSpace == ColorGrade::Space::encoded ? 1.0f : 0.0f});
    append(config, std::vector<float>{mottleSigma, static_cast<float>(mottleRadius), mottleLambda});
    append(config, mottleStrength);
    append(config, parameters(paperCurves[0]));
    append(config, parameters(paperCurves[2]));
    append(config, std::vector<float>{xMids[0], xMids[2]});
    append(config, std::vector<float>{static_cast<float>(rawValue(stock.grainDensityLaw))});
    for (int layer = 0; layer < 3; ++layer) config.push_back(stock.granularityAnchorDensity(layer));
    append(config, std::vector<float>(3, stock.grainFogDensity));
    append(config, grainSigmaLayer);
    append(config, mottleSigmaLayer);
    append(config, std::vector<float>{printMTFSigma, static_cast<float>(printMTFRadius)});
    append(config, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1});
    append(config, std::vector<float>{0});
    append(config, std::vector<float>{0, 0, 0, 0, 0, 0});
    append(config, std::vector<float>{0});
    append(config, std::vector<float>{printMTFActive ? swiftMin(swiftMax(scanSharpening(printMedium), 0.0f), 1.0f)
                                                     : 0.0f});
    // The diffusion filter. With no filter the direct share is 1 and every kernel weight is 0.
    bool diffusionActive = (mask & FilmEngineFeature::diffusion) != 0;
    config.push_back(diffusionActive && diffusionHalo ? diffusionHalo->directShare : 1.0f);
    if (diffusionActive && diffusionHalo) {
        for (size_t row = 0; row < std::min<size_t>(3, diffusionHalo->weights.size()); ++row) {
            for (float v : diffusionHalo->weights[row]) config.push_back(v * diffusionHalo->scatteredShare);
        }
    } else {
        append(config, std::vector<float>(9, 0.0f));
    }
    append(config, diffusionRadii);
    if (donorActive) {
        append(config, parameters(labScanSetup.uncoloured(donor->curve)));
        append(config, donor->inhibition);
    } else {
        append(config, std::vector<float>(6, 0.0f));
        append(config, std::vector<float>{0, 0, 0});
    }
    append(config, halationRingRadii);
    if (selected(TextureStages::halation())) {
        for (const auto& row : halation.matrix) append(config, row);
    } else {
        append(config, std::vector<float>(9, 0.0f));
    }
    for (const CharacteristicCurve& curve : filmCurves) append(config, secondaryParameters(curve));
    append(config, mtfSecondarySigmas);
    append(config, mtfSecondaryRadii);
    append(config, mtfPrimaryShare);
    append(config, stock.couplerReleaseGamma);
    config.push_back(donor ? donor->releaseGamma : 1.0f);
    if (diffusionActive && diffusionHalo && diffusionHalo->weights.size() > 3) {
        for (float v : diffusionHalo->weights[3]) config.push_back(v * diffusionHalo->scatteredShare);
    } else {
        append(config, std::vector<float>{0, 0, 0});
    }
    config.push_back(stock.isReversal ? 1.0f : 0.0f);
    {
        const std::vector<float>& green = stock.grainDensityProfile.records()[1];
        for (size_t i = 0; i < std::min<size_t>(3, green.size()); ++i) config.push_back(green[i]);
    }
    config.push_back(options.sdrShoulderKnee(stock));
    append(config, std::vector<float>{0, 0, 0, 0});
    for (const CharacteristicCurve& curve : filmCurves) append(config, sampledParameters(curve));
    append(config, std::vector<float>{screenedAdjacency ? 1.0f : 0.0f, adjacencySecondarySigma,
                                      static_cast<float>(adjacencySecondaryRadius)});
    append(config, std::vector<float>{fringeActive ? fringeAmount : 0.0f, fringeSigma, static_cast<float>(fringeRadius)});
    config.push_back(0);
    append(config, stock.grainReversalProfile);
    float cameraPreflash = !noFilm ? swiftMax(0.0f, options.cameraPreflash) : 0.0f;
    float printerPreflash = (!noFilm && writesPrint(options.stage) && !options.negativeViewing &&
                             EnlargerOptics::illuminates(stock, printMedium))
                                ? swiftMax(0.0f, options.printerPreflash)
                                : 0.0f;
    append(config, std::vector<float>{cameraPreflash, printerPreflash});
    append(config, std::vector<float>{0, 0});
    for (const auto& record : stock.grainDensityProfile.records()) append(config, record);
    if (filmActive) {
        filmGrainPopulation = FilmGrain::Population{stock, referenceRoll};
    }
    if (filmActive && bindsFilmGrain) {
        std::shared_ptr<const FilmGrain::TileBinding> film =
            FilmGrain::binding(stock, referenceRoll ? &*referenceRoll : nullptr, checkCancellation);
        check(checkCancellation);
        filmTileBinding = film;
        append(config, film->configurationBlock(pxPerMM, grainScale, options.filmGrain));
        check(checkCancellation);
    } else {
        filmTileBinding = nullptr;
        append(config, std::vector<float>(FOTUFILM_CONFIG_FILM_TILE_COUNT, 0.0f));
    }
    append(config, options.gateConfiguration(width, height));
    append(config, std::vector<float>{HDRHighlightRange::knee,
                                      HDRHighlightRange::curvature(options.sceneHeadroom, options.hdrRange)});
    config.push_back(options.gateCornerConfiguration(width, height));
    append(config, options.carrierConfiguration(width, height));
    append(config, options.stage == PipelineStage::print ? options.printFinish.packed() : PrintFinish::neutral().packed());
    // A print span handed a scanned negative is not ported; every span reads densities.
    append(config, ApproximateNegativeScan::packedReadingNone());
    preconditionCheck(static_cast<int>(config.size()) == configurationCount,
                      "configuration length differs from FOTUFILM_FRAME_CONFIGURATION_COUNT");

    int optical = 0;
    int lightReach = 0;
    if (mask & FilmEngineFeature::mtf) {
        lightReach = std::max(std::max(maxInt(mtfRadii, 0), maxInt(mtfSecondaryRadii, 0)),
                              mtfLumaActive ? mtfLumaRadius : 0);
        optical += lightReach;
    }
    if (mask & FilmEngineFeature::diffusion) {
        std::vector<int> reaches;
        for (int radius : diffusionRadii) {
            reaches.push_back(boxPyramidSupport(radius, fotufilm_diffusion_stride(radius)));
        }
        int reach = maxInt(reaches, 0);
        optical += reach;
        lightReach += reach;
    }
    int halationReach = 0;
    if (mask & FilmEngineFeature::halation) {
        std::vector<int> reaches;
        for (size_t i = 0; i < std::min(halationRadii.size(), halationRingRadii.size()); ++i) {
            reaches.push_back(boxPyramidSupport(halationRadii[i], fotufilm_halation_stride(halationRadii[i]),
                                                halationRingRadii[i]));
        }
        halationReach = maxInt(reaches, 0);
        optical += halationReach;
    }
    int diffusion = 0;
    if (mask & FilmEngineFeature::couplers) {
        diffusion = std::max(std::max(diffusion, fotufilm_gaussian_grid_reach(couplerSigma, couplerRadius)),
                             fotufilm_gaussian_grid_reach(fringeSigma, fringeRadius));
    }
    if (mask & FilmEngineFeature::adjacency) {
        diffusion = std::max(std::max(diffusion, fotufilm_gaussian_grid_reach(adjacencySigma, adjacencyRadius)),
                             fotufilm_gaussian_grid_reach(adjacencySecondarySigma, adjacencySecondaryRadius));
    }
    optical += diffusion;
    int grainSupport = (mask & FilmEngineFeature::grain) ? grainRadius : 0;
    int printSupport = (mask & FilmEngineFeature::printMTF) ? printMTFRadius : 0;
    spatialSupport = std::max(optical, grainSupport) + printSupport;
    spatialSupportSansHalation = std::max(optical - halationReach, grainSupport) + printSupport;
    lightSupport = lightReach;
    halationSupport = halationReach;
    halationPixelRadii.assign(halationRadii.begin(), halationRadii.end());

    localToneEnabled = options.localTone && readsScene(options.stage);
    configuration = std::move(config);
    bool screenMeters = printMedium == PrintPaper::screen && (!stock.isReversal || levelsPos) &&
                        options.digitalReference == DigitalReferenceStyle::autoLevels;
    bool labScanMetersHere = printMedium == PrintPaper::labScan && !stock.isReversal;
    if (!noFilm && !showingNegative && !stock.isReflectionPrint && (screenMeters || labScanMetersHere) &&
        readsScene(options.stage) && !options.sceneHighlightStops) {
        meterStock_ = stock;
        meterMedium_ = printMedium;
        meterExposureEV_ = options.exposureEV;
        meterDodging_ = options.labScanDodging;
        meterScanExposure_ = options.screenExposureEV;
        meterKeys_ = options.screenCMY;
        meterLevels_ = MeteredLevels();
        meterLevels_.scale = levelsScale;
        meterLevels_.shift = levelsShift + autoColour;
        meterLevels_.film = labScanSetup;
    } else if (toneKey) {
        meterLevels_ = MeteredLevels();
        meterLevels_.scale = levelsScale;
        meterLevels_.shift = levelsShift + autoColour;
        meterLevels_.film = labScanSetup;
        meterLevels_.filmBoost = filmBoost;
        meterLevels_.toneKey = toneKey;
        meterLevels_.shadowLift = shadowLift;
        meterLevels_.highlightHold = highlights - held;
    }
    if (noFilm) {
        spectral = unusedSpectralTables();
        spectralCacheID = 0x4E4F46494C4D0000ull;
    } else if (showingNegative && negativeViewing) {
        SpectralPipelineTables base = SpectralRuntime::tables(stock);
        spectral = SpectralPipelineTables{base.exposure,
                                          SpectralRuntime::negativeViewing(stock, *negativeViewing, options.bleachBypass),
                                          std::nullopt};
        SpectralRuntime::TableOptions tableOptions;
        tableOptions.bleachBypass = options.bleachBypass;
        spectralCacheID = SpectralRuntime::cacheIdentifier(stock, kDefaultPrintPaper, tableOptions) ^
                          (0x4E45474154495645ull + ordinal(*negativeViewing));
    } else {
        SpectralRuntime::TableOptions tableOptions;
        tableOptions.bleachBypass = options.bleachBypass;
        tableOptions.printViewingKelvin = options.printViewingKelvin;
        tableOptions.callier = callierCoefficient(options.enlarger, stock, printMedium);
        tableOptions.printer = printer;
        tableOptions.printerLights = printerLights;
        tableOptions.digitalReference = options.digitalReference;
        tableOptions.screenGrade = options.screenGrade;
        tableOptions.screenExposureEV = options.screenExposureEV;
        tableOptions.labScanLook = options.labScanLook;
        tableOptions.receiverBands = options.receiverBands;
        spectral = SpectralRuntime::tables(stock, printMedium, tableOptions);
        spectralCacheID = SpectralRuntime::cacheIdentifier(stock, printMedium, tableOptions);
    }
    // One resolved spectrum controls both integration and upload identity.
    if (!noFilm) {
        std::vector<float> illuminant = options.resolvedSceneSpectrum(stock.referenceIlluminantKelvin);
        SpectralLUT exposure = SpectralRuntime::sceneExposure(stock, illuminant);
        spectral = SpectralPipelineTables{exposure, spectral.filmOutput, spectral.paperOutput};
        spectralCacheID = (spectralCacheID ^ SpectralRuntime::illuminantSignature(illuminant)) * 0x100000001b3ull;
    }
    featureMask = mask;
    seed = static_cast<uint32_t>(animatedSeed);
}

// MARK: - Output transform and veiling glare

void FilmEngineInvocation::setOutputTransform(const FilmOutputTransform& transform) {
    for (size_t index = 0; index < transform.matrix.size(); ++index) {
        configuration[outputMatrixOffset + index] = transform.matrix[index];
    }
    configuration[outputTransferOffset] = static_cast<float>(static_cast<int32_t>(transform.transfer));
    for (size_t index = 0; index < transform.coefficients.size(); ++index) {
        configuration[outputCoefficientsOffset + index] = transform.coefficients[index];
    }
    configuration[outputPremultipliedOffset] = transform.premultiplied ? 1.0f : 0.0f;
    configuration[outputShoulderOffset] = transform.shoulderKnee.value_or(-1.0f);
    configuration[outputGamutOffset] = transform.gamutLuminance ? 1.0f : 0.0f;
    Float3 luminance = transform.gamutLuminance.value_or(Float3());
    for (int channel = 0; channel < 3; ++channel) configuration[outputGamutOffset + 1 + channel] = luminance[channel];
}

std::optional<Float3> FilmEngineInvocation::flareMean() const {
    float red = configuration[flareMeanOffset];
    if (!(red >= 0)) return std::nullopt;
    return Float3(red, configuration[flareMeanOffset + 1], configuration[flareMeanOffset + 2]);
}

void FilmEngineInvocation::setFlareMean(std::optional<Float3> value) {
    Float3 v = value.value_or(Float3(-1, -1, -1));
    configuration[flareMeanOffset] = v.x;
    configuration[flareMeanOffset + 1] = v.y;
    configuration[flareMeanOffset + 2] = v.z;
}

void FilmEngineInvocation::withExposureSampler(const std::function<void(const ExposureSampler&)>& body) const {
    float gain = configuration[exposureGainOffset] / 0.18f;
    Float3 balance(configuration[whiteBalanceOffset], configuration[whiteBalanceOffset + 1],
                   configuration[whiteBalanceOffset + 2]);
    float highlights = configuration[sceneAdjustOffset];
    float shadows = configuration[sceneAdjustOffset + 1];
    float saturation = configuration[sceneAdjustOffset + 2];
    float vibrance = configuration[sceneAdjustOffset + 3];
    bool neutral = highlights == 0 && shadows == 0 && saturation == 1 && vibrance == 0;
    int gridWidth = std::max(1, static_cast<int>(configuration[toneGridSizeOffset]));
    int gridHeight = std::max(1, static_cast<int>(configuration[toneGridSizeOffset + 1]));
    float frameWidth = swiftMax(configuration[frameSizeOffset], 1.0f);
    float frameHeight = swiftMax(configuration[frameSizeOffset + 1], 1.0f);
    ExposureSampler sampler{gain,
                            balance,
                            highlights,
                            shadows,
                            saturation,
                            vibrance,
                            neutral,
                            spectral.exposure.data(),
                            spectral.exposure.dimension(),
                            ColorScience::luminanceWeights,
                            gridWidth,
                            gridHeight,
                            frameWidth,
                            frameHeight,
                            configuration.data() + toneGridAOffset,
                            configuration.data() + toneGridBOffset};
    body(sampler);
}

Float3 FilmEngineInvocation::measuredFlareMean(const float* linearRGBA, int width, int rows) const {
    if (!(width > 0 && rows > 0)) return Float3();
    Double3 mean = flareExposureSum(linearRGBA, width, rows) / static_cast<double>(static_cast<int64_t>(width) * rows);
    return Float3(static_cast<float>(mean.x), static_cast<float>(mean.y), static_cast<float>(mean.z));
}

Float3 FilmEngineInvocation::measuredAreaWeightedFlareMean(const float* linearRGBA, int width, int height) const {
    return measuredAreaWeightedFlareMean(FilmFlareFrame::linearRec2020RGBA(linearRGBA, width, height));
}

Float3 FilmEngineInvocation::measuredAreaWeightedFlareMean(const FilmFlareFrame& frame) const {
    Double3 total;
    withExposureSampler([&](const ExposureSampler& sampler) {
        for (size_t index = 0; index < frame.averages().size(); ++index) {
            Double3 exposure = sampler.neutral
                                   ? sampler.exposure(frame.averages()[index], 0, 0)
                                   : sampler.exposure(frame.averages()[index], frame.positions()[index].x,
                                                      frame.positions()[index].y);
            total += exposure * static_cast<double>(frame.counts()[index]);
        }
    });
    Double3 mean = total / static_cast<double>(static_cast<int64_t>(frame.width()) * frame.height());
    return Float3(static_cast<float>(mean.x), static_cast<float>(mean.y), static_cast<float>(mean.z));
}

Double3 FilmEngineInvocation::flareExposureSum(const float* pixels, int width, int rows) const {
    Double3 total;
    withExposureSampler([&](const ExposureSampler& sampler) {
        size_t count = static_cast<size_t>(width) * rows;
        for (size_t index = 0; index < count; ++index) {
            Float3 rgb(pixels[index * 4], pixels[index * 4 + 1], pixels[index * 4 + 2]);
            total += sampler.neutral ? sampler.exposure(rgb, 0, 0)
                                     : sampler.exposure(rgb, static_cast<int>(index % width),
                                                        static_cast<int>(index / width));
        }
    });
    return total;
}

void FilmEngineInvocation::flareExposureRowSums(const float* pixels, int width, int rows, int firstRow,
                                                Double3* sums) const {
    if (!(width > 0 && rows > 0)) return;
    withExposureSampler([&](const ExposureSampler& sampler) {
        ParallelWork::forEach(rows, [&](int row) {
            Double3 total;
            const float* scan = pixels + static_cast<size_t>(row) * width * 4;
            int y = firstRow + row;
            for (int x = 0; x < width; ++x) {
                total += sampler.exposure(Float3(scan[x * 4], scan[x * 4 + 1], scan[x * 4 + 2]), x, y);
            }
            sums[row] = total;
        });
    });
}

void FilmEngineInvocation::flareExposureRowSums(const float* red, const float* green, const float* blue, int width,
                                                int rows, int firstRow, Double3* sums) const {
    if (!(width > 0 && rows > 0)) return;
    withExposureSampler([&](const ExposureSampler& sampler) {
        ParallelWork::forEach(rows, [&](int row) {
            Double3 total;
            size_t start = static_cast<size_t>(row) * width;
            for (size_t index = start; index < start + width; ++index) {
                Float3 rgb(red[index], green[index], blue[index]);
                total += sampler.neutral ? sampler.exposure(rgb, 0, 0)
                                         : sampler.exposure(rgb, static_cast<int>(index % width),
                                                            firstRow + static_cast<int>(index / width));
            }
            sums[row] = total;
        });
    });
}

void FilmEngineInvocation::packFilmCurves(const FilmStock& stock, const LabScanTiming::Setup& setup) {
    std::vector<CharacteristicCurve> curves = setup.curves(stock);
    auto write = [&](const std::vector<float>& values, int offset) {
        std::copy(values.begin(), values.end(), configuration.begin() + offset);
    };
    std::vector<float> packed, secondary, sampled;
    for (const CharacteristicCurve& curve : curves) {
        append(packed, parameters(curve));
        append(secondary, secondaryParameters(curve));
        append(sampled, sampledParameters(curve));
    }
    write(packed, FOTUFILM_CONFIG_CURVES);
    write(secondary, curveSecondaryOffset);
    write(sampled, sampledCurvesOffset);
    if (!stock.donorLayers.empty() && (featureMask & FilmEngineFeature::donorLayer) != 0) {
        write(parameters(setup.uncoloured(stock.donorLayers[0].curve)), FOTUFILM_CONFIG_DONOR_CURVE);
    }
}

}  // namespace fotufilm
