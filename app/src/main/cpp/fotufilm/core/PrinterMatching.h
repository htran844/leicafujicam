// Port of Sources/FotufilmCore/PrinterMatching.swift (the `PrinterProfile.matching` extension).
#ifndef FOTUFILM_CORE_PRINTER_MATCHING_H
#define FOTUFILM_CORE_PRINTER_MATCHING_H

#include <optional>
#include <stdexcept>

#include "Enlarger.h"
#include "PrinterProfile.h"
#include "Simd.h"

namespace fotufilm {

struct FilmStock;

namespace PrinterMatching {

enum class Matching { fixed, density, densityAndColor };

/// `PrinterProfile.MatchingError`.
struct MatchingError : std::runtime_error {
    enum class Kind { unsupportedMedium, invalidReference };
    Kind kind;
    explicit MatchingError(Kind k)
        : std::runtime_error(k == Kind::unsupportedMedium ? "printer matching: unsupported medium"
                                                          : "printer matching: invalid reference"),
          kind(k) {}
};

struct Match {
    PrinterProfile printer;
    Float3 referenceRGB;
    Float3 targetRGB;
    /// Maximum absolute error in display-linear RGB, not a perceptual Delta E.
    float residual;
    /// True when the requested match was not reached within the profile's constraints.
    bool limited;
};

/// Adjusts `profile` against a chosen developed-film reference patch; throws MatchingError.
Match matching(const PrinterProfile& profile, Matching mode, Float3 referenceDensity, Float3 targetDensity,
               const FilmStock& stock, PrintPaper paper = PrintPaper::ektacolorEdge,
               Enlarger enlarger = Enlarger::diffuser, float bleachBypass = 0,
               std::optional<float> printViewingKelvin = std::nullopt);

}  // namespace PrinterMatching
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINTER_MATCHING_H
