// Port of Sources/FotufilmCore/Enlarger.swift.
#include "Enlarger.h"

#include "FilmStock.h"

namespace fotufilm {

const std::vector<Enlarger>& allEnlargers() {
    static const std::vector<Enlarger> all = {Enlarger::diffuser, Enlarger::condenser};
    return all;
}

const char* rawValue(Enlarger enlarger) {
    switch (enlarger) {
        case Enlarger::diffuser: return "diffuser";
        case Enlarger::condenser: return "condenser";
    }
    return "";
}

std::optional<Enlarger> enlargerFromRawValue(const std::string& raw) {
    for (Enlarger enlarger : allEnlargers()) {
        if (raw == rawValue(enlarger)) return enlarger;
    }
    return std::nullopt;
}

const char* name(Enlarger enlarger) {
    switch (enlarger) {
        case Enlarger::diffuser: return "Diffuser";
        case Enlarger::condenser: return "Condenser";
    }
    return "";
}

const char* detail(Enlarger enlarger) {
    switch (enlarger) {
        case Enlarger::diffuser: return "Use soft, even light, as in a color enlarger or minilab.";
        case Enlarger::condenser:
            return "Use focused light for stronger contrast and more visible grain and dust in black-and-white prints.";
    }
    return "";
}

namespace EnlargerOptics {
bool illuminates(const FilmStock& stock, PrintPaper paper) {
    return !viewsFilmDirectly(paper, stock) && !stock.isReflectionPrint && resolved(paper, stock) == paper &&
           !isNegative(paper) && !isScan(paper) && paper != PrintPaper::screen && !isProjected(paper);
}
}  // namespace EnlargerOptics

float callierCoefficient(Enlarger enlarger, const FilmStock& stock, PrintPaper paper) {
    if (!(enlarger == Enlarger::condenser && EnlargerOptics::illuminates(stock, paper))) return 1;
    return stock.isMonochrome ? EnlargerOptics::silverCallierCoefficient : EnlargerOptics::dyeCallierCoefficient;
}

}  // namespace fotufilm
