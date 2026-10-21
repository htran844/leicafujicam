// Port of Sources/FotufilmCore/CrystalGrainModel.swift: CrystalGrainPopulation and
// CrystalGrainModel (the crystal population the film grain model lays).
#ifndef FOTUFILM_CORE_CRYSTAL_GRAIN_MODEL_H
#define FOTUFILM_CORE_CRYSTAL_GRAIN_MODEL_H

#include <string>
#include <vector>

#include "CharacteristicCurve.h"
#include "Json.h"

namespace fotufilm {

struct FilmStock;

/// `CrystalGrainModel.binCount`.
constexpr int kCrystalGrainBinCount = 4;

/// Stock-authoring parameters for the film grain model's crystal population.
struct CrystalGrainPopulation {
    /// Maximum radius ratio on the class ladder; 1 gives equal radii.
    float radiusSpan;
    /// Target fractions of fitted density weight, fastest to slowest.
    std::vector<float> sublayerShares;
    /// Effective crystal count relative to the RMS-anchored population.
    float coatingDensityScale;

    CrystalGrainPopulation(float radiusSpan, std::vector<float> sublayerShares, float coatingDensityScale);

    static bool valid(float span, const std::vector<float>& shares, float scale);
    static CrystalGrainPopulation decode(const JsonValue& value, const std::string& path);

    bool operator==(const CrystalGrainPopulation& o) const {
        return radiusSpan == o.radiusSpan && sublayerShares == o.sublayerShares &&
               coatingDensityScale == o.coatingDensityScale;
    }
    bool operator!=(const CrystalGrainPopulation& o) const { return !(*this == o); }
};

/// The crystals that form the image, the population `FilmGrain` lays: which crystals an exposure
/// leaves developable, and what the tank makes of them.
struct CrystalGrainModel {
    /// Size bins per record: the sublayers the film grain model coats.
    static constexpr int binCount = kCrystalGrainBinCount;
    /// Log-exposure grid the fits and the density inversion run on.
    static const std::vector<float>& fitLogExposures();

    /// The coating and the light.
    struct Exposure {
        /// Speed classes: log10 exposure of one absorbed photon on average, relative to mid-grey.
        static const std::vector<float>& classLogExposures();
        static constexpr int hitThreshold = 3;
        static constexpr float sizeExponent = 1.0f / 3.0f;

        std::vector<float> weights;
        std::vector<float> logRadius;

        static float hitProbability(int hits, float logExposure, float classLogExposure);
        static float hitFraction(float logExposure, float classLogExposure);
        static float latentFraction(float logExposure, float classLogExposure, float developability);
        std::vector<float> latentFractions(float logExposure, float developability) const;
    };

    /// Everything the tank decides.
    struct Development {
        static constexpr float poolGain = 0.1f;
        static constexpr float dyeExponent = 2;
        static constexpr float markDispersion = 0.5f;
        static float markSecondMoment() { return 1 + markDispersion * markDispersion; }
        static const std::vector<float>& developabilities();

        struct Bin {
            float cloudRadiusMM;
            float dyePerCloud;
            float crystalsPerMM2;
            float pool;
            float demandAtFull;
            float gain;
            std::vector<int> classes;
            std::vector<float> shares;
        };

        std::vector<Bin> bins;
        bool isReversal;
        bool isSilver;
        std::vector<float> fogFractions;
        float developability;
        float fitError;

        static float saturated(float forming, float gain);
        static float dye(float demand, float pool);
        std::vector<float> formingFractions(const Exposure& exposure, float logExposure) const;
        std::vector<float> binFractions(const Exposure& exposure, float logExposure) const;
        float meanDensity(const Exposure& exposure, float logExposure) const;
        float apertureVariance(const Exposure& exposure, float logExposure) const;
    };

    Exposure exposure;
    Development development;
    /// The record's density above base at the sheet's read point, and the model's sigma there.
    float readNetDensity;
    float readSigma;
    float dMin;
    float range;
    CharacteristicCurve curve;

    /// Record `layer` of `stock` as developed; `reference` (nullable) is the same roll at the
    /// pack's reference process.
    CrystalGrainModel(const FilmStock& stock, const FilmStock* reference, int layer, float grainScale = 1);

    const std::vector<Development::Bin>& bins() const { return development.bins; }
    const std::vector<float>& weights() const { return exposure.weights; }
    float fitError() const { return development.fitError; }
    const std::vector<float>& fogFractions() const { return development.fogFractions; }
    bool isReversal() const { return development.isReversal; }
    bool isSilver() const { return development.isSilver; }

    static std::vector<float> fogFractions(const std::vector<float>& weights, const std::vector<float>& logRadius,
                                           float density);
    void refitDevelopment(const CharacteristicCurve& developed);

    std::vector<float> formingFractions(float logExposure) const;
    std::vector<float> binFractions(float logExposure) const;
    float meanDensity(float logExposure) const;
    float logExposure(float netDensity) const;
    float apertureVariance(float logExposure) const;
    float sigma(float netDensity) const;

    static std::vector<float> nonNegativeLeastSquares(const std::vector<std::vector<double>>& basis,
                                                      const std::vector<double>& targets);
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_CRYSTAL_GRAIN_MODEL_H
