// Port of Sources/FotufilmCore/AgfaCP30PrintSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_AGFACP30_PRINT_SPECTRA_H
#define FOTUFILM_CORE_AGFACP30_PRINT_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace AgfaCP30PrintSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<std::vector<float>>& layerSensitivity();
const CharacteristicCurve& redCurve();
const CharacteristicCurve& greenCurve();
const CharacteristicCurve& blueCurve();

}  // namespace AgfaCP30PrintSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_AGFACP30_PRINT_SPECTRA_H
