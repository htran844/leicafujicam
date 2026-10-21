// Port of Sources/FotufilmCore/EternaCP3521XDPrintSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_ETERNACP3521XD_PRINT_SPECTRA_H
#define FOTUFILM_CORE_ETERNACP3521XD_PRINT_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace EternaCP3521XDPrintSpectra {

const CharacteristicCurve& redCurve();
const CharacteristicCurve& greenCurve();
const CharacteristicCurve& blueCurve();

}  // namespace EternaCP3521XDPrintSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ETERNACP3521XD_PRINT_SPECTRA_H
