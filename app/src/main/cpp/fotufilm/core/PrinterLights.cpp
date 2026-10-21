// Port of Sources/FotufilmCore/PrinterLights.swift.
#include "PrinterLights.h"

#include <cmath>
#include <cstring>

#include "FilmStock.h"

namespace fotufilm {

namespace {
uint64_t bits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}
}  // namespace

PrinterLights PrinterLights::normalized() const {
    PrinterLights result = *this;
    for (int channel = 0; channel < 3; ++channel) {
        result[channel] = std::isfinite((*this)[channel])
                              ? swiftMin(swiftMax((*this)[channel], rangeLower), rangeUpper)
                              : centre;
    }
    return result;
}

Float3 PrinterLights::beamGain() const {
    PrinterLights p = normalized();
    Float3 gain;
    for (int c = 0; c < 3; ++c) gain[c] = std::pow(10.0f, (p[c] - centre) * logExposurePerPoint);
    return gain;
}

bool PrinterLights::applies(const FilmStock& stock, PrintPaper paper) {
    return isProjected(paper) && !viewsFilmDirectly(paper, stock) && !stock.isReflectionPrint &&
           ::fotufilm::resolved(paper, stock) == paper;
}

std::optional<PrinterLights> PrinterLights::resolved(const std::optional<PrinterLights>& lights,
                                                     const FilmStock& stock, PrintPaper paper) {
    if (!lights) return std::nullopt;
    PrinterLights normal = lights->normalized();
    if (normal == balanced() || !applies(stock, paper)) return std::nullopt;
    return normal;
}

uint64_t PrinterLights::spectralSignature() const {
    PrinterLights p = normalized();
    uint64_t h = 0x4C49474854530001ULL;
    for (float value : {p.red, p.green, p.blue}) h = (h ^ bits(value)) * 0x100000001b3ULL;
    return h;
}

}  // namespace fotufilm
