// Port of Sources/FotufilmCore/LabScanTiming.swift.
#include "LabScanTiming.h"

#include <cmath>
#include <map>
#include <mutex>

#include "FilmStock.h"
#include "PrintPaperTables.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace LabScanTiming {

float highlight(const AutoAdjustment::SceneStops& scene) {
    float excess = swiftMax(0.0f, scene.bright - scene.median - highlightSpan);
    return scene.bright - swiftMin(backlightMaxStops, backlightShare * excess);
}

Dodge dodge(const AutoAdjustment::SceneStops& scene, float strength) {
    auto ramp = [](float reach, float span) { return swiftMin(swiftMax((reach - span) / dodgeRamp, 0.0f), 1.0f); };
    float share = std::isfinite(strength) ? swiftMin(swiftMax(strength, dodgingRangeLower), dodgingRangeUpper) : 1.0f;
    return Dodge{swiftMin(share * dodgeMaxHold * ramp(scene.bright - scene.median, dodgeHighlightSpan), 1.0f),
                 swiftMin(share * dodgeMaxLift * ramp(scene.median - scene.dark, dodgeShadowSpan), 1.0f), scene.median};
}

std::vector<CharacteristicCurve> Setup::curves(const FilmStock& stock) const {
    std::vector<CharacteristicCurve> result;
    for (size_t i = 0; i < stock.curves.size(); ++i) {
        result.push_back(stock.curves[i].reexposed(shift + keys[static_cast<int>(i)]));
    }
    return result;
}

Setup setup(const FilmStock& stock, std::optional<float> sceneHighlightStops, std::optional<float> sceneMedianStops,
            float exposureEV, float scanExposure, Float3 keys) {
    if (stock.isReversal || stock.isReflectionPrint) return Setup::identity();
    Float3 colour;
    if (!stock.isMonochrome) {
        for (int layer = 0; layer < 3; ++layer) {
            colour[layer] = std::isfinite(keys[layer]) ? -keyReach * swiftMin(swiftMax(keys[layer], -1.0f), 1.0f) : 0;
        }
    }
    Points points = profilePoints(stock);
    float ev = std::isfinite(exposureEV) ? exposureEV : 0;
    float contrast = 1;
    float middle = 0;
    float high = 0;
    float stops = std::isfinite(scanExposure) ? swiftMin(swiftMax(scanExposure, -6.0f), 6.0f) : 0;
    bool metered = sceneHighlightStops && std::isfinite(*sceneHighlightStops) && sceneMedianStops &&
                   std::isfinite(*sceneMedianStops);
    if (metered) {
        middle = swiftMin(swiftMax(*sceneMedianStops - ev, -12.0f), 12.0f);
        high = swiftMax(swiftMin(swiftMax(*sceneHighlightStops - ev, -12.0f), 12.0f), middle);
        float reach = high - points.toe;
        contrast = reach > 1e-3f ? swiftMin(swiftMax((points.white - points.toe) / reach, 1.0f), maxStretch) : maxStretch;
    } else if (stops == 0) {
        return Setup{0, colour, Float3::one(), Float3()};
    }
    auto placed = [&](float s) { return points.toe + contrast * (s - points.toe); };
    if (metered) stops += swiftMin(0.0f, (1 - keyShare) * middle - placed(middle), points.white - placed(high));
    // Each record scans a grey where the fixed profile scans the grey at placed(x) + ev + stops.
    float lift = swiftMax(stops, 0.0f);
    float low = swiftMin(
        swiftMax(middle - recordAnchorBelow, points.toe + (recordAnchorToeClearance - ev - stops) / contrast), middle);
    float top = swiftMax(high, middle + recordAnchorSpan);
    auto read = [&](float s) { return reads(stock, s) * points.masking; };
    Float3 readLow = read(low + ev + lift), readHigh = read(top + ev + lift);
    Float3 targetLow = read(placed(low) + ev + stops);
    Float3 targetHigh = read(placed(top) + ev + stops);
    Float3 scale = Float3::one();
    for (int c = 0; c < 3; ++c) {
        if (std::fabs(readHigh[c] - readLow[c]) > 1e-3f) {
            scale[c] = (targetHigh[c] - targetLow[c]) / (readHigh[c] - readLow[c]);
        }
    }
    return Setup{lift * static_cast<float>(std::log10(2.0)), colour, scale, targetLow - scale * readLow};
}

namespace {
std::mutex& pointsLock() {
    static std::mutex lock;
    return lock;
}
std::map<uint64_t, Points>& pointsCache() {
    static std::map<uint64_t, Points> cache;
    return cache;
}
}  // namespace

Points profilePoints(const FilmStock& stock) {
    uint64_t key = SpectralRuntime::cacheIdentifier(stock, PrintPaper::labScan);
    {
        std::lock_guard<std::mutex> guard(pointsLock());
        auto found = pointsCache().find(key);
        if (found != pointsCache().end()) return found->second;
    }
    CharacteristicCurve curve = printCurve(PrintPaper::labScan, stock);
    // FotufilmEngine.Options().printCorrection is 0.
    std::vector<float> scale = stock.printingContrastScale(0, PrintPaper::labScan);
    Float3 masking(scale[0], scale[1], scale[2]);
    float read = (curve.logExposure(curve.dMin + whiteDensity) - printExposureMidpoints(PrintPaper::labScan, stock)[1]) /
                 masking.y;
    // The green read falls as exposure rises; bisect the stop for the read.
    float low = -16, high = 16;
    for (int i = 0; i < 40; ++i) {
        float middle = (low + high) / 2;
        if (reads(stock, middle).y > read) low = middle; else high = middle;
    }
    float stop = reads(stock, -0.5f).y - reads(stock, 0.5f).y;
    float toe = -(reads(stock, std::nullopt).y - reads(stock, 0.0f).y) / swiftMax(stop, 1e-3f);
    Points points{(low + high) / 2, swiftMin(toe, -1.0f), masking};
    std::lock_guard<std::mutex> guard(pointsLock());
    pointsCache()[key] = points;
    return points;
}

Float3 reads(const FilmStock& stock, std::optional<float> stops) {
    PrintPaper paper = PrintPaper::labScan;
    float callier = SpectralRuntime::callierCoefficient(1, stock, paper);
    float logExposure = stops.value_or(-40.0f) * static_cast<float>(std::log10(2.0));
    auto density = [&](float x) {
        return std::vector<float>{stock.developedDensity(0, x) * callier, stock.developedDensity(1, x) * callier,
                                  stock.developedDensity(2, x) * callier};
    };
    SpectralRuntime::PrintingIllumination mid = SpectralRuntime::printingIllumination(
        stock, paper, density(0), stock.spectralProfile.imageDyeDensity, 0, callier);
    Float3 energy =
        SpectralRuntime::paperExposure(density(logExposure), stock, mid.lamp, sensitivity(paper), 0, callier);
    Float3 ratio;
    for (int c = 0; c < 3; ++c) {
        ratio[c] = std::log10(swiftMax(energy[c], 1e-12f) / swiftMax(mid.referenceEnergy[c], 1e-12f));
    }
    return ratio + SpectralRuntime::referenceCastOffset(mid.referenceEnergy, stock, paper);
}

}  // namespace LabScanTiming
}  // namespace fotufilm
