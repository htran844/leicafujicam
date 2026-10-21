// Port of Sources/FotufilmCore/LabScanFinish.swift.
#include "LabScanFinish.h"

#include <cmath>

#include "ColorScience.h"

namespace fotufilm {
namespace LabScanFinish {

const std::vector<HueCorrection>& corrections() {
    static const std::vector<HueCorrection> values = {
        {45, 35, 0.97f, 2.3f},    // reds, oranges, skin
        {135, 35, 0.89f, 5.5f},   // foliage toward teal, muted
        {240, 35, 1.05f, -6.4f},  // cyans and blues toward cyan
    };
    return values;
}

Float3 apply(Float3 colour, float strengthIn, bool chromatic, float floor) {
    float strength = std::isfinite(strengthIn) ? swiftMin(swiftMax(strengthIn, 0.0f), 1.0f) : 1.0f;
    if (!(strength > 0)) return colour;
    Float3 toned = graded(colour, strength);
    if (!chromatic) return toned;
    Float3 lab = oklab(ColorScience::linearDisplayP3ToSRGB(toned));
    float l = lab.x;

    // The crossover on near-neutral colours: cyan-blue shadows and lower mid-tones.
    Float3 weights = ColorScience::displayP3LuminanceWeights;
    float level = weights.x * colour.x + weights.y * colour.y + weights.z * colour.z;
    float onset = floor > 0 ? clamp(std::log2(swiftMax(level, 1e-12f) / floor) - 0.5f, 0.0f, 1.0f) : 1.0f;
    float shadow = clamp((0.70f - l) / 0.40f, 0.0f, 1.0f) * clamp((l - 0.04f) / 0.16f, 0.0f, 1.0f) * onset;
    float saturation = std::sqrt(lab.y * lab.y + lab.z * lab.z) / swiftMax(l, 1e-3f);
    float neutral = l * std::exp(-saturation / castSaturation);
    Float2 cast = Float2(strength * neutral * shadow * shadowCast.x, strength * neutral * shadow * shadowCast.y);
    lab.y += cast.x;
    lab.z += cast.y;

    // The hue corrections, faded out in the deepest shadows, toward white, and over saturated colours.
    float chroma = std::sqrt(lab.y * lab.y + lab.z * lab.z);
    float hue = std::atan2(lab.z, lab.y) * 180 / kFloatPi;
    float gain = 1;
    float turn = 0;
    for (const HueCorrection& correction : corrections()) {
        float distance = std::fmod(hue - correction.hue, 360.0f);
        if (distance > 180) distance -= 360;
        if (distance < -180) distance += 360;
        float weight = std::exp(-0.5f * (distance / correction.width) * (distance / correction.width));
        gain *= 1 + strength * (correction.chroma - 1) * weight;
        turn += strength * correction.turn * weight;
    }
    float saturated = clamp((chroma - correctionFadeLower) / (correctionFadeUpper - correctionFadeLower), 0.0f, 1.0f);
    float fade = clamp(l / 0.25f, 0.0f, 1.0f) * clamp((1 - l) / 0.08f, 0.0f, 1.0f) *
                 (1 - saturated * saturated * (3 - 2 * saturated));
    chroma *= 1 + (gain - 1) * fade;
    hue += turn * fade;
    float radians = hue * kFloatPi / 180;
    lab.y = chroma * std::cos(radians);
    lab.z = chroma * std::sin(radians);
    return ColorScience::linearSRGBToDisplayP3(linearFromOklab(lab));
}

Float3 graded(Float3 colour, float strength) {
    Float3 weights = ColorScience::displayP3LuminanceWeights;
    float luminance = weights.x * colour.x + weights.y * colour.y + weights.z * colour.z;
    if (!(luminance > 1e-6f)) return colour;
    float level = std::cbrt(luminance);
    float upper = clamp((level - 0.55f) / 0.45f, 0.0f, 1.0f);
    float lifted = level + strength * (highlightLift * std::sin(kFloatPi * upper) -
                                       blackPull * clamp(1 - level / 0.35f, 0.0f, 1.0f));
    float target = swiftMax(lifted, 0.0f);
    return colour * (target * target * target / luminance);
}

Float3 oklab(Float3 rgb) {
    float l = std::cbrt(0.4122214708f * rgb.x + 0.5363325363f * rgb.y + 0.0514459929f * rgb.z);
    float m = std::cbrt(0.2119034982f * rgb.x + 0.6806995451f * rgb.y + 0.1073969566f * rgb.z);
    float s = std::cbrt(0.0883024619f * rgb.x + 0.2817188376f * rgb.y + 0.6299787005f * rgb.z);
    return Float3(0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                  1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                  0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s);
}

Float3 linearFromOklab(Float3 lab) {
    float l = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    float m = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    float s = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
    float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
    return Float3(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3,
                  -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
                  -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3);
}

}  // namespace LabScanFinish
}  // namespace fotufilm
