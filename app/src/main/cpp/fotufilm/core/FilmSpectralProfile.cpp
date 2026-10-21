// Port of Sources/FotufilmCore/SpectralModel.swift: FilmDyeFamily, NegativeViewing and
// FilmSpectralProfile.
#include "FilmSpectralProfile.h"

#include <cmath>
#include <cstring>
#include <string>

#include "Simd.h"
#include "SpectralGrid.h"

namespace fotufilm {

const char* rawValue(FilmDyeFamily family) {
    switch (family) {
        case FilmDyeFamily::kodakNegative: return "kodakNegative";
        case FilmDyeFamily::fujiNegative: return "fujiNegative";
        case FilmDyeFamily::motionNegative: return "motionNegative";
        case FilmDyeFamily::kodachrome: return "kodachrome";
        case FilmDyeFamily::monochrome: return "monochrome";
    }
    return "";
}

std::optional<FilmDyeFamily> filmDyeFamilyFromRawValue(const std::string& raw) {
    if (raw == "kodakNegative") return FilmDyeFamily::kodakNegative;
    if (raw == "fujiNegative") return FilmDyeFamily::fujiNegative;
    if (raw == "motionNegative") return FilmDyeFamily::motionNegative;
    if (raw == "kodachrome") return FilmDyeFamily::kodachrome;
    if (raw == "monochrome") return FilmDyeFamily::monochrome;
    return std::nullopt;
}

const char* rawValue(NegativeViewing viewing) {
    switch (viewing) {
        case NegativeViewing::lightBox: return "light-box";
        case NegativeViewing::scanner: return "scanner";
    }
    return "";
}

std::optional<NegativeViewing> negativeViewingFromRawValue(const std::string& raw) {
    if (raw == "light-box") return NegativeViewing::lightBox;
    if (raw == "scanner") return NegativeViewing::scanner;
    return std::nullopt;
}

const char* name(NegativeViewing viewing) {
    switch (viewing) {
        case NegativeViewing::lightBox: return "Light Box";
        case NegativeViewing::scanner: return "Scanner";
    }
    return "";
}

const char* detail(NegativeViewing viewing) {
    switch (viewing) {
        case NegativeViewing::lightBox: return "Keeps the film\xE2\x80\x99s orange base, lit just under white.";
        case NegativeViewing::scanner: return "Divides the film base out, so clear film reads white.";
    }
    return "";
}

uint64_t ordinal(NegativeViewing viewing) {
    switch (viewing) {
        case NegativeViewing::lightBox: return 0;
        case NegativeViewing::scanner: return 1;
    }
    return 0;
}

const std::vector<NegativeViewing>& allNegativeViewings() {
    static const std::vector<NegativeViewing> all = {NegativeViewing::lightBox, NegativeViewing::scanner};
    return all;
}

FilmSpectralProfile::FilmSpectralProfile(std::vector<std::vector<float>> layerSensitivity_,
                                         std::vector<std::vector<float>> imageDyeDensity_,
                                         std::optional<std::vector<float>> minimumDensity_) {
    int layers = static_cast<int>(layerSensitivity_.size());
    if (!(FilmStockLimits::supportedCaptureLayerMin <= layers &&
          layers <= FilmStockLimits::supportedCaptureLayerMax)) {
        throw std::runtime_error("precondition failed: " + std::to_string(layers) +
                                 " capture layers; this build renders 3...3");
    }
    preconditionCheck(imageDyeDensity_.size() == static_cast<size_t>(FilmStockLimits::dyeCount),
                      "imageDyeDensity.count == FilmStock.dyeCount");
    for (const auto& row : layerSensitivity_) {
        preconditionCheck(row.size() == static_cast<size_t>(SpectralGrid::count),
                          "layerSensitivity rows on SpectralGrid");
    }
    for (const auto& row : imageDyeDensity_) {
        preconditionCheck(row.size() == static_cast<size_t>(SpectralGrid::count),
                          "imageDyeDensity rows on SpectralGrid");
    }
    if (minimumDensity_) {
        preconditionCheck(minimumDensity_->size() == static_cast<size_t>(SpectralGrid::count),
                          "minimumDensity on SpectralGrid");
        for (float v : *minimumDensity_) {
            preconditionCheck(std::isfinite(v) && v >= 0, "minimumDensity finite and non-negative");
        }
    }
    layerSensitivity = std::move(layerSensitivity_);
    imageDyeDensity = std::move(imageDyeDensity_);
    minimumDensity = std::move(minimumDensity_);
}

FilmSpectralProfile FilmSpectralProfile::floored(float share) const {
    if (!(share > 0)) return *this;
    FilmSpectralProfile profile = *this;
    for (size_t l = 0; l < profile.layerSensitivity.size(); ++l) {
        const std::vector<float>& layer = layerSensitivity[l];
        float floor = share * maxOr(layer, 0);
        std::vector<float> out(layer.size());
        for (size_t i = 0; i < layer.size(); ++i) out[i] = swiftMax(layer[i], floor);
        profile.layerSensitivity[l] = std::move(out);
    }
    return profile;
}

FilmSpectralProfile FilmSpectralProfile::measured(const std::vector<std::vector<float>>& layerSensitivity,
                                                  FilmDyeFamily dyeFamily) {
    int layers = static_cast<int>(layerSensitivity.size());
    preconditionCheck(FilmStockLimits::supportedCaptureLayerMin <= layers &&
                          layers <= FilmStockLimits::supportedCaptureLayerMax,
                      "supported capture layer count");
    for (const auto& row : layerSensitivity) {
        preconditionCheck(row.size() == static_cast<size_t>(SpectralGrid::count), "layer on SpectralGrid");
    }
    std::vector<std::vector<float>> continued;
    continued.reserve(layerSensitivity.size());
    for (const auto& layer : layerSensitivity) continued.push_back(SpectralGrid::continuedTails(layer));
    return FilmSpectralProfile(SpectralGrid::normalizeSensitivities(continued), SpectralGrid::dyes(dyeFamily));
}

FilmSpectralProfile FilmSpectralProfile::color(const std::vector<float>& peaksNM,
                                               const std::vector<float>& widthsNM,
                                               FilmDyeFamily dyeFamily) {
    preconditionCheck(peaksNM.size() == 3 && widthsNM.size() == 3, "three peaks and widths");
    std::vector<std::vector<float>> layers;
    for (int layer = 0; layer < 3; ++layer) {
        Sigmas sigmas = colorLayerSigmas(layer, widthsNM[layer]);
        layers.push_back(SpectralGrid::asymmetricGaussian(peaksNM[layer], sigmas.left, sigmas.right));
    }
    return FilmSpectralProfile(SpectralGrid::normalizeSensitivities(layers), SpectralGrid::dyes(dyeFamily));
}

FilmSpectralProfile::Sigmas FilmSpectralProfile::colorLayerSigmas(int layer, float widthNM) {
    return Sigmas{widthNM * (layer == 0 ? 0.54f : 0.62f),
                  widthNM * (layer == 0 ? 0.32f : (layer == 2 ? 0.62f : 0.78f))};
}

float FilmSpectralProfile::colorLayerSensitivity(int layer, float peakNM, float widthNM, float wavelength) {
    Sigmas sigmas = colorLayerSigmas(layer, widthNM);
    float sigma = wavelength < peakNM ? sigmas.left : sigmas.right;
    float x = (wavelength - peakNM) / swiftMax(sigma, 1.0f);
    return std::exp(-0.5f * x * x);
}

const std::vector<float>& FilmSpectralProfile::monochromeBandCentresNM() {
    static const std::vector<float> centres = {610, 545, 445};
    return centres;
}

const std::vector<float>& FilmSpectralProfile::monochromeBandWidthsNM() {
    static const std::vector<float> widths = {68, 62, 58};
    return widths;
}

FilmSpectralProfile FilmSpectralProfile::monochrome(const std::vector<float>& rgbWeights) {
    preconditionCheck(rgbWeights.size() == 3, "three rgbWeights");
    const auto& centers = monochromeBandCentresNM();
    const auto& widths = monochromeBandWidthsNM();
    std::vector<float> response(SpectralGrid::count, 0.0f);
    for (int component = 0; component < 3; ++component) {
        std::vector<float> band =
            SpectralGrid::asymmetricGaussian(centers[component], widths[component], widths[component]);
        for (size_t i = 0; i < response.size(); ++i) response[i] += rgbWeights[component] * band[i];
    }
    auto normalized = SpectralGrid::normalizeSensitivities({response, response, response});
    return FilmSpectralProfile(normalized, SpectralGrid::dyes(FilmDyeFamily::monochrome));
}

FilmSpectralProfile FilmSpectralProfile::approximation(const std::vector<std::vector<float>>& sensitivity,
                                                       bool monochrome, bool reversal) {
    if (monochrome) {
        return FilmSpectralProfile::monochrome(sensitivity[0]);
    }
    return FilmSpectralProfile::color({650, 550, 450},
                                      reversal ? FilmDyeFamily::kodachrome : FilmDyeFamily::kodakNegative);
}

namespace {
inline uint64_t bits(float v) {
    uint32_t b;
    std::memcpy(&b, &v, sizeof b);
    return static_cast<uint64_t>(b);
}
}  // namespace

uint64_t FilmSpectralProfile::signature() const {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const auto& row : layerSensitivity) {
        for (float v : row) h = (h ^ bits(v)) * 0x100000001b3ull;
    }
    for (const auto& row : imageDyeDensity) {
        for (float v : row) h = (h ^ bits(v)) * 0x100000001b3ull;
    }
    if (minimumDensity) {
        h = (h ^ 0x62617365ull) * 0x100000001b3ull;
        for (float v : *minimumDensity) h = (h ^ bits(v)) * 0x100000001b3ull;
    }
    return h;
}

}  // namespace fotufilm
