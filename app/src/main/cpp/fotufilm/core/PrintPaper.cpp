// Port of Sources/FotufilmCore/PrintPaper.swift, and the PrintPaperTables.swift lab-scan
// reference balance.
#include "PrintPaper.h"

#include <algorithm>

#include "DigitalReferenceReceiver.h"
#include "FilmFormat.h"
#include "FilmStock.h"

namespace fotufilm {

const std::vector<PrintPaper>& allPrintPapers() {
    static const std::vector<PrintPaper> all = {
        PrintPaper::ektacolorEdge,  PrintPaper::enduraPremier,   PrintPaper::crystalArchive,
        PrintPaper::vision2383,     PrintPaper::vision2393,      PrintPaper::eternaCP,
        PrintPaper::labScan,        PrintPaper::telecine,        PrintPaper::screen,
        PrintPaper::negative,       PrintPaper::ilfochromeCPS1K, PrintPaper::ilfochromeCLM1K,
        PrintPaper::exr5386,        PrintPaper::agfaCP30,        PrintPaper::eternaCP3521XD,
    };
    return all;
}

const char* rawValue(PrintPaper paper) {
    switch (paper) {
        case PrintPaper::ektacolorEdge: return "ektacolor-edge";
        case PrintPaper::enduraPremier: return "endura-premier";
        case PrintPaper::crystalArchive: return "crystal-archive";
        case PrintPaper::vision2383: return "vision-2383";
        case PrintPaper::vision2393: return "vision-2393";
        case PrintPaper::eternaCP: return "eterna-cp";
        case PrintPaper::labScan: return "lab-scan";
        case PrintPaper::telecine: return "telecine";
        case PrintPaper::screen: return "screen";
        case PrintPaper::negative: return "negative";
        case PrintPaper::ilfochromeCPS1K: return "ilfochrome-cps-1k";
        case PrintPaper::ilfochromeCLM1K: return "ilfochrome-clm-1k";
        case PrintPaper::exr5386: return "exr-5386";
        case PrintPaper::agfaCP30: return "agfa-cp30";
        case PrintPaper::eternaCP3521XD: return "eterna-cp-3521xd";
    }
    return "";
}

std::optional<PrintPaper> printPaperFromRawValue(const std::string& raw) {
    for (PrintPaper paper : allPrintPapers()) {
        if (raw == rawValue(paper)) return paper;
    }
    return std::nullopt;
}

std::optional<PrintPaper> printPaperPreset(const std::string& id) {
    if (id == "ilfochrome" || id == "cibachrome" || id == "cibachrome-cps-1k") return PrintPaper::ilfochromeCPS1K;
    if (id == "cibachrome-clm-1k") return PrintPaper::ilfochromeCLM1K;
    return printPaperFromRawValue(id);
}

float scanSharpening(PrintPaper paper) {
    switch (paper) {
        case PrintPaper::labScan: return 0.65f;
        case PrintPaper::telecine: return 0.8f;
        default: return 0;
    }
}

const std::vector<float>& labScanReferenceBalance() {
    static const std::vector<float> balance = {1.0579212f, 1.0f, 0.880968f};
    return balance;
}

PrintRoute route(PrintPaper paper) {
    switch (paper) {
        case PrintPaper::labScan: return PrintRoute::labScan;
        case PrintPaper::telecine: return PrintRoute::telecine;
        case PrintPaper::screen: return PrintRoute::screen;
        case PrintPaper::negative: return PrintRoute::negative;
        default: return PrintRoute::print;
    }
}

const PrintStock* printStock(PrintPaper paper) {
    switch (paper) {
        case PrintPaper::ektacolorEdge: return &PrintStock::ektacolorEdge();
        case PrintPaper::enduraPremier: return &PrintStock::enduraPremier();
        case PrintPaper::crystalArchive: return &PrintStock::crystalArchive();
        case PrintPaper::vision2383: return &PrintStock::vision2383();
        case PrintPaper::vision2393: return &PrintStock::vision2393();
        case PrintPaper::eternaCP: return &PrintStock::eternaCP();
        case PrintPaper::ilfochromeCPS1K: return &PrintStock::ilfochromeCPS1K();
        case PrintPaper::ilfochromeCLM1K: return &PrintStock::ilfochromeCLM1K();
        case PrintPaper::exr5386: return &PrintStock::exr5386();
        case PrintPaper::agfaCP30: return &PrintStock::agfaCP30();
        case PrintPaper::eternaCP3521XD: return &PrintStock::eternaCP3521XD();
        case PrintPaper::labScan:
        case PrintPaper::telecine:
        case PrintPaper::screen:
        case PrintPaper::negative: return nullptr;
    }
    return nullptr;
}

std::optional<PrintProcess> printProcess(PrintPaper paper) {
    const PrintStock* stock = printStock(paper);
    if (!stock) return std::nullopt;
    return stock->process;
}

std::vector<PrintPaper> media(PrintProcess printedBy) {
    std::vector<PrintPaper> result;
    for (PrintPaper paper : allPrintPapers()) {
        if (printProcess(paper) == printedBy) result.push_back(paper);
    }
    return result;
}

std::string name(PrintPaper paper) {
    switch (route(paper)) {
        case PrintRoute::print: return printStock(paper)->name;
        case PrintRoute::labScan: return "Lab Scan";
        case PrintRoute::telecine: return "Telecine";
        case PrintRoute::screen: return "Digital Reference";
        case PrintRoute::negative: return "Negative";
    }
    return "";
}

std::string detail(PrintPaper paper) {
    switch (route(paper)) {
        case PrintRoute::print: return printStock(paper)->detail;
        case PrintRoute::labScan:
            return "A minilab scan: each frame set up and finished with a deeper black, cool shadows, teal foliage "
                   "and cyan-leaning blues. Choose TIFF for a 16-bit master.";
        case PrintRoute::telecine:
            return "A Rec.709 video transfer with timed color, raised blacks and a finished contrast curve.";
        case PrintRoute::screen:
            return "A fixed digital receiver preserves negative-film color and tone. Reversal films are viewed "
                   "directly and support HDR output.";
        case PrintRoute::negative:
            return "View the developed negative before printing or conversion to a positive.";
    }
    return "";
}

bool supportsHDRDelivery(PrintPaper paper, const FilmStock& stock) {
    return showsHDR(paper) && stock.isReversal && !stock.isReflectionPrint;
}

bool isPositivePaper(PrintPaper paper) {
    std::optional<PrintProcess> process = printProcess(paper);
    return process.has_value() && isPositiveWorking(*process);
}

bool viewsFilmDirectly(PrintPaper paper, const FilmStock& stock) { return stock.isReversal && !isPositivePaper(paper); }

bool levelsPositive(PrintPaper paper, const FilmStock& stock, DigitalReferenceStyle digitalReference) {
    return paper == PrintPaper::screen && stock.isReversal && !stock.isReflectionPrint &&
           usesGradedCurve(digitalReference);
}

std::vector<PrintPaper> choices(const FilmStock& stock) {
    if (stock.isReflectionPrint) return {PrintPaper::screen};
    if (stock.isReversal) {
        std::vector<PrintPaper> result = {PrintPaper::screen};
        for (PrintPaper paper : media(PrintProcess::positivePaper)) result.push_back(paper);
        return result;
    }
    std::vector<PrintPaper> result;
    for (PrintPaper paper : allPrintPapers()) {
        if (!isPositivePaper(paper)) result.push_back(paper);
    }
    return result;
}

PrintPaper resolved(PrintPaper paper, const FilmStock& stock) {
    std::vector<PrintPaper> available = choices(stock);
    return std::find(available.begin(), available.end(), paper) != available.end() ? paper : available[0];
}

PrintPaper defaultPrintPaper(const FilmStock& stock) {
    return resolved(stock.nativePrintMedium.value_or(kDefaultPrintPaper), stock);
}

std::vector<PrintPaper> stripChoices(const FilmStock& stock, const FilmFormat& gauge) {
    if (stock.isReflectionPrint) return {PrintPaper::screen};
    if (stock.isReversal) {
        std::vector<PrintPaper> result = media(PrintProcess::positivePaper);
        result.push_back(PrintPaper::screen);
        return result;
    }
    std::vector<PrintPaper> offered;
    if (gauge.isMotionPicture()) {
        offered = media(PrintProcess::releasePrintFilm);
        offered.push_back(PrintPaper::telecine);
    } else {
        offered = media(PrintProcess::photoPaper);
    }
    if (stock.nativePrintMedium) {
        auto index = std::find(offered.begin(), offered.end(), *stock.nativePrintMedium);
        if (index != offered.end()) {
            PrintPaper native = *index;
            offered.erase(index);
            offered.insert(offered.begin(), native);
        }
    }
    offered.push_back(PrintPaper::screen);
    return offered;
}

bool isProjected(PrintPaper paper) {
    std::optional<PrintProcess> process = printProcess(paper);
    return process.has_value() && isProjected(*process);
}

float enlargerBlurMM(PrintPaper paper) {
    switch (route(paper)) {
        case PrintRoute::print: return enlargerBlurMM(printStock(paper)->process);
        case PrintRoute::labScan: return 0.007f;
        case PrintRoute::telecine: return 0.0065f;
        case PrintRoute::screen:
        case PrintRoute::negative: return 0;
    }
    return 0;
}

bool readsLayersDirectly(PrintPaper paper, const FilmStock& stock) {
    return paper == PrintPaper::screen && stock.isMonochrome;
}

bool acceptsPrintCorrection(PrintPaper paper) {
    return paper != PrintPaper::screen && !isNegative(paper) && !isPositivePaper(paper);
}

float midDensity(PrintPaper paper) {
    std::optional<PrintProcess> process = printProcess(paper);
    return process ? midDensity(*process) : midDensity(PrintProcess::photoPaper);
}

}  // namespace fotufilm
