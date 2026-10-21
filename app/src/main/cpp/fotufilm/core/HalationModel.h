// Port of Sources/FotufilmCore/HalationModel.swift, legacy part: the model selector. The layered
// transport's constructions (`adjusted`, `illustrative`) are not ported; `transportConstruction`
// and `withoutLayeredTransport` are on FotufilmEngine::Options (Pipeline.h).
#ifndef FOTUFILM_CORE_HALATION_MODEL_H
#define FOTUFILM_CORE_HALATION_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fotufilm {

enum class HalationModel { legacy, layered };

inline const char* rawValue(HalationModel model) { return model == HalationModel::legacy ? "legacy" : "layered"; }
inline std::optional<HalationModel> halationModelFromRawValue(const std::string& raw) {
    if (raw == "legacy") return HalationModel::legacy;
    if (raw == "layered") return HalationModel::layered;
    return std::nullopt;
}
inline std::string id(HalationModel model) { return rawValue(model); }
inline const char* name(HalationModel model) {
    return model == HalationModel::legacy ? "Legacy" : "Layered Transport";
}
inline const std::vector<HalationModel>& allHalationModels() {
    static const std::vector<HalationModel> all = {HalationModel::legacy, HalationModel::layered};
    return all;
}

/// LayeredTransportRenderer.swift's `TransportBackend` (Int32 raw values). Kept only so Options
/// carries the upstream field; the layered renderer is not ported.
enum class TransportBackend : int32_t { cpu = 0, metal = 1 };

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HALATION_MODEL_H
