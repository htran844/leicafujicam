// Port of Sources/FotufilmCore/Vision2383PrintSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_VISION2383_PRINT_SPECTRA_H
#define FOTUFILM_CORE_VISION2383_PRINT_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace Vision2383PrintSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<std::vector<float>>& layerSensitivity();
const CharacteristicCurve& redCurve();
const CharacteristicCurve& greenCurve();
const CharacteristicCurve& blueCurve();

}  // namespace Vision2383PrintSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_VISION2383_PRINT_SPECTRA_H
