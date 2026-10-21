// Port of Sources/FotufilmCore/CrystalArchivePaperSpectra.swift (published data, values unchanged).
#ifndef FOTUFILM_CORE_CRYSTAL_ARCHIVE_PAPER_SPECTRA_H
#define FOTUFILM_CORE_CRYSTAL_ARCHIVE_PAPER_SPECTRA_H

#include <vector>

namespace fotufilm {
namespace CrystalArchivePaperSpectra {

const std::vector<std::vector<float>>& dyeDensity();
const std::vector<float>& neutralAmounts();
const std::vector<std::vector<float>>& layerSensitivity();

}  // namespace CrystalArchivePaperSpectra
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_CRYSTAL_ARCHIVE_PAPER_SPECTRA_H
