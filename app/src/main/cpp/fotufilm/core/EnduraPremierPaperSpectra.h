// Port of Sources/FotufilmCore/EnduraPremierPaperSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_ENDURA_PREMIER_PAPER_SPECTRA_H
#define FOTUFILM_CORE_ENDURA_PREMIER_PAPER_SPECTRA_H

#include <vector>
#include "CharacteristicCurve.h"

namespace fotufilm {
namespace EnduraPremierPaperSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<float>& neutralAmounts();
const std::vector<std::vector<float>>& layerSensitivity();
const CharacteristicCurve& redCurve();
const CharacteristicCurve& greenCurve();
const CharacteristicCurve& blueCurve();

}  // namespace EnduraPremierPaperSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ENDURA_PREMIER_PAPER_SPECTRA_H
