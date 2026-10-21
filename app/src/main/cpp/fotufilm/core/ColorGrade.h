// Port of Sources/FotufilmCore/ColorGrade.swift.
#ifndef FOTUFILM_CORE_COLOR_GRADE_H
#define FOTUFILM_CORE_COLOR_GRADE_H

#include <optional>
#include <string>
#include <vector>

#include "Simd.h"

namespace fotufilm {

/// Three-way colour correction applied to the finished print.
struct ColorGrade {
    /// One band's three numbers, all resting at zero.
    struct Band {
        /// -1 cool … +1 warm.
        float balanceX = 0;
        /// -1 magenta … +1 green.
        float balanceY = 0;
        /// -1 down … +1 up.
        float level = 0;

        bool isNeutral() const { return balanceX == 0 && balanceY == 0 && level == 0; }
        /// The band's channel tilt before the band's own scaling.
        Float3 tilt() const;
        float clampedLevel() const { return swiftMin(swiftMax(level, -1.0f), 1.0f); }

        bool operator==(const Band& o) const {
            return balanceX == o.balanceX && balanceY == o.balanceY && level == o.level;
        }
        bool operator!=(const Band& o) const { return !(*this == o); }
    };

    Band shadows;
    Band midtones;
    Band highlights;

    static ColorGrade neutral() { return ColorGrade(); }
    bool isNeutral() const { return shadows.isNeutral() && midtones.isNeutral() && highlights.isNeutral(); }

    static constexpr float shadowLift = 0.04f;
    static constexpr float shadowTilt = 0.02f;
    static constexpr float midtoneGamma = 0.5f;
    static constexpr float midtoneTilt = 0.3f;
    static constexpr float highlightGain = 0.6f;
    static constexpr float highlightTilt = 0.3f;

    /// Per-channel lift: the value black is carried to.
    Float3 lift() const;
    /// Per-channel gain: the multiplier white is carried by.
    Float3 gain() const;
    /// Per-channel inverse gamma — the exponent the engine raises by.
    Float3 inverseGamma() const;
    /// FOTUFILM_CONFIG_GRADE_LIFT, _GAIN and _INV_GAMMA, in order.
    std::vector<float> packed() const;

    /// Which signal the three-way corrector works on.
    enum class Space { linear, encoded };
    static const char* rawValue(Space space);
    static std::optional<Space> spaceFromRawValue(const std::string& raw);

    /// The grade applied to one display-linear print value; matches `color_grade` in Transfer.h.
    Float3 apply(Float3 value, Space space = Space::linear) const;

    bool operator==(const ColorGrade& o) const {
        return shadows == o.shadows && midtones == o.midtones && highlights == o.highlights;
    }
    bool operator!=(const ColorGrade& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_COLOR_GRADE_H
