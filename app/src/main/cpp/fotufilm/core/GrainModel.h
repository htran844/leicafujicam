// Port of Sources/FotufilmCore/GrainModel.swift.
#ifndef FOTUFILM_CORE_GRAIN_MODEL_H
#define FOTUFILM_CORE_GRAIN_MODEL_H

#include <optional>
#include <string>
#include <vector>

namespace fotufilm {

/// Which model develops grain.
enum class GrainModel {
    /// A unit-variance field blurred to the clump's correlation length.
    clumpField,
    /// The film's own crystals laid where they sit on the emulsion.
    film,
};

const char* rawValue(GrainModel model);
std::optional<GrainModel> grainModelFromRawValue(const std::string& raw);
inline std::string id(GrainModel model) { return rawValue(model); }
const char* title(GrainModel model);
const char* detail(GrainModel model);
const std::vector<GrainModel>& allGrainModels();
/// Resolves an identifier or alias (e.g. "standard", "clump", "film").
std::optional<GrainModel> grainModelNamed(const std::string& name);

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_GRAIN_MODEL_H
