// Port of Sources/FotufilmCore/SpectralModel.swift: SpectralRuntime's table cache and builder
// (`tables`, `cacheIdentifier`, `buildTables`), the scan characterization (SpectralResponseInverse,
// ScanOutputCalibration), the negative and direct views, the neutral density basis, and the Digital
// Reference readers.
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "BoundedCache.h"
#include "ColorScience.h"
#include "Enlarger.h"
#include "Illuminant.h"
#include "LabScanFinish.h"
#include "PrintPaperTables.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace SpectralRuntime {

namespace {

uint64_t bits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

constexpr uint64_t kPrime = 0x100000001b3ULL;

struct TableCache {
    std::mutex lock;
    std::condition_variable built;
    /// Bounded: every custom-film slider position, aged year and viewing lamp is a stock of its own.
    BoundedCache<uint64_t, SpectralPipelineTables> cache{40};
    /// Keys a thread is building right now.
    std::set<uint64_t> building;
};

TableCache& tableCache() {
    static TableCache cache;
    return cache;
}

std::mutex& negativeLock() {
    static std::mutex lock;
    return lock;
}

BoundedCache<uint64_t, SpectralLUT>& negativeCache() {
    static BoundedCache<uint64_t, SpectralLUT> cache(16);
    return cache;
}

}  // namespace

// Shared with SpectralRuntimePrint.cpp: the printing balance and the positive read cache.
std::mutex& balanceLock() {
    static std::mutex lock;
    return lock;
}

RetainedTables retainedTables() {
    TableCache& tc = tableCache();
    std::lock_guard<std::mutex> guard(tc.lock);
    size_t bytes = 0;
    tc.cache.forEachValue([&](const SpectralPipelineTables& tables) {
        bytes += (tables.exposure.values().size() + tables.filmOutput.values().size() +
                  (tables.paperOutput ? tables.paperOutput->values().size() : 0)) *
                 sizeof(float);
    });
    return {tc.cache.count(), bytes};
}

std::optional<float> printLightKelvin(std::optional<float> cct) {
    if (!cct || !std::isfinite(*cct) || !(*cct > 0)) return std::nullopt;
    // Bound the input before quantizing, and snap both the input and the rounded bucket to the
    // named standards so canonicalization is idempotent.
    float bounded = clamp(*cct, 1000.0f, 25000.0f);
    const float standards[3] = {2856, 5003, 6504};
    for (float standard : standards) {
        if (std::fabs(bounded - standard) < printLightBucketKelvin / 2) return standard;
    }
    float bucket = std::round(bounded / printLightBucketKelvin) * printLightBucketKelvin;
    for (float standard : standards) {
        if (std::fabs(bucket - standard) < printLightBucketKelvin / 2) return standard;
    }
    return bucket;
}

std::vector<float> printLightSPD(float kelvin) {
    return kelvin >= 4000 ? Illuminant::daylight(kelvin) : Illuminant::planckian(kelvin);
}

std::optional<std::vector<float>> referenceViewingLight(PrintPaper paper) {
    if (isProjected(paper)) return Illuminant::xenonProjection();
    if (acceptsViewingIlluminant(paper)) return Illuminant::d50();
    return std::nullopt;
}

namespace {
SpectralPipelineTables buildTables(const FilmStock& stock, PrintPaper paper, float bleachBypass,
                                   std::optional<float> printViewingKelvin, float callier,
                                   const std::optional<PrinterProfile>& printer,
                                   const std::optional<PrinterLights>& printerLights,
                                   DigitalReferenceStyle digitalReference, float screenGrade, float screenExposureEV,
                                   float labScanLook, const ReceiverBands& receiverBands);
}  // namespace

SpectralPipelineTables tables(const FilmStock& stock, PrintPaper requestedPaper, const TableOptions& options) {
    PrintPaper paper = resolved(requestedPaper, stock);
    float labScanLook = effectiveLabScanLook(options.labScanLook, paper);
    ReceiverBands receiverBands = effectiveReceiverBands(options.receiverBands, stock, paper);
    std::optional<PrinterProfile> printer = PrinterProfile::resolved(options.printer, stock, paper);
    std::optional<PrinterLights> printerLights = PrinterLights::resolved(options.printerLights, stock, paper);
    float bleachBypass = retainedSilverFraction(options.bleachBypass, stock);
    float screenGrade = effectiveScreenGrade(options.screenGrade, stock, paper, options.digitalReference);
    float screenExposureEV = directViewExposure(options.screenExposureEV, stock, paper, options.digitalReference);
    std::optional<float> printViewingKelvin =
        !acceptsViewingIlluminant(paper) ? std::nullopt : printLightKelvin(options.printViewingKelvin);
    float callier = callierCoefficient(options.callier, stock, paper);

    TableOptions effective;
    effective.bleachBypass = bleachBypass;
    effective.printViewingKelvin = printViewingKelvin;
    effective.callier = callier;
    effective.printer = printer;
    effective.printerLights = printerLights;
    effective.digitalReference = options.digitalReference;
    effective.screenGrade = screenGrade;
    effective.screenExposureEV = screenExposureEV;
    effective.labScanLook = labScanLook;
    effective.receiverBands = receiverBands;
    uint64_t key = cacheIdentifier(stock, paper, effective);

    TableCache& tc = tableCache();
    {
        std::unique_lock<std::mutex> lock(tc.lock);
        while (true) {
            if (auto found = tc.cache.value(key)) return *found;
            if (tc.building.count(key)) {
                tc.built.wait(lock);
                continue;
            }
            tc.building.insert(key);
            break;
        }
    }
    try {
        SpectralPipelineTables built =
            buildTables(stock, paper, bleachBypass, printViewingKelvin, callier, printer, printerLights,
                        options.digitalReference, screenGrade, screenExposureEV, labScanLook, receiverBands);
        std::lock_guard<std::mutex> lock(tc.lock);
        tc.cache.insert(built, key);
        tc.building.erase(key);
        tc.built.notify_all();
        return built;
    } catch (...) {
        std::lock_guard<std::mutex> lock(tc.lock);
        tc.building.erase(key);
        tc.built.notify_all();
        throw;
    }
}

float retainedSilverFraction(float requested, const FilmStock& stock) {
    if (stock.isMonochrome || stock.isReversal) return 0;
    return swiftMin(swiftMax(requested, 0.0f), 1.0f);
}

float retainedSilverDensity(const std::vector<float>& density, const std::vector<float>& dMin, float fraction) {
    if (!(fraction > 0)) return 0;
    float silver = 0;
    for (int layer = 0; layer < 3; ++layer) silver += swiftMax(density[layer] - dMin[layer], 0.0f);
    return fraction * retainedSilverPerDye * silver;
}

float callierCoefficient(float requested, const FilmStock& stock, PrintPaper paper) {
    if (!(std::isfinite(requested) && requested > 1 && EnlargerOptics::illuminates(stock, paper))) return 1;
    return requested;
}

uint64_t cacheIdentifier(const FilmStock& stock, PrintPaper requestedPaper, const TableOptions& options) {
    PrintPaper paper = resolved(requestedPaper, stock);
    uint64_t h = stock.spectralProfile.signature();
    auto add = [&h](float v) { h = (h ^ bits(v)) * kPrime; };
    // The exposure LUT is normalized against the illuminant this emulsion was balanced for.
    add(stock.referenceIlluminantKelvin);
    for (const CharacteristicCurve& curve : stock.curves) {
        add(curve.dMin);
        add(curve.gamma);
        add(curve.toe);
        add(curve.toeWidth);
        add(curve.shoulder);
        add(curve.shoulderWidth);
        if (curve.sampled) {
            add(static_cast<float>(curve.sampled->logExposure.size()));
            for (float value : curve.sampled->logExposure) add(value);
            for (float value : curve.sampled->density) add(value);
        }
        if (curve.secondary) {
            add(1);
            add(curve.secondary->gamma);
            add(curve.secondary->toe);
            add(curve.secondary->toeWidth);
            add(curve.secondary->shoulder);
            add(curve.secondary->shoulderWidth);
        } else {
            add(0);
        }
    }
    add(stock.paperCurve.dMin);
    add(stock.paperCurve.gamma);
    add(stock.paperCurve.toe);
    add(stock.paperCurve.toeWidth);
    add(stock.paperCurve.shoulder);
    add(stock.paperCurve.shoulderWidth);
    add(stock.isReversal ? midDensity(PrintPaper::screen) : midDensity(paper));
    h = (h ^ static_cast<uint64_t>(stock.isReversal ? 1 : 0)) * kPrime;
    h = (h ^ static_cast<uint64_t>(stock.isMonochrome ? 1 : 0)) * kPrime;
    h = (h ^ static_cast<uint64_t>(stock.isReflectionPrint ? 1 : 0)) * kPrime;
    if (!viewsFilmDirectly(paper, stock)) {
        for (unsigned char byte : std::string(rawValue(paper))) h = (h ^ static_cast<uint64_t>(byte)) * kPrime;
    }
    // The Digital Reference style changes the screen tables of developed film and nothing else.
    if (paper == PrintPaper::screen && !stock.isReflectionPrint &&
        (!stock.isReversal || usesGradedCurve(options.digitalReference))) {
        for (unsigned char byte : std::string(rawValue(options.digitalReference))) {
            h = (h ^ static_cast<uint64_t>(byte)) * 0x9E3779B97F4A7C15ULL;
        }
    }
    float grade = effectiveScreenGrade(options.screenGrade, stock, paper, options.digitalReference);
    if (grade != DigitalReferenceReceiver::referenceGrade) add(grade);
    float directExposure = directViewExposure(options.screenExposureEV, stock, paper, options.digitalReference);
    if (directExposure != 0) add(directExposure + 1024);
    float look = effectiveLabScanLook(options.labScanLook, paper);
    if (look != 1) add(look + 2048);
    ReceiverBands bands = effectiveReceiverBands(options.receiverBands, stock, paper);
    if (bands != ReceiverBands::paper()) {
        for (int band = 0; band < 3; ++band) add(bands[band] + 4096);
    }
    // Hashed only away from their off positions.
    float bleach = retainedSilverFraction(options.bleachBypass, stock);
    if (bleach > 0) h = (h ^ bits(bleach)) * kPrime;
    if (acceptsViewingIlluminant(paper)) {
        if (std::optional<float> kelvin = printLightKelvin(options.printViewingKelvin)) {
            h = (h ^ bits(*kelvin)) * 0x9E3779B97F4A7C15ULL;
        }
    }
    float callier = callierCoefficient(options.callier, stock, paper);
    if (callier != 1) h = (h ^ bits(callier)) * 0xC2B2AE3D27D4EB4FULL;
    uint64_t donor = donorSignature(stock);
    if (donor != 0) h = (h ^ donor) * kPrime;
    if (std::optional<PrinterProfile> printer = PrinterProfile::resolved(options.printer, stock, paper)) {
        h = (h ^ printer->spectralSignature()) * kPrime;
    }
    if (std::optional<PrinterLights> lights = PrinterLights::resolved(options.printerLights, stock, paper)) {
        h = (h ^ lights->spectralSignature()) * kPrime;
    }
    return h;
}

float neutralDensity(const FilmStock& stock, float logExposure) {
    return (stock.curves[0].density(logExposure) + stock.curves[1].density(logExposure) +
            stock.curves[2].density(logExposure)) /
           3;
}

namespace {

/// Legacy direct read for monochrome Screen output.
constexpr int screenReadingSamples = 1024;

std::vector<std::vector<float>> screenReading(const FilmStock& stock) {
    float mid = neutralDensity(stock, 0);
    std::vector<std::vector<float>> result;
    for (int layer = 0; layer < 3; ++layer) {
        const CharacteristicCurve& curve = stock.curves[layer];
        float range = curve.dMax() - curve.dMin;
        std::vector<float> row(screenReadingSamples + 1);
        for (int i = 0; i <= screenReadingSamples; ++i) {
            float density = curve.dMin + range * static_cast<float>(i) / static_cast<float>(screenReadingSamples);
            row[i] = mid - neutralDensity(stock, curve.logExposure(density));
        }
        result.push_back(std::move(row));
    }
    return result;
}

float interpolate(const std::vector<float>& table, float t) {
    float x = swiftMin(swiftMax(t, 0.0f), 1.0f) * static_cast<float>(table.size() - 1);
    int low = static_cast<int>(x);
    int high = std::min(low + 1, static_cast<int>(table.size()) - 1);
    return table[low] + (x - static_cast<float>(low)) * (table[high] - table[low]);
}

float dot3(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Newton inverse of a three-record spectral response.
class SpectralResponseInverse {
public:
    explicit SpectralResponseInverse(std::function<Float3(Float3)> response) : response(std::move(response)) {}

    /// The response to a light in the exposure table's basis.
    std::function<Float3(Float3)> response;

    static constexpr float floor = 1e-6f;
    static constexpr float neutralWeight = 0.005f;

    Float3 solve(Float3 target, Float3 initial) const {
        const float low = std::log(floor);
        const float weight = neutralWeight * neutralWeight;
        auto bounded = [&](Float3 x) {
            return Float3(clamp(x.x, low, 0.0f), clamp(x.y, low, 0.0f), clamp(x.z, low, 0.0f));
        };
        auto chroma = [](Float3 x) { return x - Float3::repeating((x.x + x.y + x.z) / 3); };
        auto respond = [&](Float3 x) { return sample(Float3(std::exp(x.x), std::exp(x.y), std::exp(x.z))); };
        auto cost = [&](Float3 x, Float3 residual) {
            Float3 c = chroma(x);
            return dot3(residual, residual) + weight * dot3(c, c);
        };
        Float3 x = bounded(Float3(std::log(swiftMax(initial.x, floor)), std::log(swiftMax(initial.y, floor)),
                                  std::log(swiftMax(initial.z, floor))));
        Float3 residual = respond(x) - target;
        float current = cost(x, residual);
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float epsilon = 0.01f;
            Float3 columns[3];
            for (int channel = 0; channel < 3; ++channel) {
                Float3 below = x;
                Float3 above = x;
                below[channel] = swiftMax(x[channel] - epsilon, low);
                above[channel] = swiftMin(x[channel] + epsilon, 0.0f);
                float span = swiftMax(above[channel] - below[channel], 1e-6f);
                columns[channel] = (respond(above) - respond(below)) / span;
            }
            // Normal equations of the data term and the neutral prior, with a vanishing ridge.
            Float3 gradient = Float3(dot3(columns[0], residual), dot3(columns[1], residual), dot3(columns[2], residual)) +
                              weight * chroma(x);
            Float3 normal[3];
            for (int row = 0; row < 3; ++row) {
                normal[row] = Float3(dot3(columns[row], columns[0]), dot3(columns[row], columns[1]),
                                     dot3(columns[row], columns[2]));
            }
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    normal[row][column] += weight * (static_cast<float>(row == column ? 1 : 0) - 1.0f / 3);
                }
                normal[row][row] += 1e-8f;
            }
            Float3 delta;
            if (!normalSolve(normal, gradient, delta)) break;
            float step = 1;
            bool accepted = false;
            for (int attempt = 0; attempt < 10; ++attempt) {
                Float3 candidate = bounded(x - step * delta);
                Float3 candidateResidual = respond(candidate) - target;
                float candidateCost = cost(candidate, candidateResidual);
                if (candidateCost < current) {
                    x = candidate;
                    residual = candidateResidual;
                    current = candidateCost;
                    accepted = true;
                    break;
                }
                step *= 0.5f;
            }
            if (!accepted || step * std::sqrt(dot3(delta, delta)) < 1e-5f) break;
        }
        return Float3(std::exp(x.x), std::exp(x.y), std::exp(x.z));
    }

private:
    /// The solve walks linear Rec.2020 and steps into the table's basis only to read the response.
    Float3 sample(Float3 scene) const { return response(ColorScience::linearRec2020ToExposureDomain(scene)); }

    /// Cholesky in double precision.
    static bool normalSolve(const Float3 matrix[3], Float3 rhs, Float3& out) {
        double a[3][3];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) a[r][c] = static_cast<double>(matrix[r][c]);
        }
        for (int j = 0; j < 3; ++j) {
            double diagonal = a[j][j];
            for (int k = 0; k < j; ++k) diagonal -= a[j][k] * a[j][k];
            if (!(diagonal > 0)) return false;
            a[j][j] = std::sqrt(diagonal);
            for (int i = j + 1; i < 3; ++i) {
                double value = a[i][j];
                for (int k = 0; k < j; ++k) value -= a[i][k] * a[j][k];
                a[i][j] = value / a[j][j];
            }
        }
        double y[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            double value = static_cast<double>(rhs[i]);
            for (int k = 0; k < i; ++k) value -= a[i][k] * y[k];
            y[i] = value / a[i][i];
        }
        double x[3] = {0, 0, 0};
        for (int i = 2; i >= 0; --i) {
            double value = y[i];
            for (int k = i + 1; k < 3; ++k) value -= a[k][i] * x[k];
            x[i] = value / a[i][i];
        }
        out = Float3(static_cast<float>(x[0]), static_cast<float>(x[1]), static_cast<float>(x[2]));
        return true;
    }
};

/// Characterizes a scan's complete response and inverts its receiver records into display colour.
class ScanOutputCalibration {
public:
    static constexpr float shadowOnset = 0.5f;
    static constexpr float shadowFade = 3;

    ScanOutputCalibration(PrintPaper paper, const FilmStock& stock, const SpectralLUT& exposure,
                          const SpectralLUT& printing, float finish)
        : inverse_(nullptr) {
        CharacteristicCurve curve = printCurve(paper, stock);
        float scanRange = curve.dMax() - curve.dMin;
        range_ = scanRange;
        deliversRec709_ = deliversRec709(paper);
        finish_ = paper == PrintPaper::labScan ? finish : 0;
        float midpoint = curve.logExposure(curve.dMin + anchorDensity(paper));
        // FotufilmEngine.Options().printCorrection is 0.
        std::vector<float> masking = stock.printingContrastScale(0, paper);
        std::vector<CharacteristicCurve> filmCurves = stock.curves;
        std::function<Float3(Float3)> response = [exposure, printing, filmCurves, curve, midpoint, masking,
                                                  scanRange](Float3 scene) {
            // On the table's face with the light scaled back out, as the renderer reads it.
            float radiance = swiftMax(scene.x, scene.y, scene.z, 1e-6f);
            Float3 records = exposure.sample(scene / radiance) * (radiance / 0.18f);
            Float3 activation;
            for (int channel = 0; channel < 3; ++channel) {
                const CharacteristicCurve& film = filmCurves[channel];
                activation[channel] = (film.density(std::log10(swiftMax(records[channel], 1e-6f))) - film.dMin) /
                                      (film.dMax() - film.dMin);
            }
            Float3 receiver = printing.sample(activation);
            Float3 out;
            for (int channel = 0; channel < 3; ++channel) {
                out[channel] = curve.density(midpoint + masking[channel] * receiver[channel]) - curve.dMin;
            }
            return out / scanRange;
        };
        inverse_ = SpectralResponseInverse(response);
        Float3 w = ColorScience::displayP3LuminanceWeights;
        if (deliversRec709_) {
            shadowFloor_ = std::pow(10.0f, -scanRange);
        } else {
            Float3 black = response(Float3()) * scanRange;
            shadowFloor_ = w.x * std::pow(10.0f, -black.x) + w.y * std::pow(10.0f, -black.y) +
                           w.z * std::pow(10.0f, -black.z);
        }
        for (float stops : strideThrough(-20.0f, 6.0f, 0.125f)) {
            Float3 read = response(Float3::repeating(0.18f * std::pow(2.0f, stops))) * scanRange;
            Float3 raw(std::pow(10.0f, -read.x), std::pow(10.0f, -read.y), std::pow(10.0f, -read.z));
            float level = w.x * raw.x + w.y * raw.y + w.z * raw.z;
            greyAxis_.push_back({std::log(swiftMax(level, 1e-12f)), raw / swiftMax(level, 1e-12f)});
        }
        std::stable_sort(greyAxis_.begin(), greyAxis_.end(),
                         [](const GreyPoint& a, const GreyPoint& b) { return a.logLuminance < b.logLuminance; });
    }

    Float3 greyShare(float luminance) const {
        float x = std::log(swiftMax(luminance, 1e-12f));
        if (greyAxis_.empty()) return Float3::one();
        const GreyPoint& first = greyAxis_.front();
        const GreyPoint& last = greyAxis_.back();
        if (x <= first.logLuminance) return first.share;
        if (x >= last.logLuminance) return last.share;
        int low = 0, high = static_cast<int>(greyAxis_.size()) - 1;
        while (high - low > 1) {
            int middle = (low + high) / 2;
            if (greyAxis_[middle].logLuminance <= x) low = middle; else high = middle;
        }
        const GreyPoint& a = greyAxis_[low];
        const GreyPoint& b = greyAxis_[high];
        float t = (x - a.logLuminance) / swiftMax(b.logLuminance - a.logLuminance, 1e-9f);
        return a.share + t * (b.share - a.share);
    }

    Float3 operator()(Float3 density) const {
        Float3 activation = density / range_;
        Float3 uncalibrated(std::pow(10.0f, -density.x), std::pow(10.0f, -density.y), std::pow(10.0f, -density.z));
        Float3 initial = ColorScience::linearDisplayP3ToRec2020(uncalibrated);
        initial = Float3(clamp(initial.x, 0.0f, 1.0f), clamp(initial.y, 0.0f, 1.0f), clamp(initial.z, 0.0f, 1.0f));
        Float3 scene = inverse_.solve(activation, initial);
        Float3 calibrated = ColorScience::linearRec2020ToDisplayP3(scene);
        Float3 weights = ColorScience::displayP3LuminanceWeights;
        auto luminance = [&](Float3 value) { return weights.x * value.x + weights.y * value.y + weights.z * value.z; };

        // Reapply the medium's own balance at the recovered tone level.
        float balanceFloor = deliversRec709_ ? 0.0f : 0.045f;
        float sceneLuminance = clamp(luminance(calibrated), balanceFloor, 1.0f);
        Float3 neutralDensity = inverse_.response(Float3::repeating(sceneLuminance)) * range_;
        Float3 neutralReceiver(std::pow(10.0f, -neutralDensity.x), std::pow(10.0f, -neutralDensity.y),
                               std::pow(10.0f, -neutralDensity.z));
        calibrated *= neutralReceiver / swiftMax(luminance(neutralReceiver), 1e-6f);
        float recoveredLuminance = luminance(calibrated);
        if (!(recoveredLuminance > 1e-6f)) {
            // A zero-light inverse has no usable colour estimate; keep the scan's measured luminance.
            return deliversRec709_ ? uncalibrated : Float3::repeating(luminance(uncalibrated));
        }

        if (deliversRec709_) {
            calibrated = heldToGamut(calibrated, recoveredLuminance);
            // Hold the timed colour at the Rec.709 boundary before delivering it on P3.
            Float3 rec709 = ColorScience::linearDisplayP3ToSRGB(calibrated);
            Float3 held = heldToGamut(rec709, recoveredLuminance);
            calibrated = ColorScience::linearSRGBToDisplayP3(held);
        }
        float outputLuminance = luminance(uncalibrated);
        calibrated *= outputLuminance / recoveredLuminance;
        if (!deliversRec709_) {
            // From half a stop above the no-light floor over three stops, the characterized colour
            // gives way to the colour the scan itself reads.
            float above = std::log2(swiftMax(outputLuminance, 1e-12f) / swiftMax(shadowFloor_, 1e-12f));
            float shadow = clamp(1 - (above - shadowOnset) / shadowFade, 0.0f, 1.0f);
            float hold = shadow * shadow * (3 - 2 * shadow);
            Float3 neutral = Float3::repeating(outputLuminance);
            Float3 read = uncalibrated / greyShare(outputLuminance);
            read *= outputLuminance / swiftMax(luminance(read), 1e-12f);
            Float3 held = neutral + clamp(above / 0.25f, 0.0f, 1.0f) * (read - neutral);
            Float3 scanned = calibrated + hold * (held - calibrated);
            float scannedLuminance = outputLuminance;
            if (finish_ > 0) {
                scanned = LabScanFinish::apply(scanned, finish_, true, shadowFloor_);
                scannedLuminance = luminance(scanned);
            }
            // Keep highlight color; only compress chroma the delivery gamut cannot hold.
            return compressedToDisplayGamut(scanned, scannedLuminance);
        }

        // The characterization fades to the neutral axis at both ends of the receiver.
        float blackFloor = std::pow(10.0f, -range_);
        float shadow = clamp((blackFloor * 2 - outputLuminance) / (blackFloor * 2 - blackFloor * 1.5f), 0.0f, 1.0f);
        float highlight = clamp((outputLuminance - clipLow) / (clipHigh - clipLow), 0.0f, 1.0f);
        float endpoint = swiftMax(shadow, highlight);
        float hold = endpoint * endpoint * (3 - 2 * endpoint);
        return calibrated + hold * (Float3::repeating(outputLuminance) - calibrated);
    }

private:
    struct GreyPoint {
        float logLuminance;
        Float3 share;
    };

    SpectralResponseInverse inverse_;
    float range_ = 0;
    bool deliversRec709_ = false;
    float finish_ = 0;
    float shadowFloor_ = 0;
    std::vector<GreyPoint> greyAxis_;
    /// The finished video transfer gives up highlight chroma near its white point.
    static constexpr float clipLow = 0.76f;
    static constexpr float clipHigh = 0.84f;
};

}  // namespace

Float3 heldToGamut(Float3 colour, float luminance) {
    float scale = 1;
    for (int channel = 0; channel < 3; ++channel) {
        if (colour[channel] < 0) scale = swiftMin(scale, luminance / (luminance - colour[channel]));
    }
    if (!(scale < 1)) return colour;
    return Float3::repeating(luminance) + scale * (colour - Float3::repeating(luminance));
}

Float3 compressedToDisplayGamut(Float3 colour, float luminance) {
    if (!(luminance > 0 && luminance < 1)) return Float3::repeating(clamp(luminance, 0.0f, 1.0f));
    Float3 neutral = Float3::repeating(luminance);
    Float3 chroma = colour - neutral;
    float radius = 0;
    for (int channel = 0; channel < 3; ++channel) {
        float distance = chroma[channel] >= 0 ? chroma[channel] / (1 - luminance) : -chroma[channel] / luminance;
        radius = swiftMax(radius, distance);
    }
    const float knee = 0.9f;
    if (!(radius > knee)) return colour;
    float room = 1 - knee;
    float over = radius - knee;
    float compressed = knee + room * over / (over + room);
    return neutral + chroma * (compressed / radius);
}

// ---- Negative and direct views ----

namespace {
/// The curve endpoints the table is built over.
uint64_t densitySignature(const FilmStock& stock) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const CharacteristicCurve& curve : stock.curves) {
        for (float value : {curve.dMin, curve.dMax()}) h = (h ^ bits(value)) * kPrime;
    }
    return h;
}
}  // namespace

SpectralLUT negativeViewing(const FilmStock& stock, NegativeViewing look, float bleachBypass) {
    float bleach = retainedSilverFraction(bleachBypass, stock);
    uint64_t key = stock.spectralProfile.signature() ^ (ordinal(look) * 0x9E3779B97F4A7C15ULL) ^ densitySignature(stock);
    if (bleach > 0) key = (key ^ bits(bleach)) * kPrime;
    std::lock_guard<std::mutex> guard(negativeLock());
    if (auto found = negativeCache().value(key)) return *found;

    std::vector<float> dMin = {stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin};
    std::vector<float> ranges = {stock.curves[0].dMax() - stock.curves[0].dMin,
                                 stock.curves[1].dMax() - stock.curves[1].dMin,
                                 stock.curves[2].dMax() - stock.curves[2].dMin};
    // The base carries no developed silver; only the image inverts.
    Float3 base = transmissionRGB(dMin, stock);
    Float3 divisor;
    switch (look) {
        case NegativeViewing::lightBox: {
            float brightest = swiftMax(base.x, base.y, base.z, 1e-6f);
            divisor = Float3::repeating(brightest / lightBoxBaseLevel);
            break;
        }
        case NegativeViewing::scanner:
            divisor = Float3(swiftMax(base.x, 1e-6f), swiftMax(base.y, 1e-6f), swiftMax(base.z, 1e-6f));
            break;
    }
    // Indexed by the negative's own activation: nothing was inverted upstream.
    SpectralLUT table = buildLUT([&](Float3 p) {
        std::vector<float> density = {dMin[0] + p.x * ranges[0], dMin[1] + p.y * ranges[1], dMin[2] + p.z * ranges[2]};
        Float3 rgb = transmissionRGB(density, stock, retainedSilverDensity(density, dMin, bleach)) / divisor;
        return Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f));
    });
    negativeCache().insert(table, key);
    return table;
}

Float3 reversalBalance(const FilmStock& stock, const DensityMap& aligned) {
    std::vector<float> mid = aligned({stock.developedDensity(0, 0), stock.developedDensity(1, 0),
                                      stock.developedDensity(2, 0)});
    Float3 rawMid = transmissionRGB(mid, stock);
    if (!stock.isReflectionPrint) {
        // Balance the reference sample as the renderer reads it: the containing cell only.
        float ranges[3];
        for (int c = 0; c < 3; ++c) ranges[c] = stock.curves[c].dMax() - stock.curves[c].dMin;
        Float3 q;
        for (int c = 0; c < 3; ++c) {
            float density = stock.developedDensity(c, 0);
            q[c] = clamp((density - stock.curves[c].dMin) / swiftMax(ranges[c], 1e-6f), 0.0f, 1.0f) *
                   static_cast<float>(lutDimension - 1);
        }
        int origin[3];
        for (int c = 0; c < 3; ++c) origin[c] = std::min(static_cast<int>(q[c]), lutDimension - 2);
        std::vector<float> values;
        for (int z = 0; z < 2; ++z) {
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    int offsets[3] = {x, y, z};
                    std::vector<float> density(3);
                    for (int c = 0; c < 3; ++c) {
                        density[c] = stock.curves[c].dMin + static_cast<float>(origin[c] + offsets[c]) /
                                                                static_cast<float>(lutDimension - 1) * ranges[c];
                    }
                    Float3 rgb = transmissionRGB(aligned(density), stock);
                    values.insert(values.end(), {swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f), 1.0f});
                }
            }
        }
        rawMid = SpectralLUT(2, values).sample(
            q - Float3(static_cast<float>(origin[0]), static_cast<float>(origin[1]), static_cast<float>(origin[2])));
    }
    Float3 balance(0.18f / swiftMax(rawMid.x, 1e-6f), 0.18f / swiftMax(rawMid.y, 1e-6f), 0.18f / swiftMax(rawMid.z, 1e-6f));
    if (!stock.isReflectionPrint) return balance;
    Float3 white = transmissionRGB(aligned({stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin}), stock) *
                   balance;
    // The transmission is integrated to Display P3, so its luminance reads with the P3 weights.
    Float3 weights = ColorScience::displayP3LuminanceWeights;
    float level = weights.x * white.x + weights.y * white.y + weights.z * white.z;
    return balance / swiftMax(level, 1e-6f);
}

std::vector<float> NeutralDensityBasis::operator()(const std::vector<float>& density) const {
    if (statusA) {
        Float3 a = statusA->amounts(Float3(density[0], density[1], density[2]));
        return {a.x, a.y, a.z};
    }
    std::vector<float> out(3);
    for (int layer = 0; layer < 3; ++layer) {
        Float4 c = coefficients[layer];
        float d = density[layer];
        out[layer] = swiftMax(c.x + c.y * d + c.z * d * d + c.w * d * d * d, 0.0f);
    }
    return out;
}

namespace {
/// Gaussian elimination with partial pivoting on the normal equations; rows keep a four-value stride.
std::optional<Float4> solveNormal(std::vector<double> a, std::vector<double> b, int termCount) {
    for (int column = 0; column < termCount; ++column) {
        int pivot = column;
        for (int row = column + 1; row < termCount; ++row) {
            if (std::fabs(a[row * 4 + column]) > std::fabs(a[pivot * 4 + column])) pivot = row;
        }
        if (!(std::fabs(a[pivot * 4 + column]) > 1e-12)) return std::nullopt;
        if (pivot != column) {
            for (int k = 0; k < termCount; ++k) std::swap(a[column * 4 + k], a[pivot * 4 + k]);
            std::swap(b[column], b[pivot]);
        }
        for (int row = column + 1; row < termCount; ++row) {
            double factor = a[row * 4 + column] / a[column * 4 + column];
            if (factor == 0) continue;
            for (int k = column; k < termCount; ++k) a[row * 4 + k] -= factor * a[column * 4 + k];
            b[row] -= factor * b[column];
        }
    }
    double x[4] = {0, 0, 0, 0};
    for (int row = termCount - 1; row >= 0; --row) {
        double sum = b[row];
        for (int k = row + 1; k < termCount; ++k) sum -= a[row * 4 + k] * x[k];
        x[row] = sum / a[row * 4 + row];
    }
    return Float4(static_cast<float>(x[0]), static_cast<float>(x[1]), static_cast<float>(x[2]),
                  static_cast<float>(x[3]));
}
}  // namespace

NeutralDensityBasis neutralDensityBasis(const FilmStock& stock) {
    // A separated-base profile specifies the density basis its dye amplitudes use.
    if (stock.spectralProfile.minimumDensity) {
        return NeutralDensityBasis{std::vector<Float4>(3, Float4(0, 1, 0, 0)), std::nullopt};
    }
    if (stock.isReversal && !stock.isReflectionPrint) {
        return NeutralDensityBasis{{}, StatusADyeUnmix(stock.spectralProfile.imageDyeDensity)};
    }
    int termCount = stock.isReflectionPrint ? 4 : 3;
    std::vector<std::vector<double>> normal(3, std::vector<double>(16, 0.0));
    std::vector<std::vector<double>> moment(3, std::vector<double>(4, 0.0));
    for (float logExposure : strideThrough(-1.5f, 0.75f, 0.025f)) {
        float density[3] = {stock.developedDensity(0, logExposure), stock.developedDensity(1, logExposure),
                            stock.developedDensity(2, logExposure)};
        double neutral = static_cast<double>(density[0] + density[1] + density[2]) / 3;
        double weight = std::pow(10.0, -neutral);
        for (int layer = 0; layer < 3; ++layer) {
            double d = static_cast<double>(density[layer]);
            double terms[4] = {1, d, d * d, d * d * d};
            for (int row = 0; row < termCount; ++row) {
                for (int column = 0; column < termCount; ++column) {
                    normal[layer][row * 4 + column] += weight * terms[row] * terms[column];
                }
                moment[layer][row] += weight * terms[row] * neutral;
            }
        }
    }
    std::vector<Float4> coefficients;
    for (int layer = 0; layer < 3; ++layer) {
        coefficients.push_back(solveNormal(normal[layer], moment[layer], termCount).value_or(Float4(0, 1, 0, 0)));
    }
    return NeutralDensityBasis{coefficients, std::nullopt};
}

// ---- Wavelength integrals ----

Float3 transmissionRGB(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                       float neutralDensity, const std::vector<float>* illuminant,
                       const std::vector<float>* densityOffset) {
    std::vector<float> spectrum(SpectralGrid::count, 0.0f);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        float d = neutralDensity;
        for (size_t dye = 0; dye < dyes.size(); ++dye) d += density[dye] * dyes[dye][i];
        if (densityOffset) d += (*densityOffset)[i];
        spectrum[i] = std::pow(10.0f, -d);
    }
    if (!illuminant) return SpectralGrid::toLinearDisplayP3(spectrum);
    return SpectralGrid::toLinearDisplayP3(spectrum, *illuminant);
}

Float3 paperExposure(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                     const std::vector<float>& lamp, const std::vector<std::vector<float>>& paperSensitivity,
                     float neutralDensity, const std::vector<float>* densityOffset) {
    Float3 e = Float3::repeating(0);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        float d = density[0] * dyes[0][i] + density[1] * dyes[1][i] + density[2] * dyes[2][i] + neutralDensity;
        if (densityOffset) d += (*densityOffset)[i];
        float transmitted = lamp[i] * std::pow(10.0f, -d);
        e.x += transmitted * paperSensitivity[0][i];
        e.y += transmitted * paperSensitivity[1][i];
        e.z += transmitted * paperSensitivity[2][i];
    }
    return e;
}

// ---- Effective controls and Digital Reference readers ----

float effectiveScreenGrade(float grade, const FilmStock& stock, PrintPaper paper,
                           DigitalReferenceStyle digitalReference) {
    if (!(paper == PrintPaper::screen && !stock.isReversal && usesGradedCurve(digitalReference) &&
          std::isfinite(grade))) {
        return DigitalReferenceReceiver::referenceGrade;
    }
    return swiftMin(swiftMax(grade, DigitalReferenceReceiver::gradeRangeLower), DigitalReferenceReceiver::gradeRangeUpper);
}

float directViewExposure(float stops, const FilmStock& stock, PrintPaper paper,
                         DigitalReferenceStyle digitalReference) {
    if (!(paper == PrintPaper::screen && viewsFilmDirectly(paper, stock) && !stock.isReflectionPrint &&
          !levelsPositive(paper, stock, digitalReference) && std::isfinite(stops))) {
        return 0;
    }
    return swiftMin(swiftMax(stops, -6.0f), 6.0f);
}

float effectiveLabScanLook(float look, PrintPaper paper) {
    if (paper != PrintPaper::labScan) return 1;
    return std::isfinite(look) ? swiftMin(swiftMax(look, 0.0f), 1.0f) : 1.0f;
}

ReceiverBands effectiveReceiverBands(const ReceiverBands& bands, const FilmStock& stock, PrintPaper paper) {
    if (!(paper == PrintPaper::screen && !stock.isMonochrome && !stock.isReflectionPrint)) return ReceiverBands::paper();
    return bands.clamped();
}

float screenExposureShift(float stops, const FilmStock& stock, PrintPaper paper,
                          DigitalReferenceStyle digitalReference, float screenGrade) {
    if (!(paper == PrintPaper::screen && !stock.isReflectionPrint && std::isfinite(stops) &&
          (!viewsFilmDirectly(paper, stock) || levelsPositive(paper, stock, digitalReference)))) {
        return 0;
    }
    float grade = effectiveScreenGrade(screenGrade, stock, paper, digitalReference);
    return DigitalReferenceReceiver::exposureShift(swiftMin(swiftMax(stops, -6.0f), 6.0f), stock, digitalReference,
                                                   grade);
}

ScreenToeBalance screenToeBalance(const FilmStock& stock, DigitalReferenceStyle digitalReference,
                                  const DensityRead& read) {
    if (!(!stock.isReversal && !stock.isMonochrome && digitalReference == DigitalReferenceStyle::autoLevels)) {
        return ScreenToeBalance{std::nullopt};
    }
    std::vector<Float3> wedge;
    wedge.reserve(257);
    for (int i = 0; i <= 256; ++i) {
        float logExposure = -8 * static_cast<float>(i) / 256;
        wedge.push_back(read({stock.developedDensity(0, logExposure), stock.developedDensity(1, logExposure),
                              stock.developedDensity(2, logExposure)}));
    }
    std::vector<std::vector<Float2>> wedges;
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<Float2> samples;
        for (const Float3& r : wedge) {
            if (samples.empty() || r[channel] > samples.back().x + 1e-5f) samples.push_back(Float2(r[channel], r.y - r[channel]));
        }
        wedges.push_back(std::move(samples));
    }
    return ScreenToeBalance{wedges};
}

DensityRead screenReader(const FilmStock& stock, const ReceiverBands& bands, const DensityRead& relativeEnergy,
                         const std::function<DensityRead()>& paperRelativeEnergy) {
    DigitalReferenceReceiver::DensityUnmix unmix = DigitalReferenceReceiver::unmix(bands);
    DensityRead read = [unmix, relativeEnergy](const std::vector<float>& density) {
        return unmix.read(relativeEnergy(density));
    };
    if (bands == ReceiverBands::paper()) return read;
    const DigitalReferenceReceiver::DensityUnmix& paperUnmix = DigitalReferenceReceiver::unmixPaper();
    DensityRead paperEnergy = paperRelativeEnergy();
    auto neutral = std::make_shared<ReceiverNeutralCalibration>(
        stock, read, [&paperUnmix, paperEnergy](const std::vector<float>& d) { return paperUnmix.read(paperEnergy(d)); });
    return [neutral, read](const std::vector<float>& density) { return (*neutral)(read(density)); };
}

DensityMap positiveBandShift(const FilmStock& stock, const ReceiverBands& bands) {
    if (bands == ReceiverBands::paper()) return {};
    auto basis = std::make_shared<NeutralDensityBasis>(neutralDensityBasis(stock));
    auto dyes = std::make_shared<std::vector<std::vector<float>>>(stock.spectralProfile.imageDyeDensity);
    auto offset = std::make_shared<std::optional<std::vector<float>>>(filmDensityOffset(stock));
    auto reader = [basis, dyes, offset](std::vector<std::vector<float>> sensitivity) -> DensityRead {
        float totals[3];
        for (int b = 0; b < 3; ++b) {
            float sum = 0;
            for (float v : sensitivity[b]) sum += v;
            totals[b] = swiftMax(sum, 1e-12f);
        }
        auto shared = std::make_shared<std::vector<std::vector<float>>>(std::move(sensitivity));
        float t0 = totals[0], t1 = totals[1], t2 = totals[2];
        return [basis, dyes, offset, shared, t0, t1, t2](const std::vector<float>& density) {
            std::vector<float> aligned = (*basis)(density);
            const auto& sens = *shared;
            Float3 read = Float3::repeating(0);
            for (int i = 0; i < SpectralGrid::count; ++i) {
                float d = *offset ? (**offset)[i] : 0.0f;
                for (size_t dye = 0; dye < dyes->size(); ++dye) d += aligned[dye] * (*dyes)[dye][i];
                float transmitted = std::pow(10.0f, -d);
                for (int band = 0; band < 3; ++band) read[band] += transmitted * sens[band][i];
            }
            float totalsArray[3] = {t0, t1, t2};
            Float3 out;
            for (int c = 0; c < 3; ++c) out[c] = -std::log10(swiftMax(read[c] / totalsArray[c], 1e-12f));
            return out;
        };
    };
    DensityRead moved = reader(bands.sensitivity());
    DensityRead paperRead = reader(ReceiverBands::paper().sensitivity());
    auto neutral = std::make_shared<ReceiverNeutralCalibration>(stock, moved, paperRead);
    // Each record's density and the paper bands' read of it, both ways along the neutral wedge.
    DensityRead records = [](const std::vector<float>& density) { return Float3(density[0], density[1], density[2]); };
    auto wedgeRead = std::make_shared<ReceiverNeutralCalibration>(stock, records, paperRead);
    auto wedgeDensity = std::make_shared<ReceiverNeutralCalibration>(stock, paperRead, records);
    float lower[3], upper[3];
    for (int c = 0; c < 3; ++c) {
        lower[c] = stock.curves[c].dMin;
        upper[c] = stock.curves[c].dMax();
    }
    float l0 = lower[0], l1 = lower[1], l2 = lower[2], u0 = upper[0], u1 = upper[1], u2 = upper[2];
    return [neutral, moved, paperRead, wedgeRead, wedgeDensity, l0, l1, l2, u0, u1, u2](const std::vector<float>& density) {
        Float3 difference = (*neutral)(moved(density)) - paperRead(density);
        Float3 shifted = (*wedgeDensity)((*wedgeRead)(Float3(density[0], density[1], density[2])) + difference);
        float lo[3] = {l0, l1, l2}, hi[3] = {u0, u1, u2};
        std::vector<float> out(3);
        for (int c = 0; c < 3; ++c) out[c] = swiftMin(swiftMax(shifted[c], lo[c]), hi[c]);
        return out;
    };
}

float screenShadowScale(const FilmStock& stock, PrintPaper paper, DigitalReferenceStyle digitalReference) {
    if (!(paper == PrintPaper::screen && !stock.isReversal &&
          digitalReference == DigitalReferenceStyle::referenceExposure)) {
        return 1;
    }
    return DigitalReferenceReceiver::referenceBaseTarget(stock) / DigitalReferenceReceiver::baseRead(stock);
}

Float3 PositiveScreenRead::transmittance(const std::vector<float>& density) const {
    Float3 rgb = transmissionRGB(basis(density), stock) * balance;
    return Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f));
}

float PositiveScreenRead::luminance(float stops) const {
    float logExposure = stops * std::log10(2.0f);
    std::vector<float> density = {stock.developedDensity(0, logExposure), stock.developedDensity(1, logExposure),
                                  stock.developedDensity(2, logExposure)};
    Float3 rgb = transmittance(density);
    Float3 weights = ColorScience::displayP3LuminanceWeights;
    float y = weights.x * rgb.x + weights.y * rgb.y + weights.z * rgb.z;
    return -std::log10(swiftMax(y, 1e-6f));
}

namespace {
BoundedCache<uint64_t, std::shared_ptr<const PositiveScreenRead>>& positiveReadCache() {
    static BoundedCache<uint64_t, std::shared_ptr<const PositiveScreenRead>> cache(64);
    return cache;
}
}  // namespace

std::shared_ptr<const PositiveScreenRead> positiveScreenRead(const FilmStock& stock) {
    uint64_t key = cacheIdentifier(stock, PrintPaper::screen);
    {
        std::lock_guard<std::mutex> guard(balanceLock());
        if (auto found = positiveReadCache().value(key)) return *found;
    }
    NeutralDensityBasis basis = neutralDensityBasis(stock);
    Float3 balance = reversalBalance(stock, [&basis](const std::vector<float>& d) { return basis(d); });
    Float3 white =
        transmissionRGB(basis({stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin}), stock) * balance;
    auto read = std::make_shared<const PositiveScreenRead>(
        PositiveScreenRead{stock, basis, balance, -std::log10(swiftMax(white.x, white.y, white.z, 1e-6f))});
    {
        std::lock_guard<std::mutex> guard(balanceLock());
        positiveReadCache().insert(read, key);
    }
    return read;
}

Float3 referenceCastOffset(Float3 midEnergy, const FilmStock& stock, PrintPaper paper) {
    return referenceCastOffset(midEnergy, stock, paper, PrintPaperTables::labScanReferenceMidRatio,
                               PrintPaperTables::labScanCastCeiling);
}

Float3 referenceCastOffset(Float3 midEnergy, const FilmStock& stock, PrintPaper paper, Float2 reference,
                           float ceiling) {
    if (!(isReferenceAnchored(paper) && !stock.isMonochrome)) return Float3();
    float red = std::log10(swiftMax(midEnergy.x, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)) - reference.x;
    float blue = std::log10(swiftMax(midEnergy.z, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)) - reference.y;
    // The profile's correction authority: small casts pass as film character, larger ones are
    // pulled back to the ceiling, direction kept.
    float magnitude = std::sqrt(red * red + blue * blue);
    if (magnitude > ceiling) {
        float held = ceiling / magnitude;
        red *= held;
        blue *= held;
    }
    return Float3(red, 0, blue);
}

// ---- buildTables ----

namespace {

SpectralPipelineTables buildTables(const FilmStock& stock, PrintPaper paper, float bleachBypass,
                                   std::optional<float> printViewingKelvin, float callier,
                                   const std::optional<PrinterProfile>& printer,
                                   const std::optional<PrinterLights>& printerLights,
                                   DigitalReferenceStyle digitalReference, float screenGrade, float screenExposureEV,
                                   float labScanLook, const ReceiverBands& receiverBands) {
    // Output characterization is fixed at the stock's reference light.
    std::vector<float> referenceLight = filmReferenceIlluminant(stock);
    SpectralLUT exposure = exposureTable(stock, referenceLight);
    if (levelsPositive(paper, stock, digitalReference)) {
        // The slide's own transmittance handed to the paper stage as density above its 18% mid-grey.
        std::shared_ptr<const PositiveScreenRead> read = positiveScreenRead(stock);
        DensityMap shift = positiveBandShift(stock, receiverBands);
        SpectralLUT printing = buildDensityLUT(stock, [&](const std::vector<float>& density) {
            Float3 rgb = read->transmittance(shift ? shift(density) : density);
            return Float3(-std::log10(swiftMax(rgb.x, 1e-6f) / 0.18f), -std::log10(swiftMax(rgb.y, 1e-6f) / 0.18f),
                          -std::log10(swiftMax(rgb.z, 1e-6f) / 0.18f));
        });
        std::vector<float> paperRanges;
        for (const CharacteristicCurve& c : printCurves(paper, stock, digitalReference)) paperRanges.push_back(c.dMax() - c.dMin);
        SpectralLUT paperOutput = buildLUT([&](Float3 activation) {
            return DigitalReferenceReceiver::positiveRGB(Float3(
                activation.x * paperRanges[0], activation.y * paperRanges[1], activation.z * paperRanges[2]));
        });
        return SpectralPipelineTables{exposure, printing, paperOutput};
    }
    if (viewsFilmDirectly(paper, stock)) {
        NeutralDensityBasis basis = neutralDensityBasis(stock);
        DensityMap aligned = [&basis](const std::vector<float>& density) { return basis(density); };
        // A slide's scanner gain is folded into its own table: a stop is a doubling of the light.
        Float3 balance = reversalBalance(stock, aligned) *
                         std::exp2(directViewExposure(screenExposureEV, stock, paper, digitalReference));
        DensityMap shift = positiveBandShift(stock, receiverBands);
        SpectralLUT output = buildDensityLUT(stock, [&](const std::vector<float>& density) {
            Float3 rgb = transmissionRGB(aligned(shift ? shift(density) : density), stock) * balance;
            return Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f));
        });
        return SpectralPipelineTables{exposure, output, std::nullopt};
    }
    if (isNegative(paper)) {
        return SpectralPipelineTables{exposure, negativeViewing(stock, NegativeViewing::lightBox, bleachBypass),
                                      std::nullopt};
    }

    std::optional<SpectralLUT> printing;
    if (readsLayersDirectly(paper, stock)) {
        std::vector<std::vector<float>> reading = screenReading(stock);
        // The same film-base black a colour negative gets at a reference exposure.
        float shadowScale = screenShadowScale(stock, paper, digitalReference);
        printing = buildLUT([&](Float3 p) {
            Float3 read(interpolate(reading[0], p.x), interpolate(reading[1], p.y), interpolate(reading[2], p.z));
            return Float3(DigitalReferenceReceiver::stretchShadows(read.x, shadowScale),
                          DigitalReferenceReceiver::stretchShadows(read.y, shadowScale),
                          DigitalReferenceReceiver::stretchShadows(read.z, shadowScale));
        });
    } else {
        std::vector<float> dMin = {stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin};
        std::shared_ptr<const NeutralDensityBasis> basis;
        if (stock.isReversal) basis = std::make_shared<const NeutralDensityBasis>(neutralDensityBasis(stock));
        DensityMap aligned = [basis](const std::vector<float>& density) {
            return basis ? (*basis)(density) : density;
        };
        std::vector<float> midDensity = aligned({stock.developedDensity(0, 0), stock.developedDensity(1, 0),
                                                 stock.developedDensity(2, 0)});
        // Under a condenser head every density is the diffuse figure times the Callier coefficient
        // of the material holding it.
        float silverCallier = callier == 1 ? 1.0f : EnlargerOptics::silverCallierCoefficient;
        std::vector<float> scaledMid(3);
        for (int c = 0; c < 3; ++c) scaledMid[c] = midDensity[c] * callier;
        // The relative log energy each record receives through `bands`, against mid-grey.
        auto energyReader = [&](const ReceiverBands& bands) -> DensityRead {
            auto paperSensitivity = std::make_shared<const std::vector<std::vector<float>>>(sensitivity(paper, bands));
            PrintingIllumination illumination = printingIllumination(
                stock, paper, scaledMid, stock.spectralProfile.imageDyeDensity,
                silverCallier * retainedSilverDensity(midDensity, dMin, bleachBypass), callier, bands, printerLights);
            auto lamp = std::make_shared<const std::vector<float>>(printer ? printer->filteredSpectrum()
                                                                           : illumination.lamp);
            // Keep the simulated printer's reference setup fixed when its lamp or filters move.
            Float3 midEnergy = !printer ? illumination.referenceEnergy
                                        : paperExposure(scaledMid, stock,
                                                        PrinterProfile::simulatedTungsten().filteredSpectrum(),
                                                        *paperSensitivity,
                                                        silverCallier * retainedSilverDensity(midDensity, dMin, bleachBypass),
                                                        callier);
            // A reference-anchored medium keeps the cast this stock leaves against the reference.
            Float3 castOffset = referenceCastOffset(midEnergy, stock, paper);
            auto offset = std::make_shared<const std::optional<std::vector<float>>>(filmDensityOffset(stock, callier));
            auto dyes = std::make_shared<const std::vector<std::vector<float>>>(stock.spectralProfile.imageDyeDensity);
            return [aligned, callier, lamp, paperSensitivity, silverCallier, dMin, bleachBypass, midEnergy, castOffset,
                    offset, dyes](const std::vector<float>& density) {
                std::vector<float> scaled = aligned(density);
                for (float& d : scaled) d = d * callier;
                Float3 energy = paperExposure(scaled, *dyes, *lamp, *paperSensitivity,
                                              silverCallier * retainedSilverDensity(density, dMin, bleachBypass),
                                              *offset ? &**offset : nullptr);
                return Float3(std::log10(swiftMax(energy.x, 1e-12f) / swiftMax(midEnergy.x, 1e-12f)),
                              std::log10(swiftMax(energy.y, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)),
                              std::log10(swiftMax(energy.z, 1e-12f) / swiftMax(midEnergy.z, 1e-12f))) +
                       castOffset;
            };
        };
        DensityRead relativeEnergy = energyReader(receiverBands);
        float shadowScale = screenShadowScale(stock, paper, digitalReference);
        DensityRead screenRead =
            paper == PrintPaper::screen
                ? screenReader(stock, receiverBands, relativeEnergy,
                               [&]() { return energyReader(ReceiverBands::paper()); })
                : relativeEnergy;
        ScreenToeBalance toeBalance = screenToeBalance(
            stock, paper == PrintPaper::screen ? digitalReference : DigitalReferenceStyle::gradedPrint, screenRead);
        printing = buildDensityLUT(stock, [&](const std::vector<float>& density) {
            if (paper != PrintPaper::screen) return relativeEnergy(density);
            Float3 read = toeBalance(screenRead(density));
            return Float3(DigitalReferenceReceiver::stretchShadows(read.x, shadowScale),
                          DigitalReferenceReceiver::stretchShadows(read.y, shadowScale),
                          DigitalReferenceReceiver::stretchShadows(read.z, shadowScale));
        });
    }

    // One range per record: each channel's activation is read back through its curve's range.
    std::vector<float> paperRanges;
    for (const CharacteristicCurve& c : printCurves(paper, stock, digitalReference)) paperRanges.push_back(c.dMax() - c.dMin);
    // The lamp the finished positive is read under: a stated temperature wins on physical media.
    std::optional<std::vector<float>> viewingLight =
        printViewingKelvin ? std::optional<std::vector<float>>(printLightSPD(*printViewingKelvin))
                           : referenceViewingLight(paper);
    std::optional<SpectralLUT> paperOutput;
    if (paper == PrintPaper::screen) {
        float grade = effectiveScreenGrade(screenGrade, stock, paper, digitalReference);
        paperOutput = buildLUT([&](Float3 activation) {
            return DigitalReferenceReceiver::rgb(
                Float3(activation.x * paperRanges[0], activation.y * paperRanges[1], activation.z * paperRanges[2]),
                digitalReference, stock, grade);
        });
    } else if (isScan(paper)) {
        // The scan's output is a digital inversion with no viewing dyes or lamp.
        std::optional<ScanOutputCalibration> calibration;
        if (!stock.isMonochrome) calibration.emplace(paper, stock, exposure, *printing, labScanLook);
        paperOutput = buildLUT([&](Float3 activation) {
            Float3 density(activation.x * paperRanges[0], activation.y * paperRanges[1], activation.z * paperRanges[2]);
            if (calibration) return (*calibration)(density);
            Float3 rgb(std::pow(10.0f, -density.x), std::pow(10.0f, -density.y), std::pow(10.0f, -density.z));
            // A monochrome scan carries no chroma to characterize.
            if (paper == PrintPaper::labScan) return LabScanFinish::apply(rgb, labScanLook, false);
            return deliversRec709(paper) ? ColorScience::linearSRGBToDisplayP3(rgb) : rgb;
        });
    } else if (!stock.isMonochrome) {
        PrintReceiver receiver = printReceiver(stock, paper, viewingLight);
        paperOutput = buildLUT([&](Float3 activation) {
            return receiver.rgb(
                Float3(activation.x * paperRanges[0], activation.y * paperRanges[1], activation.z * paperRanges[2]));
        });
    } else {
        const std::vector<std::vector<float>>& paperDyes = dyes(paper);
        paperOutput = buildLUT([&](Float3 activation) {
            std::vector<float> density = {activation.x * paperRanges[0], activation.y * paperRanges[1],
                                          activation.z * paperRanges[2]};
            return transmissionRGB(density, paperDyes, 0, viewingLight ? &*viewingLight : nullptr);
        });
    }
    return SpectralPipelineTables{exposure, *printing, paperOutput};
}

}  // namespace

}  // namespace SpectralRuntime
}  // namespace fotufilm
