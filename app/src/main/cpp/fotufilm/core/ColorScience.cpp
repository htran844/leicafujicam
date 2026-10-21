// Port of Sources/FotufilmCore/ColorScience.swift.
#include "ColorScience.h"

#include <cmath>

namespace fotufilm {
namespace ColorScience {

Float3 linearSRGBToRec2020(Float3 rgb) {
    return Float3(
        0.627403896f * rgb.x + 0.329283038f * rgb.y + 0.043313066f * rgb.z,
        0.069097289f * rgb.x + 0.919540395f * rgb.y + 0.011362316f * rgb.z,
        0.016391439f * rgb.x + 0.088013308f * rgb.y + 0.895595253f * rgb.z);
}

Float3 linearSRGBToDisplayP3(Float3 rgb) {
    return Float3(
        0.822461969f * rgb.x + 0.177538031f * rgb.y,
        0.033194199f * rgb.x + 0.966805801f * rgb.y,
        0.017082631f * rgb.x + 0.072397441f * rgb.y + 0.910519929f * rgb.z);
}

Float3 linearDisplayP3ToSRGB(Float3 rgb) {
    return Float3(
        1.22494018f * rgb.x - 0.224940176f * rgb.y,
        -0.042056955f * rgb.x + 1.04205695f * rgb.y,
        -0.019637555f * rgb.x - 0.078636046f * rgb.y + 1.09827360f * rgb.z);
}

Float3 linearDisplayP3ToRec2020(Float3 rgb) {
    return Float3(
        0.753833034f * rgb.x + 0.198597369f * rgb.y + 0.047569597f * rgb.z,
        0.045743849f * rgb.x + 0.941777220f * rgb.y + 0.012478931f * rgb.z,
        -0.001210340f * rgb.x + 0.017601717f * rgb.y + 0.983608623f * rgb.z);
}

Float3 linearRec2020ToDisplayP3(Float3 rgb) {
    return Float3(
        1.343578253f * rgb.x - 0.282179671f * rgb.y - 0.061398582f * rgb.z,
        -0.065297453f * rgb.x + 1.075787916f * rgb.y - 0.010490463f * rgb.z,
        0.002821787f * rgb.x - 0.019598495f * rgb.y + 1.016776707f * rgb.z);
}

Float3 linearRec2020ToExposureDomain(Float3 rgb) {
    return Float3(
        0.670231843f * rgb.x + 0.152168745f * rgb.y + 0.177599412f * rgb.z,
        0.044501114f * rgb.x + 0.854482372f * rgb.y + 0.101016514f * rgb.z,
        0.025777047f * rgb.y + 0.974222953f * rgb.z);
}

Float3 linearExposureDomainToRec2020(Float3 rgb) {
    return Float3(
        1.509371119f * rgb.x - 0.261310044f * rgb.y - 0.248061075f * rgb.z,
        -0.078854123f * rgb.x + 1.187622946f * rgb.y - 0.108768823f * rgb.z,
        0.002086408f * rgb.x - 0.031423416f * rgb.y + 1.029337008f * rgb.z);
}

float srgbToLinear(float v) {
    if (v <= 0.04045f) return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float v) {
    float c = clamp(v, 0.0f, 1.0f);
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
}

float gradingEncode(float linear) {
    if (linear <= 0.0031308f) return linear * 12.92f;
    if (linear >= 1) return 1 + (linear - 1) * srgbSlopeAtWhite;
    return 1.055f * std::pow(linear, 1 / 2.4f) - 0.055f;
}

float gradingDecode(float coded) {
    if (coded <= 0.04045f) return coded / 12.92f;
    if (coded >= 1) return 1 + (coded - 1) / srgbSlopeAtWhite;
    return std::pow((coded + 0.055f) / 1.055f, 2.4f);
}

float displayShoulder(float x, float knee) { return highlightShoulder(x, 1, knee); }

float highlightShoulder(float x, float ceiling, float knee_) {
    float knee = swiftMin(swiftMax(knee_, 0.0f), ceiling);
    if (!(x > knee)) return x;
    float over = x - knee, room = ceiling - knee;
    if (!(room > 0)) return ceiling;
    return knee + room * over / (over + room);
}

}  // namespace ColorScience
}  // namespace fotufilm
