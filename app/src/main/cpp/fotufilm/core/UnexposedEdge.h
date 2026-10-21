// Port of Sources/FotufilmCore/UnexposedEdge.swift: the develop request, margins, the gate and
// carrier laws and `extend`. The border geometry presets (`Geometry`, FilmBorderGeometry) and
// `TakingAperture` (SensorFrame) belong to the print frame, which is not ported. The Options
// extension (`pixelsPerMM`, `gateConfiguration`, ...) is declared in Pipeline.h.
#ifndef FOTUFILM_CORE_UNEXPOSED_EDGE_H
#define FOTUFILM_CORE_UNEXPOSED_EDGE_H

#include <cstddef>
#include <optional>

#include "Simd.h"

namespace fotufilm {

/// The film just outside the camera gate, which the Emulsion Border prints around the photograph.
namespace UnexposedEdge {

/// The film's width on each side of the photograph, in pixels: the band, then `carrier` more
/// beyond the carrier's opening. No carrier is 0.
struct Margins {
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
    int carrier = 0;

    bool operator==(const Margins& o) const {
        return left == o.left && right == o.right && top == o.top && bottom == o.bottom && carrier == o.carrier;
    }
};

/// Set on the options of a develop of that larger piece of film.
struct Develop {
    Margins margins;
    std::optional<float> sceneHighlightStops;
    std::optional<Float3> sceneChannelMedians;
    std::optional<Float3> sceneToneStops;
    /// The lens's f-number on this gauge; nil draws the shadow at `referenceFNumber`.
    std::optional<float> fNumber;
};

constexpr float gateSeparationMM = 0.15f;
constexpr float referenceFNumber = 2;
constexpr float gateWavelengthMM = 0.00055f;
constexpr float gateCornerRadiusMM = 0.15f;
constexpr float carrierCornerRadiusMM = 0.3f;
constexpr float carrierShadowRadiusMM = 0.15f;
constexpr float carrierFilingMM = 0.025f;
constexpr float carrierFilingCellMM = 2;
inline float carrierReachMM() { return carrierShadowRadiusMM + 0.07f; }

/// Radius of the pupil's shadow of the gate edge on the film.
float gateRadiusMM(std::optional<float> fNumber = std::nullopt);

struct Aperture {
    float left;
    float top;
    float right;
    float bottom;
};
/// Signed distance, in pixels, from the rounded aperture's edge: positive beyond it.
float gateDistance(float x, float y, Aperture aperture, float corner);
/// Share of the lens's light that passes the gate edge at `distanceMM` beyond it.
float gateTransmission(float distanceMM, std::optional<float> fNumber = std::nullopt);

/// The lens's image over the larger piece of film: `photo` (interleaved RGBA) continued outward
/// past its edges. `extended` holds `(width + left + right) * (height + top + bottom) * 4`.
void extend(const float* photo, size_t photoCount, int width, int height, const Margins& margins, float* extended,
            size_t extendedCount);

}  // namespace UnexposedEdge
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_UNEXPOSED_EDGE_H
