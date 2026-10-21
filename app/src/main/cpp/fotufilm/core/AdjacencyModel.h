// Port of Sources/FotufilmCore/AdjacencyModel.swift.
#ifndef FOTUFILM_CORE_ADJACENCY_MODEL_H
#define FOTUFILM_CORE_ADJACENCY_MODEL_H

#include <optional>
#include <string>
#include <vector>

namespace fotufilm {

/// Lateral development transport and its response.
enum class AdjacencyModel { gaussian, screenedDiffusion };

inline const char* rawValue(AdjacencyModel model) {
    switch (model) {
        case AdjacencyModel::gaussian: return "gaussian";
        case AdjacencyModel::screenedDiffusion: return "screened-diffusion";
    }
    return "";
}

inline std::optional<AdjacencyModel> adjacencyModelFromRawValue(const std::string& raw) {
    if (raw == "gaussian") return AdjacencyModel::gaussian;
    if (raw == "screened-diffusion") return AdjacencyModel::screenedDiffusion;
    return std::nullopt;
}

inline const std::vector<AdjacencyModel>& allAdjacencyModels() {
    static const std::vector<AdjacencyModel> all = {AdjacencyModel::gaussian, AdjacencyModel::screenedDiffusion};
    return all;
}

/// Two-Gaussian fit to 1 / (1 + (ell k)^2); the authored radius keeps its Gaussian-sigma meaning,
/// ell = radius / sqrt(2).
namespace AdjacencyModelConstants {
constexpr float screenedPrimaryShare = 0.2753401713f;
constexpr float screenedPrimarySigma = 0.4338526089f / 1.4142135624f;
constexpr float screenedSecondarySigma = 1.4816463071f / 1.4142135624f;
}  // namespace AdjacencyModelConstants

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_ADJACENCY_MODEL_H
