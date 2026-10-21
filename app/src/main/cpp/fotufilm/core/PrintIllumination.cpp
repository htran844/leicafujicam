// Port of Sources/FotufilmCore/PrintIllumination.swift (SpectralRuntime and SpectralGrid
// extensions, declared in SpectralModel.h and SpectralGrid.h).
#include "PrintPaperTables.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace SpectralRuntime {

PrintingIllumination printingIllumination(const FilmStock& stock, PrintPaper paper, const std::vector<float>& density,
                                          const std::vector<std::vector<float>>& dyes, float neutralDensity,
                                          float densityScale, const ReceiverBands& receiverBands,
                                          const std::optional<PrinterLights>& lights) {
    std::optional<std::vector<float>> offset = filmDensityOffset(stock, densityScale);
    const std::vector<float>* offsetPointer = offset ? &*offset : nullptr;
    if (!isProjected(paper)) {
        const std::vector<float>& lamp = paper == PrintPaper::screen
                                             ? DigitalReferenceReceiver::illuminant()
                                             : (isScan(paper) ? SpectralGrid::equalEnergy() : SpectralGrid::enlarger3200K());
        // Digital Reference balances a single negative exposure, then uses the same lamp and
        // receiver at every density.
        return {lamp, paperExposure(density, dyes, lamp, sensitivity(paper, receiverBands), neutralDensity, offsetPointer)};
    }
    Float3 aim = printingAim(paper, stock);
    std::vector<float> raw =
        releasePrinterLamp(density, dyes, sensitivity(paper), aim, neutralDensity, offsetPointer);
    float peak = swiftMax(maxOr(raw, 0), 1e-12f);
    std::optional<PrinterLights> resolvedLights = PrinterLights::resolved(lights, stock, paper);
    if (!resolvedLights) {
        std::vector<float> lamp(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) lamp[i] = raw[i] / peak;
        return {lamp, aim / peak};
    }
    std::vector<float> timed = releasePrinterLamp(density, dyes, sensitivity(paper), aim, neutralDensity,
                                                  offsetPointer, resolvedLights->beamGain());
    std::vector<float> lamp(timed.size());
    for (size_t i = 0; i < timed.size(); ++i) lamp[i] = timed[i] / peak;
    return {lamp, aim / peak};
}

std::vector<float> releasePrinterLamp(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                                      const std::vector<std::vector<float>>& sensitivity, Float3 target,
                                      float neutralDensity, const std::vector<float>* densityOffset,
                                      Float3 beamGain) {
    const auto& beams = SpectralGrid::releasePrinterBeams();
    Float3 response[3];
    for (int b = 0; b < 3; ++b) {
        response[b] = paperExposure(density, dyes, beams[b], sensitivity, neutralDensity, densityOffset);
    }
    // Normalize columns before solving so a dense orange mask does not make the blue light
    // numerically insignificant; undone in the beam weights below.
    float norms[3];
    Float3 columns[3];
    for (int b = 0; b < 3; ++b) {
        norms[b] = swiftMax(swiftMax(response[b].x, swiftMax(response[b].y, response[b].z)), 1e-12f);
        columns[b] = response[b] / norms[b];
    }
    Float3 lights = Float3::repeating(1);
    auto dot = [](Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    // Non-negative least squares by three-variable coordinate descent.
    Float3 residual = columns[0] + columns[1] + columns[2] - target;
    for (int iteration = 0; iteration < 64; ++iteration) {
        for (int channel = 0; channel < 3; ++channel) {
            Float3 column = columns[channel];
            float next = swiftMax(0.0f, lights[channel] - dot(column, residual) / swiftMax(dot(column, column), 1e-12f));
            residual += column * (next - lights[channel]);
            lights[channel] = next;
        }
    }
    Float3 weights = Float3(lights.x / norms[0], lights.y / norms[1], lights.z / norms[2]) * beamGain;
    std::vector<float> lamp(SpectralGrid::count);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        lamp[i] = beams[0][i] * weights.x + beams[1][i] * weights.y + beams[2][i] * weights.z;
    }
    return lamp;
}

}  // namespace SpectralRuntime

namespace SpectralGrid {

const std::vector<std::vector<float>>& releasePrinterBeams() {
    static const std::vector<std::vector<float>> beams = [] {
        // (low, rise, fall, high): broad tapered passbands, an approximation rather than measured
        // 2043, Series 1700 or SC-41 spectra.
        const float edges[3][4] = {{590, 610, 700, 730}, {490, 510, 580, 610}, {400, 410, 480, 500}};
        std::vector<std::vector<float>> result;
        for (const auto& e : edges) {
            float low = e[0], rise = e[1], fall = e[2], high = e[3];
            std::vector<float> beam(count);
            for (int i = 0; i < count; ++i) {
                float wavelength = wavelengths()[i];
                float transmission = swiftMax(
                    0.0f, swiftMin(1.0f, swiftMin((wavelength - low) / (rise - low), (high - wavelength) / (high - fall))));
                beam[i] = enlarger3200K()[i] * transmission;
            }
            result.push_back(std::move(beam));
        }
        return result;
    }();
    return beams;
}

}  // namespace SpectralGrid
}  // namespace fotufilm
