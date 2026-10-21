// Port of WebProfileCatalogue.choices (Sources/FotufilmEditModel/WebProfileCatalogue.swift).
#include "WebProfileCatalogue.h"

#include "EditorControlCatalogue.h"
#include "FilmSpectralProfile.h"

namespace fotufilm {
namespace WebProfileCatalogue {

std::optional<std::vector<EditorMenuChoice>> choices(EditorControlField field, const FilmStock* stock, PrintPaper paper) {
    switch (field) {
        case EditorControlField::shutter: {
            std::vector<EditorMenuChoice> out{EditorMenuChoice(std::nullopt, "Instantaneous", "", std::string("off"))};
            const std::vector<double> times =
                stock ? EditorControlCatalogue::shutterTimes(stock) : EditorControlCatalogue::shutterLadder();
            for (double seconds : times) {
                out.emplace_back(seconds, EditorControlCatalogue::shutterName(seconds), "",
                                 std::to_string(static_cast<long long>(seconds)));
            }
            return out;
        }
        case EditorControlField::printLight: return EditorControlCatalogue::viewingLights(paper);
        case EditorControlField::negativeViewing: {
            std::vector<EditorMenuChoice> out;
            const auto& all = allNegativeViewings();
            for (size_t offset = 0; offset < all.size(); ++offset) {
                out.emplace_back(static_cast<double>(offset), name(all[offset]), "", id(all[offset]));
            }
            return out;
        }
        default: {
            const EditorControl* control = EditorControlCatalogue::control(field);
            if (!control) return std::nullopt;
            const EditorMenuChoices* menu = control->kind.menuOrNull();
            if (!menu || !menu->fixedChoices()) return std::nullopt;
            return *menu->fixedChoices();
        }
    }
}

}  // namespace WebProfileCatalogue
}  // namespace fotufilm
