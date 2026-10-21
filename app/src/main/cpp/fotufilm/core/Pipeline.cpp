// Port of Sources/FotufilmCore/Pipeline.swift (FotufilmEngine.Options) and the Options extensions
// of UnexposedEdge.swift and HalationModel.swift.
#include "Pipeline.h"

#include <cmath>

#include "ColorScience.h"
#include "FilmStock.h"
#include "Illuminant.h"
#include "SpectralGrid.h"

namespace fotufilm {

using Options = FotufilmEngine::Options;

void Options::completeDeliveryMottle() {
    if (!grainMottleShare) return;
    grainMottleSizeRatio = grainMottleSizeRatio.value_or(deliveryMottleSizeRatio);
}

WhiteBalance Options::resolvedSceneIlluminant(float referenceKelvin) const {
    float base = sceneIlluminantKelvin.value_or(referenceKelvin);
    preconditionCheck(std::isfinite(base) && base > 0 && std::isfinite(whiteBalance.kelvin) && whiteBalance.kelvin > 0 &&
                          std::isfinite(whiteBalance.tint),
                      "invalid scene illuminant");
    float displacement = whiteBalance.mired() - WhiteBalance::neutral().mired();
    return WhiteBalance(WhiteBalance::miredToKelvin(WhiteBalance::kelvinToMired(base) + displacement),
                        whiteBalance.tint);
}

std::vector<float> Options::resolvedSceneSpectrum(float referenceKelvin) const {
    if (!sceneIlluminantSpectrum.empty()) {
        bool valid = static_cast<int>(sceneIlluminantSpectrum.size()) == SpectralGrid::count;
        for (float v : sceneIlluminantSpectrum) valid = valid && std::isfinite(v) && v >= 0;
        preconditionCheck(valid, "scene SPD must have 81 finite nonnegative samples");
        (void)Illuminant::luminance(sceneIlluminantSpectrum);
        return sceneIlluminantSpectrum;
    }
    WhiteBalance edited = resolvedSceneIlluminant(referenceKelvin);
    if (!sceneIlluminantChromaticity) return Illuminant::spectrum(edited);
    Float2 original = WhiteBalance::chromaticity(sceneIlluminantKelvin.value_or(referenceKelvin), 0);
    Float2 offset = WhiteBalance::uvFromXY(*sceneIlluminantChromaticity) - WhiteBalance::uvFromXY(original);
    Float2 target = WhiteBalance::uvFromXY(WhiteBalance::chromaticity(edited.kelvin, edited.tint)) + offset;
    return Illuminant::matching(WhiteBalance::xyFromUV(target), Illuminant::atLocus(edited.kelvin));
}

Float3 Options::sceneLightGains() const {
    if (sceneIlluminantSpectrum.empty() && !sceneIlluminantChromaticity && resolvedSceneIlluminant().isNeutral()) {
        return Float3(1, 1, 1);
    }
    Float3 source = WhiteBalance::workingRGB(Illuminant::chromaticity(resolvedSceneSpectrum()));
    Float3 reference = WhiteBalance::workingRGB(WhiteBalance::chromaticity(6504, 0));
    return source / reference;
}

PrintPaper Options::paperFor(const FilmStock& stock) const {
    return paper ? resolved(*paper, stock) : defaultPrintPaper(stock);
}

bool Options::supportsHDRDelivery(const FilmStock& stock) const {
    PrintPaper medium = paperFor(stock);
    return fotufilm::supportsHDRDelivery(medium, stock) && !levelsPositive(medium, stock, digitalReference);
}

float Options::sdrShoulderKnee(const FilmStock& stock) const {
    return FilmSDRDelivery::shoulderKnee(supportsHDRDelivery(stock));
}

std::optional<LayeredTransport> Options::transportConstruction(const FilmStock&) const { return std::nullopt; }

Options Options::withoutLayeredTransport() const {
    Options copy = *this;
    copy.halationModel = HalationModel::legacy;
    copy.layeredTransport = std::nullopt;
    return copy;
}

float Options::pixelsPerMM(int width, int height) const {
    float coverage = swiftMin(swiftMax(frameCoverage, 0.05f), 1.0f);
    return static_cast<float>(frameShortEdgePixels(width, height)) / (format.frameHeightMM * coverage);
}

double Options::pixelPitchMM(int width, int height) const {
    return static_cast<double>(format.frameHeightMM * swiftMin(swiftMax(frameCoverage, 0.05f), 1.0f)) /
           static_cast<double>(frameShortEdgePixels(width, height));
}

int Options::frameShortEdgePixels(int width, int height) const {
    if (!unexposedEdge) return std::min(width, height);
    const UnexposedEdge::Margins& margins = unexposedEdge->margins;
    return std::max(1, std::min(width - margins.left - margins.right, height - margins.top - margins.bottom));
}

std::vector<float> Options::gateConfiguration(int width, int height) const {
    if (!unexposedEdge) return {0, 0, 0, 0, -1};
    const UnexposedEdge::Margins& margins = unexposedEdge->margins;
    return {static_cast<float>(margins.left), static_cast<float>(margins.top),
            static_cast<float>(width - margins.right), static_cast<float>(height - margins.bottom),
            UnexposedEdge::gateRadiusMM(unexposedEdge->fNumber) * pixelsPerMM(width, height)};
}

std::vector<float> Options::carrierConfiguration(int width, int height) const {
    if (!unexposedEdge || !(unexposedEdge->margins.carrier > 0)) return {0, 0, 0, 0, 0, -1, 0, 1};
    const UnexposedEdge::Margins& margins = unexposedEdge->margins;
    float scale = pixelsPerMM(width, height);
    float reach = static_cast<float>(margins.carrier);
    float narrowest =
        static_cast<float>(std::min(std::min(margins.left, margins.right), std::min(margins.top, margins.bottom))) -
        reach;
    return {reach,
            reach,
            static_cast<float>(width) - reach,
            static_cast<float>(height) - reach,
            swiftMin(UnexposedEdge::carrierCornerRadiusMM * scale, swiftMax(narrowest, 0.0f)),
            UnexposedEdge::carrierShadowRadiusMM * scale,
            swiftMin(UnexposedEdge::carrierFilingMM * scale, swiftMax(narrowest, 0.0f) / 4),
            UnexposedEdge::carrierFilingCellMM * scale};
}

float Options::gateCornerConfiguration(int width, int height) const {
    if (!unexposedEdge) return 0;
    return swiftMin(UnexposedEdge::gateCornerRadiusMM * pixelsPerMM(width, height),
                    static_cast<float>(frameShortEdgePixels(width, height)) / 2);
}

}  // namespace fotufilm
