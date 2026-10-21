// Port of Sources/FotufilmCore/FilmStockPack.swift: FilmStockDefinition and its decoding,
// `FilmStock.decoded(fromPackJSON:)`, and the id-keyed stock lookups. Sealed/encrypted packs,
// directory scanning and the pack cache are not ported: the host reads each stock JSON and
// registers its text. The frame-only fields `sheetNotches` and `edgePrinting` are not decoded.
#ifndef FOTUFILM_CORE_FILM_STOCK_PACK_H
#define FOTUFILM_CORE_FILM_STOCK_PACK_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "FilmStock.h"
#include "FilmStockValidation.h"
#include "Json.h"

namespace fotufilm {

/// Serialised form of a FilmStock.
struct FilmStockDefinition {
    struct CurveSpec {
        struct ComponentSpec {
            float gamma = 0;
            float toe = 0;
            float toeWidth = 0;
            float shoulder = 0;
            float shoulderWidth = 0;

            CharacteristicCurveComponent component() const;
            void validate(const std::string& field, const std::string& stockID) const;
        };

        float dMin = 0;
        float gamma = 0;
        float toe = 0;
        float toeWidth = 0;
        float shoulder = 0;
        float shoulderWidth = 0;
        std::optional<ComponentSpec> secondary;
        std::optional<SampledCharacteristicCurve> sampled;

        CharacteristicCurve curve() const;
        void validate(const std::string& field, const std::string& stockID) const;
        static CurveSpec decode(const JsonValue& value, const std::string& path);
    };

    struct DonorLayerSpec {
        std::string name;
        std::vector<float> sensitivity;
        CurveSpec curve;
        std::vector<float> inhibition;
        std::optional<float> releaseGamma;
        std::optional<float> depthUM;

        DonorCaptureLayer layer() const;
    };

    struct DevelopmentSpec {
        struct ConditionSpec {
            float stops = 0;
            std::string label;
            float timeMinutes = 0;
            std::optional<float> exposureIndex;
            /// "measured", "transferred" (with basisStock) or "estimated".
            std::string basis;
            std::optional<std::string> basisStock;
            std::vector<CurveSpec> curves;
            std::optional<std::vector<CurveSpec>> donorCurves;
            std::optional<float> grainStrength;
            std::optional<float> grainSizeMM;
            std::optional<std::vector<float>> grainLayerWeights;
            std::optional<float> grainFogDensity;
            std::optional<float> adjacencyStrength;

            /// nullopt for a basis the validator rejects.
            std::optional<FilmDevelopmentBasis> resolvedBasis() const;
            FilmDevelopmentCondition condition() const;
        };

        std::string developer;
        std::optional<std::string> dilution;
        float temperatureC = 0;
        std::string agitation;
        std::string source;
        int64_t sourcePage = 0;
        std::vector<ConditionSpec> conditions;

        FilmDevelopmentProfile profile() const;
    };

    /// How a pack describes the stock's wavelength-domain behaviour.
    struct SpectralSpec {
        enum class Kind { samples, separatedBase, measured, color, monochrome };
        Kind kind = Kind::samples;
        std::vector<std::vector<float>> layerSensitivity;  // samples, separatedBase, measured
        std::vector<std::vector<float>> imageDyeDensity;   // samples, separatedBase
        std::vector<float> minimumDensity;                 // separatedBase
        FilmDyeFamily dyeFamily = FilmDyeFamily::kodakNegative;  // measured, color
        std::vector<float> peaksNM;                        // color
        std::optional<std::vector<float>> widthsNM;        // color
        std::vector<float> rgbWeights;                     // monochrome

        FilmSpectralProfile profile() const;
        void validate(const std::string& stockID) const;
        static SpectralSpec decode(const JsonValue& value, const std::string& path);
    };

    static constexpr int64_t currentSchemaVersion = 2;
    static constexpr int64_t layeredTransportSchemaVersion = 3;

    int64_t schemaVersion = 0;
    std::string id;
    std::string name;
    std::optional<std::string> subtitle;
    std::optional<std::string> nativeFormatID;
    std::optional<float> referenceIlluminantKelvin;
    std::vector<std::vector<float>> sensitivity;
    SpectralSpec spectral;
    std::optional<float> sensitivityFloor;
    std::vector<CurveSpec> curves;
    std::optional<std::vector<DonorLayerSpec>> donorLayers;
    std::optional<float> flare;
    std::optional<std::vector<float>> emulsionDiffusionMM;
    std::optional<std::vector<float>> emulsionDiffusionSecondaryMM;
    std::optional<std::vector<float>> emulsionDiffusionPrimaryShare;
    std::optional<float> mtfLumaShare;
    std::optional<float> lumaDiffusionMM;
    std::vector<std::vector<float>> couplerInhibition;
    std::optional<std::vector<float>> couplerReleaseGamma;
    std::optional<CouplerGeometry> couplerGeometry;
    float couplerDiffusionMM = 0;
    std::optional<float> chromaticFringeAmount;
    std::optional<float> chromaticFringeRadiusMM;
    std::optional<float> adjacencyStrength;
    std::optional<AdjacencyModel> adjacencyModel;
    std::optional<float> adjacencyRadiusMM;
    float grainStrength = 0;
    float grainSizeMM = 0;
    std::vector<float> grainLayerWeights;
    std::optional<float> grainLumaCorrelation;
    std::optional<float> grainMottleShare;
    std::optional<float> grainMottleSizeRatio;
    std::optional<std::vector<float>> grainLayerSizeRatio;
    std::optional<GrainDensityLaw> grainDensityLaw;
    std::optional<GrainDensityProfile> grainDensityProfile;
    /// Required; always present on a decoded definition.
    std::optional<CrystalGrainPopulation> crystalGrainPopulation;
    std::optional<std::vector<float>> grainReversalProfile;
    std::optional<float> grainFogDensity;
    std::optional<GranularityReadDensity> granularityReadDensity;
    std::vector<float> halationStrength;
    std::optional<float> halationLookScale;
    std::optional<float> halationHazeMM;
    std::optional<HalationProfile> halationProfile;
    std::optional<LayeredTransport> layeredTransport;
    std::optional<HalationProfile> estimatedHalationProfile;
    std::optional<std::vector<std::vector<float>>> halationReturnMatrix;
    CurveSpec paperCurve;
    std::optional<float> paperMidDensity;
    std::optional<ReciprocityFailure> reciprocityFailure;
    std::optional<DevelopmentSpec> development;
    std::optional<bool> isMonochrome;
    std::optional<bool> isReversal;
    std::optional<bool> isExample;
    std::optional<bool> isReflectionPrint;
    std::optional<std::string> nativePrintMedium;
    std::optional<std::string> spectralLineage;

    /// JSONDecoder's decode of one pack entry. Throws DecodingError.
    static FilmStockDefinition decode(const JsonValue& value);

    /// Throws ValidationFailure on the first problem, naming the field.
    void validate() const;
    /// `*this` if it passes.
    const FilmStockDefinition& validated() const {
        validate();
        return *this;
    }

    /// Materialise the definition into a renderable stock.
    FilmStock stock() const;
};

/// Parses, decodes and validates one stock JSON (`decoder.decode(...).validated()`).
FilmStockDefinition decodeFilmStockDefinition(const std::string& json);
/// `FilmStock.decoded(fromPackJSON:)`.
FilmStock decodeFilmStock(const std::string& json);

/// Id-keyed stock lookups over definitions the host registers from the JSON it read
/// (`FilmStock.presets`, `presetIDs`, `named`, `default` and `presetDefinitions`).
class FilmStockRegistry {
public:
    static FilmStockRegistry& shared();

    /// Decodes, validates and registers one stock under its id, replacing any earlier one with the
    /// same id. Returns the id. Throws DecodingError or ValidationFailure; nothing is registered then.
    std::string registerJSON(const std::string& json);
    void clear();

    /// `FilmStock.named(_:)`.
    std::optional<FilmStock> named(const std::string& id) const;
    std::optional<FilmStockDefinition> definition(const std::string& id) const;
    /// The definition's `nativeFormatID`, for `FilmFormat::nativeID`.
    std::optional<std::string> nativeFormatID(const std::string& id) const;
    /// Every registered id, examples included, sorted.
    std::vector<std::string> allPresetIDs() const;
    /// The films to offer a user: non-example ids, or every id when only examples exist.
    std::vector<std::string> presetIDs() const;
    /// The first id in display order.
    std::optional<FilmStock> defaultStock() const;

private:
    struct Entry {
        FilmStockDefinition definition;
        FilmStock stock;
    };
    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_STOCK_PACK_H
