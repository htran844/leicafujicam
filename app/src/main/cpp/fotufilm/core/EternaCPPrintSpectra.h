// Port of Sources/FotufilmCore/EternaCPPrintSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_ETERNACP_PRINT_SPECTRA_H
#define FOTUFILM_CORE_ETERNACP_PRINT_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace EternaCPPrintSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<std::vector<float>>& layerSensitivity();
const CharacteristicCurve& cyanCurve();
const CharacteristicCurve& magentaCurve();
const CharacteristicCurve& yellowCurve();

}  // namespace EternaCPPrintSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ETERNACP_PRINT_SPECTRA_H
