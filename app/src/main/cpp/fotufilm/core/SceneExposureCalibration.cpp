// Port of Sources/FotufilmCore/SceneExposureCalibration.swift.
#include "SceneExposureCalibration.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ColorScience.h"

namespace fotufilm {
namespace SceneExposureCalibration {

float referenceGain(const float* hdr, const float* sdr, size_t count) {
    if (hdr == nullptr || sdr == nullptr || count < 4 || count % 4 != 0) return 1;
    size_t pixelCount = count / 4;
    size_t stride = std::max<size_t>(1, pixelCount / 65536);
    Float3 weights = ColorScience::luminanceWeights;
    std::vector<float> offsets;
    offsets.reserve(std::min<size_t>(pixelCount, 65536));
    for (size_t pixel = 0; pixel < pixelCount; pixel += stride) {
        size_t index = pixel * 4;
        float reference = weights.x * sdr[index] + weights.y * sdr[index + 1] + weights.z * sdr[index + 2];
        // Stay above decode noise and below the reference rendition's shoulder.
        if (!(reference >= 0.01f && reference <= 0.65f)) continue;
        float scene = weights.x * hdr[index] + weights.y * hdr[index + 1] + weights.z * hdr[index + 2];
        if (!(std::isfinite(scene) && std::isfinite(reference) && scene > 0)) continue;
        offsets.push_back(std::log2(scene / reference));
    }
    if (offsets.size() < std::min<size_t>(64, std::max<size_t>(8, pixelCount / 16))) return 1;
    std::sort(offsets.begin(), offsets.end());
    size_t middle = offsets.size() / 2;
    float median = offsets.size() % 2 == 0 ? (offsets[middle - 1] + offsets[middle]) * 0.5f : offsets[middle];
    if (!(std::isfinite(median) && median > 0)) return 1;
    // A malformed pair cannot brighten the source or move it by more than two stops.
    return swiftMin(1.0f, swiftMax(0.25f, std::exp2(-median)));
}

}  // namespace SceneExposureCalibration
}  // namespace fotufilm
