// Port of Sources/FotufilmCore/Enlarger.swift.
#ifndef FOTUFILM_CORE_ENLARGER_H
#define FOTUFILM_CORE_ENLARGER_H

#include <optional>
#include <string>
#include <vector>

#include "PrintPaper.h"

namespace fotufilm {

struct FilmStock;

/// The lamp house over the negative.
enum class Enlarger {
    /// Diffuse illumination: reads the diffuse density the sheets are measured in. The default.
    diffuser,
    /// A condenser head: the negative's densities read at their specular (Callier) values.
    condenser,
};

constexpr Enlarger kDefaultEnlarger = Enlarger::diffuser;
const std::vector<Enlarger>& allEnlargers();
const char* rawValue(Enlarger enlarger);
std::optional<Enlarger> enlargerFromRawValue(const std::string& raw);
inline std::string id(Enlarger enlarger) { return rawValue(enlarger); }
const char* name(Enlarger enlarger);
const char* detail(Enlarger enlarger);
inline std::optional<Enlarger> enlargerPreset(const std::string& id) { return enlargerFromRawValue(id); }

namespace EnlargerOptics {
/// Callier coefficient of a silver image under collimated light.
constexpr float silverCallierCoefficient = 1.4f;
/// Callier coefficient of a dye image under collimated light.
constexpr float dyeCallierCoefficient = 1.05f;
/// Whether a lamp house is in the path at all: only an optical enlargement onto a reflection sheet.
bool illuminates(const FilmStock& stock, PrintPaper paper);
}  // namespace EnlargerOptics

/// The factor the negative's diffuse densities are multiplied by on their way to the paper.
float callierCoefficient(Enlarger enlarger, const FilmStock& stock, PrintPaper paper);

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ENLARGER_H
