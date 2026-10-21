// Port of Sources/FotufilmCore/PrintFinish.swift.
#ifndef FOTUFILM_CORE_PRINT_FINISH_H
#define FOTUFILM_CORE_PRINT_FINISH_H

#include <vector>

#include "Simd.h"

namespace fotufilm {

/// A scanned negative's light controls, acting on its print (FOTUFILM_CONFIG_PRINT_FINISH).
struct PrintFinish {
    /// Linear gains on the printed light.
    Float3 gains = Float3::one();
    /// −1...1 each.
    float highlights = 0;
    float shadows = 0;
    /// Chroma multiplier about each pixel's luminance; 1 is untouched.
    float saturation = 1;
    /// Signed chroma boost weighted toward the least colourful pixels, −1...1.
    float vibrance = 0;

    static PrintFinish neutral() { return PrintFinish(); }
    bool isNeutral() const { return *this == neutral(); }

    /// Every value as its offset from neutral.
    std::vector<float> packed() const;

    static constexpr float pivot = 0.18f;
    static constexpr float endStops = 0.75f;
    static constexpr float highlightReach = 3;
    static constexpr float shadowReach = 4;
    static constexpr Float3 luminance{0.2289746f, 0.6917385f, 0.0792869f};

    /// 0 at and below 0, rising smoothly to 1 at 1.
    static float ease(float t);

    /// One display-linear Display P3 value finished.
    Float3 apply(Float3 rgb) const;

    bool operator==(const PrintFinish& o) const {
        return gains == o.gains && highlights == o.highlights && shadows == o.shadows &&
               saturation == o.saturation && vibrance == o.vibrance;
    }
    bool operator!=(const PrintFinish& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINT_FINISH_H
