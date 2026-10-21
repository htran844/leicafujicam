// Port of Sources/FotufilmCore/LabScanTiming.swift.
#ifndef FOTUFILM_CORE_LAB_SCAN_TIMING_H
#define FOTUFILM_CORE_LAB_SCAN_TIMING_H

#include <optional>
#include <vector>

#include "AutoAdjustment.h"
#include "CharacteristicCurve.h"
#include "Simd.h"

namespace fotufilm {

struct FilmStock;

/// Lab Scan's per-frame setup: the scanner's auto setup and the operator's density and colour keys.
namespace LabScanTiming {

constexpr float whiteDensity = 0.08f;
constexpr float maxStretch = 1.5f;
constexpr float highlightSpan = 3;
constexpr float backlightShare = 0.5f;
constexpr float backlightMaxStops = 2;
constexpr float keyShare = 0.5f;
constexpr float keyReach = 0.5f;

/// The highlight the white point is set on, from the whole-frame measurement, in stops.
float highlight(const AutoAdjustment::SceneStops& scene);

/// The scanner's dodging, in the tone controls' units.
struct Dodge {
    float hold;
    float lift;
    /// The stop the regions are keyed on: the frame's median.
    float key;
    bool operator==(const Dodge& o) const { return hold == o.hold && lift == o.lift && key == o.key; }
};

constexpr float dodgeHighlightSpan = 3;
constexpr float dodgeShadowSpan = 3.5f;
constexpr float dodgeRamp = 3;
constexpr float dodgeMaxHold = 0.15f;
constexpr float dodgeMaxLift = 0.2f;
constexpr float dodgingRangeLower = 0;
constexpr float dodgingRangeUpper = 2;

Dodge dodge(const AutoAdjustment::SceneStops& scene, float strength = 1);

/// A frame's setup: each film layer forms at x the density it would form at
/// `x + shift + keys[layer]`; each scan record reads `scale * read + print`.
struct Setup {
    float shift;
    Float3 keys;
    Float3 scale = Float3::one();
    Float3 print = Float3();

    static Setup identity() { return Setup{0, Float3(), Float3::one(), Float3()}; }

    /// The stock's records as this setup exposes them.
    std::vector<CharacteristicCurve> curves(const FilmStock& stock) const;
    /// A capture layer that answers no colour key, such as a donor layer.
    CharacteristicCurve uncoloured(const CharacteristicCurve& curve) const { return curve.reexposed(shift); }

    bool operator==(const Setup& o) const {
        return shift == o.shift && keys == o.keys && scale == o.scale && print == o.print;
    }
};

constexpr float recordAnchorBelow = 2;
constexpr float recordAnchorSpan = 1.5f;
constexpr float recordAnchorToeClearance = 1.5f;

Setup setup(const FilmStock& stock, std::optional<float> sceneHighlightStops, std::optional<float> sceneMedianStops,
            float exposureEV = 0, float scanExposure = 0, Float3 keys = Float3());

/// The fixed profile's points.
struct Points {
    float white;
    float toe;
    Float3 masking;
};
Points profilePoints(const FilmStock& stock);

/// What the scanner's records read of a neutral patch `stops` over mid-grey, or of the bare film
/// base for nullopt.
Float3 reads(const FilmStock& stock, std::optional<float> stops);

}  // namespace LabScanTiming
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_LAB_SCAN_TIMING_H
