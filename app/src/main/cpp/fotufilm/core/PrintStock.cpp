// Port of Sources/FotufilmCore/PrintStock.swift.
#include "PrintStock.h"

#include <algorithm>

#include "SpectralGrid.h"

namespace fotufilm {

const std::vector<PrintProcess>& allPrintProcesses() {
    static const std::vector<PrintProcess> all = {PrintProcess::photoPaper, PrintProcess::releasePrintFilm,
                                                  PrintProcess::positivePaper};
    return all;
}

PrintStock::Spectra::Spectra(const std::vector<std::vector<float>>& dyeDensity,
                             const std::optional<std::vector<float>>& neutralAmounts,
                             const std::vector<std::vector<float>>& layerSensitivity, Process process) {
    preconditionCheck(dyeDensity.size() == 3 && layerSensitivity.size() == 3, "three dyes and three layers");
    for (const auto& row : dyeDensity) preconditionCheck(static_cast<int>(row.size()) == SpectralGrid::count, "dye on the grid");
    for (const auto& row : layerSensitivity) preconditionCheck(static_cast<int>(row.size()) == SpectralGrid::count, "layer on the grid");
    std::vector<std::vector<float>> amounts = dyeDensity;
    if (neutralAmounts) {
        for (size_t record = 0; record < amounts.size() && record < neutralAmounts->size(); ++record) {
            for (float& value : amounts[record]) value = value * (*neutralAmounts)[record];
        }
        amounts.resize(std::min(amounts.size(), neutralAmounts->size()));
    }
    std::vector<std::vector<float>> tailed;
    tailed.reserve(layerSensitivity.size());
    for (const auto& layer : layerSensitivity) tailed.push_back(SpectralGrid::continuedTails(layer));
    analyticalDyes = amounts;
    dyes = SpectralGrid::partition(amounts);
    sensitivity = keepsLayerSpeedScale(process) ? tailed : SpectralGrid::normalizeSensitivities(tailed);
}

PrintStock::PrintStock(std::string name_, std::string detail_, Process process_,
                       std::vector<CharacteristicCurve> curves_, Spectra spectra_,
                       std::optional<ReleaseSetup> releaseSetup_)
    : name(std::move(name_)),
      detail(std::move(detail_)),
      process(process_),
      curves(std::move(curves_)),
      spectra(std::move(spectra_)),
      releaseSetup(releaseSetup_) {
    preconditionCheck(curves.size() == 3, "a print stock develops three records");
    preconditionCheck(releaseSetup.has_value() == isProjected(process),
                      "release print film, and only it, states its setup aims");
}

}  // namespace fotufilm
