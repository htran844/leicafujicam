// Port of Sources/FotufilmCore/ColorGrade.swift.
#include "ColorGrade.h"

#include <cmath>

#include "ColorScience.h"

namespace fotufilm {

namespace {
/// Element-wise 2^x.
Float3 exp2(Float3 value) { return Float3(std::exp2(value.x), std::exp2(value.y), std::exp2(value.z)); }
}  // namespace

Float3 ColorGrade::Band::tilt() const {
    float x = swiftMin(swiftMax(balanceX, -1.0f), 1.0f);
    float y = swiftMin(swiftMax(balanceY, -1.0f), 1.0f);
    Float3 warm(1, 0, -1);
    Float3 green(-0.5f, 1, -0.5f);
    return x * warm + y * green;
}

Float3 ColorGrade::lift() const {
    return Float3::repeating(shadowLift * shadows.clampedLevel()) + shadowTilt * shadows.tilt();
}

Float3 ColorGrade::gain() const {
    return exp2(Float3::repeating(highlightGain * highlights.clampedLevel()) + highlightTilt * highlights.tilt());
}

Float3 ColorGrade::inverseGamma() const {
    return exp2(-(Float3::repeating(midtoneGamma * midtones.clampedLevel()) + midtoneTilt * midtones.tilt()));
}

std::vector<float> ColorGrade::packed() const {
    Float3 l = lift(), g = gain(), e = inverseGamma();
    return {l.x, l.y, l.z, g.x, g.y, g.z, e.x, e.y, e.z};
}

const char* ColorGrade::rawValue(Space space) {
    switch (space) {
        case Space::linear: return "linear";
        case Space::encoded: return "encoded";
    }
    return "";
}

std::optional<ColorGrade::Space> ColorGrade::spaceFromRawValue(const std::string& raw) {
    if (raw == "linear") return Space::linear;
    if (raw == "encoded") return Space::encoded;
    return std::nullopt;
}

Float3 ColorGrade::apply(Float3 value, Space space) const {
    Float3 l = lift(), g = gain(), exponent = inverseGamma();
    Float3 result;
    for (int channel = 0; channel < 3; ++channel) {
        float working = space == Space::encoded ? ColorScience::gradingEncode(value[channel]) : value[channel];
        float lifted = working * (g[channel] - l[channel]) + l[channel];
        float graded = exponent[channel] == 1 ? lifted : std::pow(swiftMax(lifted, 0.0f), exponent[channel]);
        result[channel] = space == Space::encoded ? ColorScience::gradingDecode(graded) : graded;
    }
    return result;
}

}  // namespace fotufilm
