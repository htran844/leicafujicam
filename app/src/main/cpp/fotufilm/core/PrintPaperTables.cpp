// Port of Sources/FotufilmCore/PrintPaperTables.swift.
#include "PrintPaperTables.h"

#include <cmath>

#include "FilmStock.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {

const std::vector<std::vector<float>>& dyes(PrintPaper paper) {
    const PrintStock* stock = printStock(paper);
    return stock ? stock->spectra.dyes : SpectralGrid::paperDyes();
}

const std::vector<std::vector<float>>& analyticalDyes(PrintPaper paper) {
    const PrintStock* stock = printStock(paper);
    return stock ? stock->spectra.analyticalDyes : SpectralGrid::paperDyeAmounts();
}

std::vector<std::vector<float>> sensitivity(PrintPaper paper) { return sensitivity(paper, ReceiverBands::paper()); }

std::vector<std::vector<float>> sensitivity(PrintPaper paper, const ReceiverBands& bands) {
    switch (route(paper)) {
        case PrintRoute::print: return printStock(paper)->spectra.sensitivity;
        case PrintRoute::screen: return bands.sensitivity();
        case PrintRoute::negative: return SpectralGrid::paperSensitivity();
        case PrintRoute::labScan: return SpectralGrid::labScanSensitivity();
        case PrintRoute::telecine: return SpectralGrid::telecineSensitivity();
    }
    return SpectralGrid::paperSensitivity();
}

std::optional<Float3> ladStatusA(PrintPaper paper) {
    const PrintStock* stock = printStock(paper);
    if (!stock || !stock->releaseSetup) return std::nullopt;
    return stock->releaseSetup->ladStatusA;
}

std::vector<float> sensitivityReferenceExposures(PrintPaper paper, const FilmStock& stock) {
    preconditionCheck(isProjected(paper), "sensitivity references belong to release print film");
    const PrintStock* print = printStock(paper);
    bool aboveMinimum = print && print->releaseSetup && print->releaseSetup->sensitivityAboveMinimum;
    std::vector<float> result;
    for (const CharacteristicCurve& curve : printCurves(paper, stock)) {
        result.push_back(curve.logExposure(aboveMinimum ? curve.dMin + 1 : 1));
    }
    return result;
}

std::vector<float> printExposureMidpoints(PrintPaper paper, const FilmStock& stock,
                                          DigitalReferenceStyle digitalReference) {
    std::vector<CharacteristicCurve> curves = printCurves(paper, stock, digitalReference);
    std::optional<Float3> aim = ladStatusA(paper);
    std::vector<float> result(3);
    if (aim && !stock.isMonochrome && !stock.isReversal) {
        for (int c = 0; c < 3; ++c) result[c] = curves[c].logExposure((*aim)[c]);
        return result;
    }
    Float3 offset = acceptsViewingIlluminant(paper) && !stock.isMonochrome
                        ? SpectralRuntime::reflectionPrintDensityTrim(paper)
                        : Float3();
    for (int c = 0; c < 3; ++c) {
        result[c] = curves[c].logExposure(curves[c].dMin + anchorDensity(paper) + offset[c]);
    }
    return result;
}

Float3 printingAim(PrintPaper paper, const FilmStock& stock) {
    std::vector<float> midpoints = printExposureMidpoints(paper, stock);
    std::vector<float> reference = sensitivityReferenceExposures(paper, stock);
    Float3 aim;
    for (int c = 0; c < 3; ++c) aim[c] = std::pow(10.0f, midpoints[c] - reference[c]);
    return aim;
}

CharacteristicCurve printCurve(PrintPaper paper, const FilmStock& stock, DigitalReferenceStyle digitalReference) {
    return printCurves(paper, stock, digitalReference)[1];
}

namespace {
std::vector<CharacteristicCurve> colourRecords(PrintPaper paper, const FilmStock& stock,
                                               DigitalReferenceStyle digitalReference) {
    switch (route(paper)) {
        case PrintRoute::print: return printStock(paper)->curves;
        case PrintRoute::screen: {
            CharacteristicCurve curve = DigitalReferenceReceiver::curve(digitalReference, stock);
            return {curve, curve, curve};
        }
        case PrintRoute::negative: return {stock.paperCurve, stock.paperCurve, stock.paperCurve};
        case PrintRoute::labScan:
            return {PrintPaperTables::labScanCurve(), PrintPaperTables::labScanCurve(),
                    PrintPaperTables::labScanCurve()};
        case PrintRoute::telecine:
            return {PrintPaperTables::telecineCurve(), PrintPaperTables::telecineCurve(),
                    PrintPaperTables::telecineCurve()};
    }
    return {stock.paperCurve, stock.paperCurve, stock.paperCurve};
}
}  // namespace

std::vector<CharacteristicCurve> printCurves(PrintPaper paper, const FilmStock& stock,
                                             DigitalReferenceStyle digitalReference) {
    if (levelsPositive(paper, stock, digitalReference)) {
        const CharacteristicCurve& curve = DigitalReferenceReceiver::positiveCurve();
        return {curve, curve, curve};
    }
    if (viewsFilmDirectly(paper, stock)) return {stock.paperCurve, stock.paperCurve, stock.paperCurve};
    // A monochrome negative prints through one exposure: the timing record three times.
    if (stock.isMonochrome) {
        CharacteristicCurve timing = colourRecords(paper, stock, digitalReference)[1];
        return {timing, timing, timing};
    }
    return colourRecords(paper, stock, digitalReference);
}

namespace PrintPaperTables {
const CharacteristicCurve& labScanCurve() {
    static const CharacteristicCurve curve(0, 3, -1, 0.3f, 0.2f, 0.3f);
    return curve;
}
const CharacteristicCurve& telecineCurve() {
    static const CharacteristicCurve curve(0.051f, 5.7f, -1.541f, 0.16f, -1.182f, 0.12f);
    return curve;
}
}  // namespace PrintPaperTables

namespace SpectralGrid {

const std::vector<std::vector<float>>& labScanSensitivity() {
    static const std::vector<std::vector<float>> values = [] {
        const float centers[3] = {630, 545, 465};
        const float sigmas[3] = {8.5f, 14, 9.5f};
        std::vector<std::vector<float>> bands;
        for (int b = 0; b < 3; ++b) {
            std::vector<float> band;
            for (float w : wavelengths()) band.push_back(std::exp(-0.5f * std::pow((w - centers[b]) / sigmas[b], 2.0f)));
            bands.push_back(std::move(band));
        }
        return normalizeSensitivities(bands);
    }();
    return values;
}

const std::vector<std::vector<float>>& telecineSensitivity() {
    static const std::vector<std::vector<float>> values = [] {
        auto concat = [](size_t leading, std::vector<float> body, size_t trailing) {
            std::vector<float> row(leading, 0.0f);
            row.insert(row.end(), body.begin(), body.end());
            row.insert(row.end(), trailing, 0.0f);
            return row;
        };
        std::vector<std::vector<float>> bands = {
            // R: 610-720 nm
            concat(45,
                   {0.0005f, 0.0010f, 0.0094f, 0.0177f, 0.0545f, 0.0913f, 0.1568f, 0.2223f, 0.3570f,
                    0.4917f, 0.6592f, 0.8267f, 0.9133f, 1.0000f, 0.8969f, 0.7937f, 0.6212f, 0.4487f,
                    0.3197f, 0.1907f, 0.1330f, 0.0752f, 0.0425f, 0.0097f, 0.0049f},
                   11),
            // G: 470-590 nm
            concat(17,
                   {0.0003f, 0.0005f, 0.0008f, 0.0012f, 0.0030f, 0.0049f, 0.0208f, 0.0366f, 0.1540f,
                    0.2714f, 0.4933f, 0.7152f, 0.8576f, 1.0000f, 0.9389f, 0.8779f, 0.6682f, 0.4584f,
                    0.2950f, 0.1317f, 0.0698f, 0.0079f, 0.0052f, 0.0025f, 0.0019f, 0.0013f, 0.0006f},
                   37),
            // B: 380-490 nm
            concat(0,
                   {0.0052f, 0.0340f, 0.0628f, 0.1160f, 0.1692f, 0.2917f, 0.4141f, 0.5883f, 0.7625f,
                    0.8812f, 1.0000f, 0.9377f, 0.8754f, 0.7133f, 0.5512f, 0.4066f, 0.2619f, 0.1847f,
                    0.1075f, 0.0668f, 0.0262f, 0.0140f, 0.0018f, 0.0009f},
                   57),
        };
        for (const auto& row : bands) preconditionCheck(row.size() == static_cast<size_t>(count), "telecine band on grid");
        return normalizeSensitivities(bands);
    }();
    return values;
}

}  // namespace SpectralGrid

}  // namespace fotufilm
