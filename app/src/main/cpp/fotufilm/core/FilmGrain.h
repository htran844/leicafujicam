// Port of Sources/FotufilmCore/FilmGrain.swift (the population, the reference render and
// FilmRandom) and FilmGrainTiles.swift (the tiles, pitch tables, configuration block and
// binding registry), defined in FilmGrain.cpp and FilmGrainTiles.cpp. The Halide/Metal tile
// builders are replaced by FilmTileBuild.h, a plain C++ port of Pipeline/FilmTileBuild.h.
// `apply`/`applyReference` (CPU grain over an ImageBuffer) are not ported: the GLES graph lays
// the grain from the tiles.
#ifndef FOTUFILM_CORE_FILM_GRAIN_H
#define FOTUFILM_CORE_FILM_GRAIN_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "FilmStock.h"
#include "FotufilmConfigLayout.h"
#include "Simd.h"

namespace fotufilm {

/// Throws to cancel; an empty function never cancels.
using CheckCancellation = std::function<void()>;

/// Grain laid on the film itself, for `GrainModel::film`.
struct FilmGrain {
    static constexpr float dyeCloudDecayMM = 0.00145f;
    struct CloudTerm {
        float sigma;
        float weight;
    };
    /// Jarvis's cloud `exp(-ρ)` as four Gaussians.
    static const std::vector<CloudTerm>& dyeCloudTerms();
    static constexpr float silverGrainEdge = 12;
    static constexpr float silverGrainDensity = 2;
    static constexpr float fastestCrystalMM = 0.0012f;
    static constexpr float finestSampleMM = 0.00035f;
    static constexpr int maxSupersample = 8;
    /// Shape of the gamma distribution each crystal's dye is drawn from.
    static int markShape();
    static constexpr int tableSamples = 129;
    static constexpr int biasSamples = 17;
    static constexpr float biasPatchMM = 0.16f;

    /// How a developed crystal's demand spreads over the film (UInt32 raw values).
    enum class Profile : uint32_t { silverGrain = 0, dyeCloud = 1 };
    static double profileValue(Profile profile, double rho);
    static double profileReach(Profile profile, double peak);

    /// One coated sublayer of a record, as this model lays it.
    struct Sublayer {
        float coatedPerMM2 = 0;
        float sigmaMM = 0;
        float peakDemand = 0;
        float capacity = 0;
        float edge = 0;
        float smallestSigmaMM = 0;
        float dyePerCloudMM2 = 0;
        float cellMM = 0;
        std::vector<float> forming;
        float voidIntegralMM2 = 0;
        Profile profile = Profile::dyeCloud;

        static Sublayer make(Profile profile, float coatedPerMM2, float capacity, float edge, float smallestSigmaMM,
                             float dyePerCloudMM2, float cellMM, std::vector<float> forming);
        /// Restores an already solved population without re-running its numerical fit.
        static Sublayer restored(Profile profile, float coatedPerMM2, float sigmaMM, float peakDemand, float capacity,
                                 float edge, float smallestSigmaMM, float dyePerCloudMM2, float cellMM,
                                 std::vector<float> forming, float voidIntegralMM2);
        void shape();
        float halfCapacityRadiusMM() const;
    };

    struct Record {
        std::vector<Sublayer> sublayers;
        float dMin = 0;
        float dMax = 0;
    };

    // ---- FilmGrainTiles.swift ----
    static constexpr float tileTexelMM = 0.001f;
    static constexpr int tileSide = FOTUFILM_FILM_TILE_SIDE;
    static constexpr int tileLevels = FOTUFILM_FILM_TILE_LEVELS;
    static constexpr float tileBlockMM = 0.128f;
    static constexpr float tileMarginMM = 0.064f;
    static constexpr float tileSeamMM = 0.016f;
    static constexpr int tileLayers = FOTUFILM_FILM_TILE_LAYERS;
    static constexpr uint64_t tileSeed = 0xA11C40575EED0001ull;
    static constexpr uint32_t tileStream = 200;

    /// One record's tile at one gross density.
    struct TileLevel {
        /// `(tileSide + 1)²` running sums of transmittance less `meanLight`, row-major.
        std::vector<float> sums;
        float meanLight = 0;
    };

    /// What one frame's pitch needs from the tiles, per record.
    struct PitchTables {
        std::vector<std::vector<float>> meanDensity;
        std::vector<std::vector<float>> blendCorrelation;
    };

    class Tiles {
    public:
        Tiles(std::vector<std::vector<TileLevel>> levels, std::vector<float> dMin, std::vector<float> dMax);
        /// Per record (empty where the record lays no grain), `tileLevels` levels.
        const std::vector<std::vector<TileLevel>> levels;
        const std::vector<float> dMin;
        const std::vector<float> dMax;
        /// The tables of `pitch` texels, measured on first use; the last eight are kept.
        PitchTables tables(float pitch, const std::function<PitchTables()>& measure) const;
        /// `(tileSide + 1)²` entries, then `tileLevels` levels, then three records, innermost first.
        std::vector<float> packed() const;

    private:
        mutable std::mutex lock_;
        mutable std::vector<std::pair<float, PitchTables>> pitches_;
    };

    /// How a frame lays the film's grain beyond its amount.
    struct Look {
        Float3 layers = Float3::one();
        float colour = 1;
        float size = 1;
        float softness = 1;

        static constexpr float sizeRangeLower = 0.5f;
        static constexpr float sizeRangeUpper = 4;
        static constexpr float softnessRangeLower = 0.5f;
        static constexpr float softnessRangeUpper = 4;

        struct Geometry {
            float pitch;
            float footprint;
        };
        /// The pixel pitch and footprint in tile texels at `pxPerMM`.
        Geometry geometry(float pxPerMM) const;
        /// Each record's grain amount at `amount`, the size's compensation included.
        std::vector<float> recordAmounts(float amount) const;
        struct Mix {
            float own;
            float shared;
        };
        static Mix mix(float colour);

        bool operator==(const Look& o) const {
            return layers == o.layers && colour == o.colour && size == o.size && softness == o.softness;
        }
    };

    std::vector<Record> records;
    bool monochrome = false;
    /// Identity of this population: the stock and grain scale it was built for.
    std::string key;
    /// A decoded bank belongs to the population.
    std::shared_ptr<const Tiles> assetTiles;

    /// A decoded population (FilmGrainAsset).
    FilmGrain(std::vector<Record> records, bool monochrome, const std::string& identity,
              std::shared_ptr<const Tiles> tiles);
    /// The population of `stock` as developed; `reference` (nullable) is the same roll at the
    /// pack's reference process.
    FilmGrain(const FilmStock& stock, const FilmStock* reference, float grainScale, bool useCachedAnchor,
              const CheckCancellation& checkCancellation);

    static std::string anchorKey(const FilmStock& stock, float grainScale);
    static std::string populationKey(const std::string& identity, float grainScale);

    struct Anchor {
        float gross;
        float sigma;
    };
    void anchorToSheet(const std::vector<Anchor>& anchors, const std::string& key, bool useCache,
                       const CheckCancellation& checkCancellation);
    static constexpr int anchorPasses = 8;
    static constexpr float anchorTolerance = 0.02f;
    void scale(int record, float factor);
    float sigma48(int record, float gross) const;

    static float unitCloudDye(float capacity, float edge, Profile profile);
    static float unitVoidIntegral(float edge, Profile profile);
    static float occupiedArea(float peak, Profile profile);
    float pointMean(int record, float gross) const;
    static double gammaQuantile(double p, double shape);
    static int supersample(float pxPerMM);

    static float lookup(const std::vector<float>& table, float gross, float lo, float hi);
    static float bilinear(const std::vector<float>& plane, int width, int height, float x, float y);

    using GrossAt = std::function<float(float, float)>;
    /// Effective density above base per pixel of record `r`, tile by tile.
    std::vector<float> render(int record, int width, int height, float pxPerMM, int supersample, uint64_t seed,
                              const GrossAt& grossAt, std::vector<float>* pointMeans,
                              std::optional<float> periodMM = std::nullopt) const;
    static void renderTile(const Record& record, int recordIndex, int x0, int y0, int tw, int th, float pxPerMM,
                           int supersample, uint64_t seed, const GrossAt& grossAt, float* output, float* means,
                           int width, std::optional<float> periodMM);
    std::vector<float> meanTable(int record, float pxPerMM, int supersample, uint64_t seed) const;

    static void layCloudTerm(const float* deposits, int gw, int gh, int margin, float* demand, int sw, int sh,
                             float sigma, float weight);
    static int tileTermFactor(float sigma, int samples);
    static std::vector<float> gaussianKernel(float sigma);
    static void blurRows(const float* input, int rowStride, int firstColumn, int rows, int columns,
                         const std::vector<float>& kernel, float* output, std::optional<int> outputStride = std::nullopt,
                         int outputOffset = 0);
    static void blurColumns(const float* input, int columns, int firstRow, int rows, const std::vector<float>& kernel,
                            float scale, float* output, bool accumulate = true);

    // ---- FilmGrainTiles.swift ----
    /// This population's tiles, rendered on first use (shared cache of four).
    std::shared_ptr<const Tiles> tiles(const CheckCancellation& checkCancellation = {}) const;
    std::vector<float> tileLight(int record, float gross, uint64_t seed) const;
    std::vector<float> referenceTileLight(int record, float gross, uint64_t seed) const;
    /// Dye-cloud records through FilmTileBuild; nullopt where it cannot lay them.
    std::optional<std::vector<std::vector<float>>> builtLights(int record, const std::vector<float>& grosses,
                                                               uint64_t seed) const;
    std::shared_ptr<const Tiles> buildTiles(uint64_t seed, bool parallel = true,
                                            const CheckCancellation& checkCancellation = {}) const;
    static TileLevel tileLevel(const std::vector<float>& light);
    PitchTables pitchTables(const Tiles& tiles, float footprint) const;
    static PitchTables measurePitch(const Tiles& tiles, double pitch, int side = 128);
    static float blend(float a, float b, float w, float rho);
    static double sumAt(const std::vector<float>& sums, double x, double y);
    static double boxSum(const std::vector<float>& sums, double x0, double x1, double y0, double y1);
    static double wrappedSumAt(const std::vector<float>& sums, double x, double y);
    static double wrappedBoxSum(const std::vector<float>& sums, double x0, double x1, double y0, double y1);
    static float tileSigma48(const std::vector<float>& light);

    struct Placement {
        double ox;
        double oy;
        int turn;
    };
    static Placement blockPlacement(uint32_t seed, int layer, int record, int bx, int by);
    static double blockTexels();
    static double marginTexels();
    static double seamTexels();
    static double layingShift(int layer);
    static double blockWeight(double c, int b);
    static double blockSharing(int lag);
    static std::pair<float, float> blockLight(const TileLevel& a, const TileLevel& b, double x0, double x1, double y0,
                                              double y1, int bx, int by, uint32_t seed, int layer, int record);

    /// The configuration's FILM_TILE block (FOTUFILM_CONFIG_FILM_TILE_COUNT floats).
    std::vector<float> configurationBlock(float pxPerMM, float amount, const Look& look, int32_t id,
                                          const Tiles* supplied = nullptr) const;

    /// A canonical tile bank and its id. A frame keeps its binding so cache eviction cannot
    /// remove its grain while another stock is preparing or rendering.
    class TileBinding {
    public:
        TileBinding(std::shared_ptr<const FilmGrain> grain, std::shared_ptr<const Tiles> tiles, int32_t id);
        const std::shared_ptr<const FilmGrain> grain;
        const int32_t id;
        bool usedAsset() const { return grain->assetTiles != nullptr; }
        /// Status of registering the bank with a renderer; always 0 here, where the host uploads
        /// the packed bank itself.
        const int32_t registrationStatus = 0;
        std::vector<float> configurationBlock(float pxPerMM, float amount, const Look& look) const;
        /// The immutable bank in the canonical (entry, level, record) layout, packed once.
        std::shared_ptr<const std::vector<float>> packed() const;
        const std::shared_ptr<const Tiles>& tiles() const { return tiles_; }

    private:
        std::shared_ptr<const Tiles> tiles_;
        mutable std::mutex packLock_;
        mutable std::shared_ptr<const std::vector<float>> packed_;
    };

    /// The rolls a develop's Film grain population is prepared for.
    struct Population {
        FilmStock stock;
        std::optional<FilmStock> reference;
        /// Equal for every develop that draws the same population.
        std::string identity() const;
    };

    static std::shared_ptr<const TileBinding> binding(const FilmStock& stock, const FilmStock* reference,
                                                      const CheckCancellation& checkCancellation = {});
    static bool isPrepared(const Population& population);
    static void prepare(const Population& population, const CheckCancellation& checkCancellation = {});
    /// The packed tiles a frame's configuration names in its FILM_TILE block, while a binding
    /// for that id is alive.
    static std::shared_ptr<const std::vector<float>> registeredTiles(const std::vector<float>& configuration);
};

/// Counter-based draws for one film cell of one sublayer.
class FilmRandom {
public:
    FilmRandom(uint64_t seed, int record, int sublayer, int stream, int x, int y);
    static uint32_t seed32(uint64_t seed);
    uint32_t next();
    /// Uniform in (0, 1).
    float uniform();
    static constexpr int maxCount = 48;
    static std::vector<float> countThresholds(float mean);
    static int count(float u, const std::vector<float>& thresholds);
    uint32_t key() const { return key_; }

private:
    uint32_t key_;
    uint32_t counter_ = 0;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_GRAIN_H
