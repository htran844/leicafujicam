// Port of the part of Sources/FotufilmEditModel/WebProfileCatalogue.swift that EditDocument reads:
// the stock- and medium-dependent menu choices. The browser profile export is not ported.
#ifndef FOTUFILM_CORE_WEB_PROFILE_CATALOGUE_H
#define FOTUFILM_CORE_WEB_PROFILE_CATALOGUE_H

#include <optional>
#include <vector>

#include "EditorControl.h"
#include "PrintPaper.h"

namespace fotufilm {

struct FilmStock;

namespace WebProfileCatalogue {
std::optional<std::vector<EditorMenuChoice>> choices(EditorControlField field, const FilmStock* stock, PrintPaper paper);
}  // namespace WebProfileCatalogue

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_WEB_PROFILE_CATALOGUE_H
