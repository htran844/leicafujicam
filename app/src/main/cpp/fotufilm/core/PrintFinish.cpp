// Port of Sources/FotufilmCore/PrintFinish.swift.
#include "PrintFinish.h"

#include <cmath>

namespace fotufilm {

std::vector<float> PrintFinish::packed() const {
    return {gains.x - 1, gains.y - 1, gains.z - 1, highlights, shadows, saturation - 1, vibrance};
}

float PrintFinish::ease(float t) {
    float x = swiftMin(swiftMax(t, 0.0f), 1.0f);
    return x * x * (3 - 2 * x);
}

Float3 PrintFinish::apply(Float3 rgb) const {
    Float3 light = rgb * gains;
    float y = (light * luminance).sum();
    if (y > 1e-6f) {
        float stops = std::log2(y / pivot);
        float moved = highlights * endStops * ease(stops / highlightReach)
            + shadows * endStops * ease(-stops / shadowReach);
        light *= std::pow(2.0f, moved);
    }
    float luma = (light * luminance).sum();
    float peak = light.max();
    float colourfulness = (peak - light.min()) / swiftMax(peak, 1e-6f);
    float chroma = saturation * (1 + vibrance * (1 - colourfulness));
    return luma + chroma * (light - luma);
}

}  // namespace fotufilm
