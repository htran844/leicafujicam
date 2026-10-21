// Port of Sources/FotufilmCore/FilmAbsorption.swift (SpectralRuntime extension, declared in
// SpectralModel.h).
#include "SpectralModel.h"

namespace fotufilm {
namespace SpectralRuntime {

std::optional<std::vector<float>> filmDensityOffset(const FilmStock& stock, float scale) {
    if (!stock.spectralProfile.minimumDensity) return std::nullopt;
    const std::vector<float>& base = *stock.spectralProfile.minimumDensity;
    const auto& dyes = stock.spectralProfile.imageDyeDensity;
    float minimum[3] = {stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin};
    std::vector<float> offset(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        offset[i] = (base[i] - minimum[0] * dyes[0][i] - minimum[1] * dyes[1][i] - minimum[2] * dyes[2][i]) * scale;
    }
    return offset;
}

Float3 transmissionRGB(const std::vector<float>& density, const FilmStock& stock, float neutralDensity,
                       const std::vector<float>* illuminant) {
    std::optional<std::vector<float>> offset = filmDensityOffset(stock);
    return transmissionRGB(density, stock.spectralProfile.imageDyeDensity, neutralDensity, illuminant,
                           offset ? &*offset : nullptr);
}

Float3 paperExposure(const std::vector<float>& density, const FilmStock& stock, const std::vector<float>& lamp,
                     const std::vector<std::vector<float>>& paperSensitivity, float neutralDensity,
                     float densityScale) {
    std::optional<std::vector<float>> offset = filmDensityOffset(stock, densityScale);
    return paperExposure(density, stock.spectralProfile.imageDyeDensity, lamp, paperSensitivity, neutralDensity,
                         offset ? &*offset : nullptr);
}

}  // namespace SpectralRuntime
}  // namespace fotufilm
