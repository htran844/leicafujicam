// Port of Sources/FotufilmCore/PrintStockCatalogue.swift. Declarations live in PrintStock.h.
#include <cmath>

#include "AgfaCP30PrintSpectra.h"
#include "CrystalArchivePaperSpectra.h"
#include "EXR5386PrintSpectra.h"
#include "EnduraPremierPaperSpectra.h"
#include "EternaCP3521XDPrintSpectra.h"
#include "EternaCPPrintSpectra.h"
#include "PrintPaperSpectra.h"
#include "PrintStock.h"
#include "Vision2383PrintSpectra.h"
#include "Vision2393PrintSpectra.h"

namespace fotufilm {

const PrintStock& PrintStock::ektacolorEdge() {
    static const PrintStock stock(
        "Kodak Ektacolor Edge Paper", "Kodak photo paper with a slightly warm color balance.",
        Process::photoPaper,
        {EktacolorEdgeCurves::red(), EktacolorEdgeCurves::green(), EktacolorEdgeCurves::blue()},
        Spectra(PrintPaperSpectra::dyeDensity(), PrintPaperSpectra::neutralAmounts(),
                PrintPaperSpectra::layerSensitivity(), Process::photoPaper));
    return stock;
}

const PrintStock& PrintStock::enduraPremier() {
    static const PrintStock stock(
        "Kodak Professional Endura Premier Paper", "Kodak photo paper with saturated colors and deep blacks.",
        Process::photoPaper,
        {EnduraPremierPaperSpectra::redCurve(), EnduraPremierPaperSpectra::greenCurve(),
         EnduraPremierPaperSpectra::blueCurve()},
        Spectra(EnduraPremierPaperSpectra::dyeDensity(), EnduraPremierPaperSpectra::neutralAmounts(),
                EnduraPremierPaperSpectra::layerSensitivity(), Process::photoPaper));
    return stock;
}

const PrintStock& PrintStock::crystalArchive() {
    // AF3-0250U2 provides no characteristic curve: all three records use the Ektacolor Edge green.
    static const PrintStock stock(
        "Fujicolor Crystal Archive Paper Type CA",
        "Fujifilm photo paper with a cooler color balance than Ektacolor Edge.", Process::photoPaper,
        {EktacolorEdgeCurves::green(), EktacolorEdgeCurves::green(), EktacolorEdgeCurves::green()},
        Spectra(CrystalArchivePaperSpectra::dyeDensity(), CrystalArchivePaperSpectra::neutralAmounts(),
                CrystalArchivePaperSpectra::layerSensitivity(), Process::photoPaper));
    return stock;
}

const PrintStock& PrintStock::vision2383() {
    static const PrintStock stock(
        "Kodak Vision Color Print Film 2383", "Kodak cinema print film with stronger contrast than photo paper.",
        Process::releasePrintFilm,
        {Vision2383PrintSpectra::redCurve(), Vision2383PrintSpectra::greenCurve(),
         Vision2383PrintSpectra::blueCurve()},
        Spectra(Vision2383PrintSpectra::dyeDensity(), std::nullopt, Vision2383PrintSpectra::layerSensitivity(),
                Process::releasePrintFilm),
        ReleaseSetup{Float3(1.09f, 1.06f, 1.03f), false});
    return stock;
}

const PrintStock& PrintStock::vision2393() {
    static const PrintStock stock(
        "Kodak Vision Premier Color Print Film 2393",
        "Kodak cinema print film with deeper blacks and stronger color than 2383, but less shadow detail.",
        Process::releasePrintFilm,
        {Vision2393PrintSpectra::redCurve(), Vision2393PrintSpectra::greenCurve(),
         Vision2393PrintSpectra::blueCurve()},
        Spectra(Vision2393PrintSpectra::dyeDensity(), std::nullopt, Vision2393PrintSpectra::layerSensitivity(),
                Process::releasePrintFilm),
        ReleaseSetup{Float3(1.09f, 1.06f, 1.03f), false});
    return stock;
}

const PrintStock& PrintStock::eternaCP() {
    static const PrintStock stock(
        "Fujifilm ETERNA-CP Color Positive Film 3513DI",
        "Fujifilm cinema print film with a cooler color balance and softer contrast than Kodak print films.",
        Process::releasePrintFilm,
        {EternaCPPrintSpectra::cyanCurve(), EternaCPPrintSpectra::magentaCurve(),
         EternaCPPrintSpectra::yellowCurve()},
        Spectra(EternaCPPrintSpectra::dyeDensity(), std::nullopt, EternaCPPrintSpectra::layerSensitivity(),
                Process::releasePrintFilm),
        ReleaseSetup{Float3(1.10f, 1.05f, 1.05f), true});
    return stock;
}

const PrintStock& PrintStock::exr5386() {
    static const PrintStock stock(
        "Kodak EXR Color Print Film 5386",
        "Kodak's 1990s cinema print film, before Vision, with lighter blacks than 2383.",
        Process::releasePrintFilm,
        {EXR5386PrintSpectra::redCurve(), EXR5386PrintSpectra::greenCurve(), EXR5386PrintSpectra::blueCurve()},
        Spectra(EXR5386PrintSpectra::dyeDensity(), std::nullopt, EXR5386PrintSpectra::layerSensitivity(),
                Process::releasePrintFilm),
        ReleaseSetup{Float3(1.10f, 1.06f, 1.03f), false});
    return stock;
}

const PrintStock& PrintStock::agfaCP30() {
    static const PrintStock stock(
        "Agfa CP30 Colour Print Film", "Agfa cinema print film with deep, neutral blacks.",
        Process::releasePrintFilm,
        {AgfaCP30PrintSpectra::redCurve(), AgfaCP30PrintSpectra::greenCurve(), AgfaCP30PrintSpectra::blueCurve()},
        Spectra(AgfaCP30PrintSpectra::dyeDensity(), std::nullopt, AgfaCP30PrintSpectra::layerSensitivity(),
                Process::releasePrintFilm),
        ReleaseSetup{Float3(1.15f, 1.05f, 1.05f), false});
    return stock;
}

const PrintStock& PrintStock::eternaCP3521XD() {
    // Fujifilm publishes no spectra for 3521XD: it borrows 3513DI's.
    static const PrintStock stock(
        "Fujifilm ETERNA-CP Color Positive Film 3521XD",
        "Fujifilm cinema print film with deeper blacks and more shadow contrast than ETERNA-CP 3513DI.",
        Process::releasePrintFilm,
        {EternaCP3521XDPrintSpectra::redCurve(), EternaCP3521XDPrintSpectra::greenCurve(),
         EternaCP3521XDPrintSpectra::blueCurve()},
        PrintStock::eternaCP().spectra, ReleaseSetup{Float3(1.10f, 1.05f, 1.05f), true});
    return stock;
}

const PrintStock& PrintStock::ilfochromeCPS1K() {
    static const PrintStock stock(
        "Ilfochrome Classic CPS.1K (Cibachrome)",
        "Normal-contrast positive paper for slide film. Approximate color response; more paper exposure "
        "lightens the print.",
        Process::positivePaper, {IlfochromeCurves::normal(), IlfochromeCurves::normal(), IlfochromeCurves::normal()},
        PrintStock::ektacolorEdge().spectra);
    return stock;
}

const PrintStock& PrintStock::ilfochromeCLM1K() {
    static const PrintStock stock(
        "Ilfochrome Classic CLM.1K (Cibachrome)",
        "Medium-contrast positive paper for slide film, softer than CPS.1K. Approximate color response.",
        Process::positivePaper, {IlfochromeCurves::medium(), IlfochromeCurves::medium(), IlfochromeCurves::medium()},
        PrintStock::ektacolorEdge().spectra);
    return stock;
}

namespace EktacolorEdgeCurves {
const CharacteristicCurve& green() {
    static const CharacteristicCurve curve(0.094f, 8.5f, -1.493f, 0.154f, -1.234f, 0.184f);
    return curve;
}
const CharacteristicCurve& red() {
    static const CharacteristicCurve curve(0.089f, 9.5f, -1.480f, 0.142f, -1.253f, 0.153f);
    return curve;
}
const CharacteristicCurve& blue() {
    static const CharacteristicCurve curve(0.051f, 5.851f, -1.541f, 0.145f, -1.182f, 0.152f);
    return curve;
}
}  // namespace EktacolorEdgeCurves

namespace IlfochromeCurves {
namespace {
CharacteristicCurve curve(float range, float contrast) {
    // A symmetric soft toe/shoulder with width 10% of their separation; the asymptotic gamma is
    // corrected so the centre slope equals the published mid-tone value.
    float gamma = contrast / std::tanh(2.5f);
    float span = range / gamma;
    return CharacteristicCurve(0, gamma, -span / 2, span / 10, span / 2, span / 10);
}
}  // namespace
const CharacteristicCurve& normal() {
    static const CharacteristicCurve value = curve(2.0f, 1.40f);
    return value;
}
const CharacteristicCurve& medium() {
    static const CharacteristicCurve value = curve(2.05f, 1.15f);
    return value;
}
}  // namespace IlfochromeCurves

}  // namespace fotufilm
