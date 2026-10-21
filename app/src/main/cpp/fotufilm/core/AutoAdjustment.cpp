// Port of Sources/FotufilmCore/AutoAdjustment.swift.
#include "AutoAdjustment.h"

#include <algorithm>
#include <map>
#include <mutex>

#include "FilmStock.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace AutoAdjustment {

namespace {
/// The window is asked for on every HDR develop; once per stock, paper and correction is enough.
struct LatitudeKey {
    uint64_t tables;
    float correction;
    bool operator<(const LatitudeKey& o) const {
        if (tables != o.tables) return tables < o.tables;
        // Hashable Float: equal values (including +0/-0) are one key.
        return correction < o.correction;
    }
};
std::mutex latitudeLock;
std::map<LatitudeKey, Latitude>& latitudeCache() {
    static std::map<LatitudeKey, Latitude> cache;
    return cache;
}
}  // namespace

Latitude latitude(const FilmStock& stock, float printCorrection, PrintPaper paper, float callier) {
    SpectralRuntime::TableOptions identity;
    identity.callier = callier;
    LatitudeKey key{SpectralRuntime::cacheIdentifier(stock, paper, identity), printCorrection};
    {
        std::lock_guard<std::mutex> guard(latitudeLock);
        auto found = latitudeCache().find(key);
        if (found != latitudeCache().end()) return found->second;
    }
    const float step = 0.25f;
    const float reach = 12;
    std::vector<float> stops = strideThrough(-reach, reach, step);
    SpectralRuntime::ToneScaleOptions options;
    options.callier = callier;
    std::vector<float> scale = SpectralRuntime::neutralToneScale(stops, stock, paper, printCorrection, options);
    std::vector<float> logScale(scale.size());
    for (size_t i = 0; i < scale.size(); ++i) logScale[i] = std::log2(swiftMax(scale[i], 1e-6f));
    auto slope = [&](int index) { return (logScale[index + 1] - logScale[index - 1]) / (2 * step); };
    auto separation = [&](int index) {
        float value = slope(index);
        return isNegative(paper) ? std::fabs(value) : value;
    };
    int count = static_cast<int>(stops.size());
    int mid = count / 2;
    float floor = kSlopeFloor * swiftMax(separation(mid), 1e-4f);
    int high = mid, low = mid;
    while (high + 1 < count - 1 && separation(high + 1) >= floor) high += 1;
    while (low - 1 > 0 && separation(low - 1) >= floor) low -= 1;
    Latitude window{stops[low], stops[high]};
    {
        std::lock_guard<std::mutex> guard(latitudeLock);
        latitudeCache()[key] = window;
    }
    return window;
}

std::optional<SceneStops> SceneStops::fromRegionStops(const std::vector<float>& regionStops) {
    std::vector<float> sorted = regionStops;
    std::stable_sort(sorted.begin(), sorted.end());
    return fromSortedRegionStops(sorted);
}

std::optional<SceneStops> SceneStops::fromSortedRegionStops(const std::vector<float>& sorted) {
    if (sorted.empty()) return std::nullopt;
    auto percentile = [&](float q) {
        float position = q * static_cast<float>(sorted.size() - 1);
        int low = static_cast<int>(position);
        int high = std::min(low + 1, static_cast<int>(sorted.size()) - 1);
        float fraction = position - static_cast<float>(low);
        return sorted[low] + fraction * (sorted[high] - sorted[low]);
    };
    return SceneStops(percentile(0.5f), percentile(0.995f), percentile(0.005f));
}

Solution solve(const std::vector<float>& regionStops, const FilmStock& stock, float printCorrection) {
    std::optional<SceneStops> scene = SceneStops::fromRegionStops(regionStops);
    if (!scene) return Solution::neutral();
    return solve(*scene, latitude(stock, printCorrection));
}

Solution solve(const SceneStops& scene, Latitude window) {
    float median = scene.median;
    float bright = scene.bright;
    float dark = scene.dark;

    float exposure = kMedianTarget - median;
    exposure = swiftMin(exposure, window.highlights - bright);
    exposure = swiftMin(swiftMax(exposure, -3.0f), 3.0f);

    float highlights = 0;
    float excessHigh = (bright + exposure) - (window.highlights - kHighlightHeadroom);
    if (excessHigh > 0) {
        highlights = -swiftMin(1.0f, excessHigh / swiftMax(3 * toneMask(bright + exposure), 1e-3f));
    }
    float shadows = 0;
    float excessLow = (window.shadows + kShadowHeadroom) - (dark + exposure);
    if (excessLow > 0) {
        shadows = swiftMin(1.0f, excessLow / swiftMax(3 * toneMask(-(dark + exposure)), 1e-3f));
    }
    return Solution{exposure, highlights, shadows};
}

float toneMask(float stops) {
    float t = swiftMin(swiftMax(stops / 6, 0.0f), 1.0f);
    return t * t * (3 - 2 * t);
}

float kDiffuseWhiteStops() {
    static const float value = -std::log2(0.18f);
    return value;
}

float headroomHighlights(float contentHeadroom, Latitude window) {
    if (!(contentHeadroom > 1)) return 0;
    float top = kDiffuseWhiteStops() + std::log2(contentHeadroom);
    float excess = top - swiftMax(window.highlights, kDiffuseWhiteStops());
    if (!(excess > 0)) return 0;
    return -swiftMin(1.0f, excess / swiftMax(3 * toneMask(top), 1e-3f));
}

}  // namespace AutoAdjustment
}  // namespace fotufilm
