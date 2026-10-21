// Port of Sources/FotufilmCore/PipelineStage.swift.
#include "PipelineStage.h"

#include <algorithm>
#include <cmath>

#include "FilmStock.h"
#include "Simd.h"

namespace fotufilm {

const char* rawValue(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::full: return "full";
        case PipelineStage::negative: return "negative";
        case PipelineStage::print: return "print";
        case PipelineStage::texture: return "texture";
    }
    return "";
}

std::optional<PipelineStage> pipelineStageFromRawValue(const std::string& raw) {
    for (PipelineStage stage : allPipelineStages()) {
        if (raw == rawValue(stage)) return stage;
    }
    return std::nullopt;
}

const char* name(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::full: return "Full";
        case PipelineStage::negative: return "Negative Only";
        case PipelineStage::print: return "Print Only";
        case PipelineStage::texture: return "Texture Only";
    }
    return "";
}

const std::vector<PipelineStage>& allPipelineStages() {
    static const std::vector<PipelineStage> all = {PipelineStage::full, PipelineStage::negative,
                                                   PipelineStage::print, PipelineStage::texture};
    return all;
}

int32_t ordinal(PipelineStage stage) {
    switch (stage) {
        case PipelineStage::full: return 0;
        case PipelineStage::negative: return 1;
        case PipelineStage::print: return 2;
        case PipelineStage::texture: return 3;
    }
    return 0;
}

std::optional<PipelineStage> pipelineStageFromOrdinal(int32_t value) {
    for (PipelineStage stage : allPipelineStages()) {
        if (ordinal(stage) == value) return stage;
    }
    return std::nullopt;
}

bool NegativeInterchange::contains(float density) {
    return std::isfinite(density) && inClosedRange(density, rangeLower, rangeUpper);
}

const std::vector<TextureStageEntry>& TextureStages::ordered() {
    static const std::vector<TextureStageEntry> list = {
        {"emulsion-mtf", "Emulsion Diffusion", emulsionMTF()},
        {"halation", "Halation", halation()},
        {"adjacency", "Adjacency", adjacency()},
        {"grain", "Grain", grain()},
        {"enlarger", "Enlarger", enlarger()},
    };
    return list;
}

TextureStages TextureStages::offered(const FilmStock& stock) {
    TextureStages result;
    bool primaryMTF = false;
    size_t n = std::min(stock.emulsionDiffusionMM.size(), stock.emulsionDiffusionPrimaryShare.size());
    for (size_t i = 0; i < n; ++i) {
        if (stock.emulsionDiffusionMM[i] > 0 && stock.emulsionDiffusionPrimaryShare[i] > 0) primaryMTF = true;
    }
    bool secondaryMTF = false;
    n = std::min(stock.emulsionDiffusionSecondaryMM.size(), stock.emulsionDiffusionPrimaryShare.size());
    for (size_t i = 0; i < n; ++i) {
        if (stock.emulsionDiffusionSecondaryMM[i] > 0 && stock.emulsionDiffusionPrimaryShare[i] < 1) {
            secondaryMTF = true;
        }
    }
    if (primaryMTF || secondaryMTF) result.insert(emulsionMTF());
    for (float h : stock.halationStrength) {
        if (h > 0) {
            result.insert(halation());
            break;
        }
    }
    // The spatial half of the coupler stages: the adjacency effect, and the couplers' diffusion.
    bool anyInhibition = false;
    for (const auto& row : stock.couplerInhibition) {
        for (float v : row) anyInhibition = anyInhibition || v != 0;
    }
    bool diffusingCouplers = stock.couplerDiffusionMM > 0 && anyInhibition;
    if (stock.adjacencyStrength > 0 || diffusingCouplers) result.insert(adjacency());
    if (stock.grainStrength > 0) result.insert(grain());
    if (!stock.isReversal) result.insert(enlarger());
    return result;
}

}  // namespace fotufilm
