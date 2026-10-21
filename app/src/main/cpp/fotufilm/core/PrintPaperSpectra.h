// Port of Sources/FotufilmCore/PrintPaperSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_PRINT_PAPER_SPECTRA_H
#define FOTUFILM_CORE_PRINT_PAPER_SPECTRA_H

#include <vector>

namespace fotufilm {
namespace PrintPaperSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<float>& neutralAmounts();
const std::vector<std::vector<float>>& layerSensitivity();

}  // namespace PrintPaperSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINT_PAPER_SPECTRA_H
