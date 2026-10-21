// Port of Sources/FotufilmCore/GrainModel.swift.
#include "GrainModel.h"

#include <cctype>

namespace fotufilm {

const char* rawValue(GrainModel model) {
    switch (model) {
        case GrainModel::clumpField: return "clump";
        case GrainModel::film: return "film";
    }
    return "";
}

std::optional<GrainModel> grainModelFromRawValue(const std::string& raw) {
    if (raw == "clump") return GrainModel::clumpField;
    if (raw == "film") return GrainModel::film;
    return std::nullopt;
}

const char* title(GrainModel model) {
    switch (model) {
        case GrainModel::clumpField: return "Standard";
        case GrainModel::film: return "Film";
    }
    return "";
}

const char* detail(GrainModel model) {
    switch (model) {
        case GrainModel::clumpField: return "Fast calibrated RMS grain";
        case GrainModel::film: return "Crystals on the film itself, averaged as light";
    }
    return "";
}

const std::vector<GrainModel>& allGrainModels() {
    static const std::vector<GrainModel> all = {GrainModel::clumpField, GrainModel::film};
    return all;
}

std::optional<GrainModel> grainModelNamed(const std::string& name) {
    // trimmingCharacters(in: .whitespacesAndNewlines).lowercased(), for the ASCII aliases.
    size_t begin = 0, end = name.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(name[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1]))) --end;
    std::string key;
    for (size_t i = begin; i < end; ++i) key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
    if (key == "clump" || key == "clumpfield" || key == "clump-field" || key == "standard" || key == "fast") {
        return GrainModel::clumpField;
    }
    if (key == "film" || key == "real" || key == "real-film" || key == "film-grain") return GrainModel::film;
    return std::nullopt;
}

}  // namespace fotufilm
