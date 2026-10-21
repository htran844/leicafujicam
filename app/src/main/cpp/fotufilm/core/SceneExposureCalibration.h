// Port of Sources/FotufilmCore/SceneExposureCalibration.swift.
#ifndef FOTUFILM_CORE_SCENE_EXPOSURE_CALIBRATION_H
#define FOTUFILM_CORE_SCENE_EXPOSURE_CALIBRATION_H

#include <cstddef>

namespace fotufilm {
namespace SceneExposureCalibration {

/// The gain that aligns the middle of a scene-linear HDR rendition with its platform SDR reference.
/// Both inputs are interleaved linear Rec.2020 RGBA of `count` floats at identical geometry.
float referenceGain(const float* sceneLinearHDR, const float* linearSDRReference, size_t count);

}  // namespace SceneExposureCalibration
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SCENE_EXPOSURE_CALIBRATION_H
