// Not an upstream file: the Android editor's view of the control catalogue and edit documents.
//
// A document is upstream EditDocument's Codable form, `{"exposure": 0.8, "paper": "screen", …}`,
// keyed by EditorControlField raw value. It may also carry `"stock": "<id>"`, which must then name
// the stock it is read against; the stock is otherwise handed alongside, as upstream hands the
// FilmStock to `options(for:)`.
#ifndef FOTUFILM_CORE_FOTUFILM_CONTROLS_H
#define FOTUFILM_CORE_FOTUFILM_CONTROLS_H

#include <string>

#include "Pipeline.h"

namespace fotufilm {

/// The Android surface's controls for `stockID` under `documentJSON`: per control its field,
/// section, kind, scale or choices (id and upstream English label), current value, and whether
/// the stock admits it and the current medium offers it. See the implementation for the layout.
/// Throws std::runtime_error for an unknown stock and DecodingError / EditDocument::Failure for
/// a malformed document.
std::string fotufilmCatalogueJSON(const std::string& stockID, const std::string& documentJSON);

/// `EditDocument.options(for:nativeFormatID:)` for a registered stock, its native gauge as the
/// fallback format. Throws as `fotufilmCatalogueJSON`, and EditDocument::Failure for a value the
/// document cannot carry onto this stock.
FotufilmEngine::Options optionsFromDocument(const std::string& stockID, const std::string& documentJSON);

/// The registered films to offer (FilmStockRegistry::presetIDs, display order) with what a film
/// picker shows: `[{"id", "name", "subtitle", "isMonochrome", "isReversal", "isReflectionPrint",
/// "isExample", "nativeFormat", "defaultPaper", "papers": [id…]}]`.
std::string fotufilmStocksJSON();

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FOTUFILM_CONTROLS_H
