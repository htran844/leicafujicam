// Port of Sources/FotufilmCore/UnexposedEdge.swift.
#include "UnexposedEdge.h"

#include <cmath>

#include "ParallelWork.h"

namespace fotufilm {
namespace UnexposedEdge {

float gateRadiusMM(std::optional<float> fNumber) {
    float cone = gateSeparationMM / (2 * fNumber.value_or(referenceFNumber));
    float fringe = std::sqrt(gateWavelengthMM * gateSeparationMM) / 2;
    return std::sqrt(cone * cone + fringe * fringe);
}

float gateDistance(float x, float y, Aperture aperture, float corner) {
    float qx = swiftMax(aperture.left - x, x - aperture.right) + corner;
    float qy = swiftMax(aperture.top - y, y - aperture.bottom) + corner;
    float ox = swiftMax(qx, 0.0f), oy = swiftMax(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + swiftMin(swiftMax(qx, qy), 0.0f) - corner;
}

float gateTransmission(float distanceMM, std::optional<float> fNumber) {
    float u = swiftMin(swiftMax(distanceMM / gateRadiusMM(fNumber), -1.0f), 1.0f);
    float v = std::fabs(u);
    float arc = std::sqrt(1 - v) * (1.5707288f + v * (-0.2121144f + v * (0.0742610f + v * -0.0187293f)));
    float shaded = (arc - v * std::sqrt(1 - v * v)) * 0.318309886f;
    return u >= 0 ? shaded : 1 - shaded;
}

void extend(const float* photo, size_t photoCount, int width, int height, const Margins& margins, float* extended,
            size_t extendedCount) {
    int outerWidth = width + margins.left + margins.right;
    int outerHeight = height + margins.top + margins.bottom;
    preconditionCheck(photoCount >= static_cast<size_t>(width) * height * 4 &&
                          extendedCount >= static_cast<size_t>(outerWidth) * outerHeight * 4,
                      "extend buffers are too small");
    ParallelWork::forEach(outerHeight, [&](int row) {
        size_t source = static_cast<size_t>(swiftMin(swiftMax(row - margins.top, 0), height - 1)) * width * 4;
        size_t target = static_cast<size_t>(row) * outerWidth * 4;
        for (int column = 0; column < outerWidth; ++column) {
            size_t from = source + static_cast<size_t>(swiftMin(swiftMax(column - margins.left, 0), width - 1)) * 4;
            size_t to = target + static_cast<size_t>(column) * 4;
            extended[to] = photo[from];
            extended[to + 1] = photo[from + 1];
            extended[to + 2] = photo[from + 2];
            extended[to + 3] = photo[from + 3];
        }
    });
}

}  // namespace UnexposedEdge
}  // namespace fotufilm
