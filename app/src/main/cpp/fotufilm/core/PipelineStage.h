// Port of Sources/FotufilmCore/PipelineStage.swift.
#ifndef FOTUFILM_CORE_PIPELINE_STAGE_H
#define FOTUFILM_CORE_PIPELINE_STAGE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fotufilm {

struct FilmStock;

/// Selects a pipeline span.
enum class PipelineStage { full, negative, print, texture };

const char* rawValue(PipelineStage stage);
std::optional<PipelineStage> pipelineStageFromRawValue(const std::string& raw);
inline std::string id(PipelineStage stage) { return rawValue(stage); }
const char* name(PipelineStage stage);
const std::vector<PipelineStage>& allPipelineStages();
/// Whether this stage reads the scene rather than a developed negative.
inline bool readsScene(PipelineStage stage) { return stage != PipelineStage::print; }
/// Whether this stage produces the finished image rather than an intermediate.
inline bool writesPrint(PipelineStage stage) { return stage == PipelineStage::full || stage == PipelineStage::print; }
/// Whether what this stage returns is light a host's transfer curve can encode.
inline bool writesEncodableLight(PipelineStage stage) { return stage != PipelineStage::negative; }
/// Whether the returned light is in the print's Display P3 basis.
inline bool deliversInPrintBasis(PipelineStage stage) { return writesPrint(stage); }
/// A small stable integer for cache identities and the flat parameter block (an ABI).
int32_t ordinal(PipelineStage stage);
std::optional<PipelineStage> pipelineStageFromOrdinal(int32_t ordinal);

/// Data exchanged between negative and print spans: base-10 diffuse optical density per record.
namespace NegativeInterchange {
constexpr float rangeLower = -0.5f;
constexpr float rangeUpper = 8;
/// Whether one density is inside the interchange's stated range.
bool contains(float density);
}  // namespace NegativeInterchange

struct TextureStageEntry;

/// Spatial stages available to `PipelineStage::texture` (an OptionSet over Int32).
struct TextureStages {
    int32_t rawValue = 0;

    constexpr TextureStages() = default;
    constexpr explicit TextureStages(int32_t raw) : rawValue(raw) {}

    static constexpr int32_t emulsionMTFBit = 1 << 0;
    static constexpr int32_t halationBit = 1 << 1;
    static constexpr int32_t adjacencyBit = 1 << 2;
    static constexpr int32_t grainBit = 1 << 3;
    static constexpr int32_t enlargerBit = 1 << 4;

    static constexpr TextureStages emulsionMTF() { return TextureStages(emulsionMTFBit); }
    static constexpr TextureStages halation() { return TextureStages(halationBit); }
    static constexpr TextureStages adjacency() { return TextureStages(adjacencyBit); }
    static constexpr TextureStages grain() { return TextureStages(grainBit); }
    static constexpr TextureStages enlarger() { return TextureStages(enlargerBit); }
    static constexpr TextureStages all() {
        return TextureStages(emulsionMTFBit | halationBit | adjacencyBit | grainBit | enlargerBit);
    }
    static constexpr TextureStages none() { return TextureStages(0); }

    bool contains(TextureStages member) const { return (rawValue & member.rawValue) == member.rawValue; }
    void insert(TextureStages member) { rawValue |= member.rawValue; }
    bool isEmpty() const { return rawValue == 0; }

    /// The selectable stages in a fixed order, each with its durable id.
    static const std::vector<TextureStageEntry>& ordered();

    /// Stages with nonzero stock contributions.
    static TextureStages offered(const FilmStock& stock);

    bool operator==(const TextureStages& o) const { return rawValue == o.rawValue; }
    bool operator!=(const TextureStages& o) const { return rawValue != o.rawValue; }
};

struct TextureStageEntry {
    const char* id;
    const char* name;
    TextureStages value;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PIPELINE_STAGE_H
