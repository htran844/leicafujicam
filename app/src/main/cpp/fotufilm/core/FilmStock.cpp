// Port of Sources/FotufilmCore/FilmStock.swift.
#include "FilmStock.h"

#include <cmath>

#include "ColorScience.h"
#include "FilmStockDefaults.h"
#include "CoreMath.h"
#include "Simd.h"

namespace fotufilm {

// ---- GrainDensityLaw / GranularityReadDensity ----

std::optional<GrainDensityLaw> grainDensityLawFromRawValue(int32_t raw) {
    switch (raw) {
        case 0: return GrainDensityLaw::dyeCloud;
        case 1: return GrainDensityLaw::silver;
        case 2: return GrainDensityLaw::dyeCloudSelwyn;
        case 3: return GrainDensityLaw::dyeCloudReversal;
        default: return std::nullopt;
    }
}

const char* rawValue(GranularityReadDensity density) {
    switch (density) {
        case GranularityReadDensity::net: return "net";
        case GranularityReadDensity::gross: return "gross";
    }
    return "";
}

std::optional<GranularityReadDensity> granularityReadDensityFromRawValue(const std::string& raw) {
    if (raw == "net") return GranularityReadDensity::net;
    if (raw == "gross") return GranularityReadDensity::gross;
    return std::nullopt;
}

// ---- GrainDensityProfile ----

namespace {
std::optional<std::vector<float>> padRow(const std::vector<float>& row) {
    for (float v : row) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    switch (row.size()) {
        case 3: return std::vector<float>{row[0], row[1], row[2], 0, 1, 0.3f};
        case 6: return row;
        default: return std::nullopt;
    }
}
}  // namespace

GrainDensityProfile GrainDensityProfile::shared(const std::vector<float>& row) {
    return fromRecords({row, row, row});
}

GrainDensityProfile GrainDensityProfile::fromRecords(const std::vector<std::vector<float>>& records) {
    GrainDensityProfile profile;
    std::vector<std::vector<float>> padded;
    bool complete = records.size() == 3;
    for (const auto& row : records) {
        auto p = padRow(row);
        if (!p) {
            complete = false;
            break;
        }
        padded.push_back(*p);
    }
    profile.records_ = complete ? padded : FilmStockDefaults::grainDensityRecords();
    return profile;
}

bool GrainDensityProfile::isShared() const {
    for (const auto& row : records_) {
        if (row != records_[0]) return false;
    }
    return true;
}

bool GrainDensityProfile::hasHump() const {
    for (const auto& row : records_) {
        if (row[3] != 0) return true;
    }
    return false;
}

float GrainDensityProfile::variance(int record, float density) const {
    const std::vector<float>& row = records_[record];
    float toe = swiftMax(row[1], 1e-4f), decay = swiftMax(row[2], 1e-4f), width = swiftMax(row[5], 1e-4f);
    float offset = (density - row[4]) / width;
    return (1 - std::exp(-density / toe)) *
           (1 + row[0] * std::exp(-density / decay) + row[3] * std::exp(-0.5f * offset * offset));
}

GrainDensityProfile GrainDensityProfile::decode(const JsonValue& value, const std::string& path) {
    // `try? decode([Float])`, falling back to `[[Float]]`.
    std::vector<float> flat;
    bool isFlat = true;
    try {
        flat = json::asFloatArray(value, path);
    } catch (const DecodingError&) {
        isFlat = false;
    }
    if (isFlat) return shared(flat);
    return fromRecords(json::asFloatMatrix(value, path));
}

// ---- FilmStock ----

const GrainDensityProfile& FilmStock::defaultGrainDensityProfile() { return FilmStockDefaults::grainDensityProfile(); }

const std::vector<float>& FilmStock::defaultGrainReversalProfile() {
    static const std::vector<float> profile = {1.1f, 3};
    return profile;
}

namespace {
const CrystalGrainPopulation& requiredPopulation(const FilmStock::Init& init) {
    preconditionCheck(init.crystalGrainPopulation.has_value(), "FilmStock requires crystalGrainPopulation");
    return *init.crystalGrainPopulation;
}
const CharacteristicCurve& requiredPaperCurve(const FilmStock::Init& init) {
    preconditionCheck(init.paperCurve.has_value(), "FilmStock requires paperCurve");
    return *init.paperCurve;
}
}  // namespace

FilmStock::FilmStock(const Init& init)
    : name(init.name),
      sensitivity(rowNormalized(init.sensitivity)),
      referenceIlluminantKelvin(init.referenceIlluminantKelvin),
      spectralProfile(init.spectralProfile
                          ? *init.spectralProfile
                          : FilmSpectralProfile::approximation(rowNormalized(init.sensitivity), init.isMonochrome,
                                                               init.isReversal)),
      grainDensityProfile(init.grainDensityProfile ? *init.grainDensityProfile : defaultGrainDensityProfile()),
      crystalGrainPopulation(requiredPopulation(init)),
      paperCurve(requiredPaperCurve(init)) {
    curves = init.curves;
    donorLayers = init.donorLayers;
    flare = init.flare;
    emulsionDiffusionMM = init.emulsionDiffusionMM;
    emulsionDiffusionSecondaryMM = init.emulsionDiffusionSecondaryMM;
    emulsionDiffusionPrimaryShare = init.emulsionDiffusionPrimaryShare;
    mtfLumaShare = init.mtfLumaShare;
    const Float3 weights = ColorScience::luminanceWeights;
    float layerVariance[3];
    for (int layer = 0; layer < 3; ++layer) {
        float primary = layer < static_cast<int>(init.emulsionDiffusionMM.size()) ? init.emulsionDiffusionMM[layer] : 0;
        float secondary = layer < static_cast<int>(init.emulsionDiffusionSecondaryMM.size())
                              ? init.emulsionDiffusionSecondaryMM[layer]
                              : 0;
        float share = layer < static_cast<int>(init.emulsionDiffusionPrimaryShare.size())
                          ? swiftMin(swiftMax(init.emulsionDiffusionPrimaryShare[layer], 0.0f), 1.0f)
                          : 1;
        layerVariance[layer] = share * primary * primary + (1 - share) * secondary * secondary;
    }
    float meanVariance = 0;
    for (int layer = 0; layer < 3; ++layer) meanVariance = meanVariance + weights[layer] * layerVariance[layer];
    lumaDiffusionMM = init.lumaDiffusionMM ? *init.lumaDiffusionMM : std::sqrt(meanVariance);
    couplerGeometry_ = init.couplerGeometry;
    couplerInhibition = couplerGeometry_ ? couplerGeometry_->matrix() : init.couplerInhibition;
    couplerReleaseGamma = init.couplerReleaseGamma;
    couplerDiffusionMM = init.couplerDiffusionMM;
    chromaticFringeAmount = init.chromaticFringeAmount;
    chromaticFringeRadiusMM = init.chromaticFringeRadiusMM;
    adjacencyStrength = init.adjacencyStrength;
    adjacencyModel = init.adjacencyModel;
    adjacencyRadiusMM = init.adjacencyRadiusMM;
    grainStrength = init.grainStrength;
    grainSizeMM = init.grainSizeMM;
    grainLayerWeights = init.grainLayerWeights;
    grainLumaCorrelation = init.isMonochrome ? 1 : swiftMin(swiftMax(init.grainLumaCorrelation, 0.0f), 1.0f);
    grainMottleShare = swiftMin(swiftMax(init.grainMottleShare, 0.0f), 0.9f);
    grainMottleSizeRatio = swiftMax(init.grainMottleSizeRatio, 1.0f);
    if (init.grainLayerSizeRatio.size() == 3) {
        grainLayerSizeRatio.clear();
        for (float r : init.grainLayerSizeRatio) grainLayerSizeRatio.push_back(swiftMax(r, 0.05f));
    } else {
        grainLayerSizeRatio = {1, 1, 1};
    }
    grainDensityLaw = init.grainDensityLaw
                          ? *init.grainDensityLaw
                          : (init.isMonochrome ? GrainDensityLaw::silver
                                               : (init.isReversal ? GrainDensityLaw::dyeCloudReversal
                                                                  : GrainDensityLaw::dyeCloud));
    const std::vector<float>& reversal = init.grainReversalProfile;
    preconditionCheck(reversal.size() == 2 && std::isfinite(reversal[0]) && inClosedRange(reversal[0], 0.1f, 2.0f) &&
                          std::isfinite(reversal[1]) && inClosedRange(reversal[1], 0.1f, 10.0f),
                      "grainReversalProfile requires an exponent in 0.1...2 and shoulder in 0.1...10");
    grainReversalProfile = reversal;
    grainFogDensity = swiftMax(init.grainFogDensity, 0.0f);
    granularityReadDensity = init.granularityReadDensity
                                 ? *init.granularityReadDensity
                                 : (init.isReversal ? GranularityReadDensity::gross : GranularityReadDensity::net);
    halationStrength = init.halationStrength;
    halationLookScale = swiftMax(init.halationLookScale, 0.0f);
    halationHazeMM = swiftMax(init.halationHazeMM, 0.0f);
    halationProfile = init.halationProfile;
    layeredTransport = init.layeredTransport;
    estimatedHalationProfile = init.estimatedHalationProfile;
    halationReturnMatrix = init.halationReturnMatrix;
    paperMidDensity = init.paperMidDensity;
    reciprocityFailure = init.reciprocityFailure;
    developmentProfile = init.developmentProfile;
    isMonochrome = init.isMonochrome;
    isReversal = init.isReversal;
    isReflectionPrint = init.isReversal && init.isReflectionPrint;
    // A reversal stock is its own positive and never meets a printing medium.
    nativePrintMedium = init.isReversal ? std::nullopt : init.nativePrintMedium;
}

void FilmStock::setCouplerGeometry(std::optional<CouplerGeometry> geometry) {
    couplerGeometry_ = std::move(geometry);
    if (couplerGeometry_) couplerInhibition = couplerGeometry_->matrix();
}

std::optional<HalationProfile> FilmStock::resolvedHalationProfile(bool useEstimate) const {
    if (halationProfile) return halationProfile;
    return useEstimate ? estimatedHalationProfile : std::nullopt;
}

std::vector<std::vector<float>> FilmStock::rowNormalized(const std::vector<std::vector<float>>& m) {
    std::vector<std::vector<float>> out;
    out.reserve(m.size());
    for (const auto& row : m) {
        float s = 0;
        for (float v : row) s = s + v;
        std::vector<float> normalized(row.size());
        for (size_t i = 0; i < row.size(); ++i) normalized[i] = row[i] / s;
        out.push_back(std::move(normalized));
    }
    return out;
}

float FilmStock::grainClumpsPerMM2() const {
    float coverage = swiftMin(swiftMax(grainAnchorCoverage(), 1e-4f), 0.99f);
    return -std::log(1 - coverage) / (kFloatPi * grainSizeMM * grainSizeMM);
}

float FilmStock::grainAnchorCoverage() const {
    float anchor = granularityAnchorDensity(1) + grainFogDensity;
    return 1 - std::pow(10.0f, -swiftMax(anchor, 1e-3f));
}

float FilmStock::granularityApertureResponse(float sigma) {
    if (!(sigma > 0)) return 1;
    float z = granularityApertureRadiusMM * granularityApertureRadiusMM / (2 * sigma * sigma);
    float lost = scaledBesselI0(z) + scaledBesselI1(z);
    return std::sqrt(swiftMax(1 - lost, 1e-4f));
}

float FilmStock::granularityAnchorDensity(int layer) const {
    const CharacteristicCurve& curve = curves[layer];
    float net = granularityReadDensity == GranularityReadDensity::gross ? 1 - curve.dMin : 1.0f;
    float range = curve.dMax() - curve.dMin;
    if (!(range > 0)) return 0.5f;
    return swiftMin(swiftMax(net, 0.05f * range), 0.9f * range);
}

float FilmStock::granularityAnchorActivation(int layer) const {
    float range = curves[layer].dMax() - curves[layer].dMin;
    if (!(range > 0)) return 0.5f;
    return granularityAnchorDensity(layer) / range;
}

float FilmStock::dyeCloudGranularityVariance(int layer, float density) const {
    return grainDensityProfile.variance(layer, density);
}

float FilmStock::silverGranularityVariance(float density) const {
    return density * std::pow(10.0f, 0.21004f * density + 0.06114f * density * density);
}

float FilmStock::reversalGranularityVariance(float density) const {
    float power = std::pow(density / grainReversalProfile[1], 2 * grainReversalProfile[0]);
    return power / (1 + power);
}

float FilmStock::grainDensityModulation(int layer, float netDensity) const {
    float anchor = granularityAnchorDensity(layer) + grainFogDensity;
    float here = swiftMax(netDensity, 0.0f) + grainFogDensity;
    if (!(anchor > 0)) return 0;
    float ratio = 0;
    switch (grainDensityLaw) {
        case GrainDensityLaw::dyeCloud:
            ratio = dyeCloudGranularityVariance(layer, here) /
                    swiftMax(dyeCloudGranularityVariance(layer, anchor), 1e-6f);
            break;
        case GrainDensityLaw::silver:
            ratio = silverGranularityVariance(here) / swiftMax(silverGranularityVariance(anchor), 1e-6f);
            break;
        case GrainDensityLaw::dyeCloudSelwyn:
            ratio = here / anchor;
            break;
        case GrainDensityLaw::dyeCloudReversal:
            ratio = reversalGranularityVariance(here) / swiftMax(reversalGranularityVariance(anchor), 1e-20f);
            break;
    }
    return std::sqrt(swiftMax(ratio, 0.0f));
}

float FilmStock::developedDensity(int layer, float logExposure) const {
    const CharacteristicCurve& curve = curves[layer];
    float formed = curve.density(logExposure);
    return isReversal ? curve.dMin + curve.dMax() - formed : formed;
}

const FilmStock& FilmStock::noFilm() {
    static const FilmStock stock = [] {
        CharacteristicCurve neutral(0, 1, -2, 0.2f, 2, 0.2f);
        Init init;
        init.name = "No film";
        init.sensitivity = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        init.curves = {neutral, neutral, neutral};
        init.couplerInhibition = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        init.couplerDiffusionMM = 0;
        init.grainStrength = 0;
        init.grainSizeMM = 0;
        init.grainLayerWeights = {1, 1, 1};
        init.crystalGrainPopulation = CrystalGrainPopulation(6, {0.25f, 0.25f, 0.25f, 0.25f}, 1);
        init.halationStrength = {0, 0, 0};
        init.paperCurve = CharacteristicCurve(0, 1, -2, 0.2f, 2, 0.2f);
        return FilmStock(init);
    }();
    return stock;
}

// ---- printingContrastScale: completed by the spectral/print layer ----

namespace SpectralRuntime {
/// Defined by the spectral layer (SpectralModel.swift `SpectralRuntime.neutralPrintingBalance`).
extern std::vector<float> neutralPrintingBalance(const FilmStock& stock, PrintPaper paper);
}  // namespace SpectralRuntime

std::vector<float> FilmStock::printingContrastScale(float correction, PrintPaper paper) const {
    if (isMonochrome || !acceptsPrintCorrection(paper)) return {1, 1, 1};
    // A reference-anchored medium's profile was solved once, on the reference stock.
    std::vector<float> balance = isReferenceAnchored(paper) ? labScanReferenceBalance()
                                                            : SpectralRuntime::neutralPrintingBalance(*this, paper);
    std::vector<float> out(balance.size());
    for (size_t i = 0; i < balance.size(); ++i) out[i] = 1 + correction * (balance[i] - 1);
    return out;
}

}  // namespace fotufilm
