// Port of Sources/FotufilmCore/HDRHighlightRange.swift.
#include "HDRHighlightRange.h"

#include <cmath>

#include "Simd.h"

namespace fotufilm {
namespace HDRHighlightRange {

float keptHeadroom(float headroom, float range) {
    if (!(headroom > 1)) return 1;
    return std::exp2(swiftMin(swiftMax(range, 0.0f), 1.0f) * std::log2(headroom));
}

float curvature(float headroom, float range) {
    if (!(headroom > 1)) return 0;
    float full = std::log2(headroom / knee);
    float kept = std::log2(keptHeadroom(headroom, range) / knee);
    if (!(full > kept && kept > 0)) return 0;
    return (full - kept) / (full * kept);
}

float gain(float peak, float curvature) {
    if (!(curvature > 0 && peak > knee)) return 1;
    float stops = std::log2(peak / knee);
    return std::exp2(stops / (1 + curvature * stops) - stops);
}

}  // namespace HDRHighlightRange
}  // namespace fotufilm
