// Port of Sources/FotufilmCore/PrintStock.swift. The shipped stocks (PrintStockCatalogue.swift)
// are declared here as static accessors and defined in PrintStockCatalogue.cpp.
#ifndef FOTUFILM_CORE_PRINT_STOCK_H
#define FOTUFILM_CORE_PRINT_STOCK_H

#include <optional>
#include <string>
#include <vector>

#include "CharacteristicCurve.h"
#include "Simd.h"

namespace fotufilm {

/// `PrintStock.Process`: the family of material a stock belongs to.
enum class PrintProcess : int {
    /// Negative-working colour paper (RA-4), viewed by reflection.
    photoPaper,
    /// Negative-working colour print film (ECP-2), projected and timed to Status A LAD aims.
    releasePrintFilm,
    /// Positive-working paper for slide film, viewed by reflection.
    positivePaper,
};

const std::vector<PrintProcess>& allPrintProcesses();
inline bool isProjected(PrintProcess p) { return p == PrintProcess::releasePrintFilm; }
inline bool isReflection(PrintProcess p) { return !isProjected(p); }
inline bool isPositiveWorking(PrintProcess p) { return p == PrintProcess::positivePaper; }
inline bool printsReversal(PrintProcess p) { return isPositiveWorking(p); }
/// Combined lens, focus and scattering blur of the printing stage, Gaussian sigma in mm on film.
inline float enlargerBlurMM(PrintProcess p) { return isProjected(p) ? 0.003f : 0.004f; }
/// Nominal visual mid-grey, as density above clear base.
inline float midDensity(PrintProcess p) { return isProjected(p) ? 1.0f : 0.744f; }
/// Whether layer sensitivities keep the datasheet's inter-layer speed scale.
inline bool keepsLayerSpeedScale(PrintProcess p) { return isProjected(p); }

/// A physical print material: the emulsion the developed film is exposed onto.
struct PrintStock {
    using Process = PrintProcess;

    /// The published setup of a release print: what the timing solve aims for.
    struct ReleaseSetup {
        /// Gross Status A setup aims, not densities above clear film.
        Float3 ladStatusA;
        /// Whether the sheet's spectral sensitivity was measured at 1.0 above minimum density.
        bool sensitivityAboveMinimum;
    };

    /// The material's dye and receiver spectra on the engine's wavelength grid.
    struct Spectra {
        /// The sheet's dye shapes at their neutral-forming amounts.
        std::vector<std::vector<float>> analyticalDyes;
        /// `analyticalDyes` divided by their pointwise sum.
        std::vector<std::vector<float>> dyes;
        /// Layer sensitivities with publication tails extended.
        std::vector<std::vector<float>> sensitivity;

        Spectra(const std::vector<std::vector<float>>& dyeDensity,
                const std::optional<std::vector<float>>& neutralAmounts,
                const std::vector<std::vector<float>>& layerSensitivity, Process process);
    };

    std::string name;
    std::string detail;
    Process process;
    std::vector<CharacteristicCurve> curves;
    Spectra spectra;
    /// Required for, and only for, release print film.
    std::optional<ReleaseSetup> releaseSetup;

    PrintStock(std::string name, std::string detail, Process process, std::vector<CharacteristicCurve> curves,
               Spectra spectra, std::optional<ReleaseSetup> releaseSetup = std::nullopt);

    // PrintStockCatalogue.swift
    static const PrintStock& ektacolorEdge();
    static const PrintStock& enduraPremier();
    static const PrintStock& crystalArchive();
    static const PrintStock& vision2383();
    static const PrintStock& vision2393();
    static const PrintStock& eternaCP();
    static const PrintStock& exr5386();
    static const PrintStock& agfaCP30();
    static const PrintStock& eternaCP3521XD();
    static const PrintStock& ilfochromeCPS1K();
    static const PrintStock& ilfochromeCLM1K();
};

// PrintStockCatalogue.swift
namespace EktacolorEdgeCurves {
const CharacteristicCurve& green();
const CharacteristicCurve& red();
const CharacteristicCurve& blue();
}  // namespace EktacolorEdgeCurves

namespace IlfochromeCurves {
const CharacteristicCurve& normal();
const CharacteristicCurve& medium();
}  // namespace IlfochromeCurves

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINT_STOCK_H
