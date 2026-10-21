// Port of Sources/FotufilmCore/PrintPaper.swift. Swift members are free functions taking the
// paper first. The measurement tables (PrintPaperTables.swift) are in PrintPaperTables.h.
#ifndef FOTUFILM_CORE_PRINT_PAPER_H
#define FOTUFILM_CORE_PRINT_PAPER_H

#include <optional>
#include <string>
#include <vector>

#include "PrintStock.h"

namespace fotufilm {

struct FilmStock;
struct FilmFormat;
enum class DigitalReferenceStyle : int;

/// The medium that turns developed film into a finished image, including the film itself. The
/// raw value and case order are what hosts persist.
enum class PrintPaper {
    ektacolorEdge,
    enduraPremier,
    crystalArchive,
    vision2383,
    vision2393,
    eternaCP,
    labScan,
    telecine,
    screen,
    negative,
    ilfochromeCPS1K,
    ilfochromeCLM1K,
    exr5386,
    agfaCP30,
    eternaCP3521XD,
};

/// Every case in declaration order (`allCases`).
const std::vector<PrintPaper>& allPrintPapers();

const char* rawValue(PrintPaper paper);
/// `PrintPaper(rawValue:)`.
std::optional<PrintPaper> printPaperFromRawValue(const std::string& raw);
inline std::string id(PrintPaper paper) { return rawValue(paper); }
/// Raw values plus the historical Ilfochrome/Cibachrome aliases.
std::optional<PrintPaper> printPaperPreset(const std::string& id);

/// Default for engine and headless callers.
constexpr PrintPaper kDefaultPrintPaper = PrintPaper::ektacolorEdge;
/// The default output for editor edits and the direct-positive destination for reversal film.
constexpr PrintPaper kEditorDefaultPrintPaper = PrintPaper::screen;

/// Whether output on this medium can hold light above display white.
inline bool showsHDR(PrintPaper paper) { return paper == PrintPaper::screen; }
/// Whether the finished output is the developed negative itself.
inline bool isNegative(PrintPaper paper) { return paper == PrintPaper::negative; }
/// Whether this finished positive is a scanner's file rather than a physical sheet.
inline bool isScan(PrintPaper paper) { return paper == PrintPaper::labScan || paper == PrintPaper::telecine; }
/// Whether this output path's colour is profiled once, on a reference negative.
inline bool isReferenceAnchored(PrintPaper paper) { return paper == PrintPaper::labScan; }
/// Whether the scan leaves the machine as a Rec.709 video signal.
inline bool deliversRec709(PrintPaper paper) { return paper == PrintPaper::telecine; }
/// Fraction of pre-blur detail restored by the output medium.
float scanSharpening(PrintPaper paper);

/// PrintPaperTables.swift: log10(red/green, blue/green) printing balance the lab-scan profile
/// was solved at.
const std::vector<float>& labScanReferenceBalance();

/// How an output forms its image: on a print stock, or by one of the paths with no print material.
enum class PrintRoute { print, labScan, telecine, screen, negative };
PrintRoute route(PrintPaper paper);
/// The physical material this output prints onto, or nullptr for scans and viewing modes.
const PrintStock* printStock(PrintPaper paper);
/// The material family, or nullopt where nothing is printed.
std::optional<PrintProcess> printProcess(PrintPaper paper);
/// Every output that prints onto a material of `process`, in case order.
std::vector<PrintPaper> media(PrintProcess printedBy);

std::string name(PrintPaper paper);
std::string detail(PrintPaper paper);

/// Whether this stock may use the medium's range above display white.
bool supportsHDRDelivery(PrintPaper paper, const FilmStock& stock);
/// Positive-to-positive paper loses density as exposure increases.
bool isPositivePaper(PrintPaper paper);
/// Direction of the paper's exposure axis, carried by the shared print configuration.
inline float exposureDirection(PrintPaper paper) { return isPositivePaper(paper) ? -1.0f : 1.0f; }
/// Whether developed film is viewed directly, without exposing another emulsion.
bool viewsFilmDirectly(PrintPaper paper, const FilmStock& stock);
/// Whether a transparent positive is delivered through the paper stage (Digital Reference's
/// graded styles).
bool levelsPositive(PrintPaper paper, const FilmStock& stock, DigitalReferenceStyle digitalReference);
/// Transparent positives print onto positive paper. Integral instant sheets are already prints.
std::vector<PrintPaper> choices(const FilmStock& stock);
/// The medium a stock actually reaches when the edit asks for this one.
PrintPaper resolved(PrintPaper paper, const FilmStock& stock);
/// `PrintPaper.default(for:)`: the stock's native medium, else the measured sheet.
PrintPaper defaultPrintPaper(const FilmStock& stock);
/// Preview media for the selected gauge, with an explicitly named native medium first.
std::vector<PrintPaper> stripChoices(const FilmStock& stock, const FilmFormat& gauge);
/// Whether this finished positive is projected rather than held.
bool isProjected(PrintPaper paper);
/// Whether a caller can replace the medium's reference viewing illuminant.
inline bool acceptsViewingIlluminant(PrintPaper paper) { return printStock(paper) != nullptr; }
/// Combined lens, focus, receiver, and scattering blur as Gaussian sigma in mm on film.
float enlargerBlurMM(PrintPaper paper);
/// Monochrome on Digital Reference keeps its established direct read.
bool readsLayersDirectly(PrintPaper paper, const FilmStock& stock);
/// Whether timing can balance the film records while forming this output:
/// `self != .screen && !isNegative && !isPositivePaper`.
bool acceptsPrintCorrection(PrintPaper paper);
/// Nominal visual mid-grey, relative to clear white.
float midDensity(PrintPaper paper);
/// Nominal density above base that reads at `midDensity`.
inline float anchorDensity(PrintPaper paper) { return midDensity(paper); }

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINT_PAPER_H
