// Port of Sources/FotufilmCore/EXR5386PrintSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_EXR5386_PRINT_SPECTRA_H
#define FOTUFILM_CORE_EXR5386_PRINT_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace EXR5386PrintSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<std::vector<float>>& layerSensitivity();
const CharacteristicCurve& redCurve();
const CharacteristicCurve& greenCurve();
const CharacteristicCurve& blueCurve();

}  // namespace EXR5386PrintSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_EXR5386_PRINT_SPECTRA_H
