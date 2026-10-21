// Port of Sources/FotufilmCore/FilmStockPack.swift: FilmStockDefinition decoding, `stock`,
// `FilmStock.decoded(fromPackJSON:)` and the id-keyed lookups.
#include "FilmStockPack.h"

#include <algorithm>

namespace fotufilm {

using json::join;

namespace {

FilmDyeFamily decodeDyeFamily(const JsonValue& o, const std::string& key, const std::string& path) {
    std::string raw = json::requiredString(o, key, path);
    auto family = filmDyeFamilyFromRawValue(raw);
    if (!family) {
        throw DecodingError("dataCorrupted at " + join(path, key) +
                            ": Cannot initialize FilmDyeFamily from invalid String value " + raw);
    }
    return *family;
}

std::vector<FilmStockDefinition::CurveSpec> decodeCurveArray(const JsonValue& value, const std::string& path) {
    if (!value.isArray()) throw DecodingError("typeMismatch: expected array at " + path);
    std::vector<FilmStockDefinition::CurveSpec> out;
    for (size_t i = 0; i < value.array.size(); ++i) {
        out.push_back(FilmStockDefinition::CurveSpec::decode(value.array[i], json::index(path, i)));
    }
    return out;
}

}  // namespace

// ---- CurveSpec ----

CharacteristicCurveComponent FilmStockDefinition::CurveSpec::ComponentSpec::component() const {
    return CharacteristicCurveComponent(gamma, toe, toeWidth, shoulder, shoulderWidth);
}

CharacteristicCurve FilmStockDefinition::CurveSpec::curve() const {
    std::optional<CharacteristicCurveComponent> second;
    if (secondary) second = secondary->component();
    return CharacteristicCurve(dMin, gamma, toe, toeWidth, shoulder, shoulderWidth, second, sampled);
}

FilmStockDefinition::CurveSpec FilmStockDefinition::CurveSpec::decode(const JsonValue& value,
                                                                      const std::string& path) {
    const JsonValue& o = json::object(value, path);
    CurveSpec spec;
    spec.dMin = json::requiredFloat(o, "dMin", path);
    spec.gamma = json::requiredFloat(o, "gamma", path);
    spec.toe = json::requiredFloat(o, "toe", path);
    spec.toeWidth = json::requiredFloat(o, "toeWidth", path);
    spec.shoulder = json::requiredFloat(o, "shoulder", path);
    spec.shoulderWidth = json::requiredFloat(o, "shoulderWidth", path);
    if (const JsonValue* s = json::optional(o, "secondary")) {
        std::string sp = join(path, "secondary");
        const JsonValue& so = json::object(*s, sp);
        ComponentSpec component;
        component.gamma = json::requiredFloat(so, "gamma", sp);
        component.toe = json::requiredFloat(so, "toe", sp);
        component.toeWidth = json::requiredFloat(so, "toeWidth", sp);
        component.shoulder = json::requiredFloat(so, "shoulder", sp);
        component.shoulderWidth = json::requiredFloat(so, "shoulderWidth", sp);
        spec.secondary = component;
    }
    if (const JsonValue* s = json::optional(o, "sampled")) {
        spec.sampled = SampledCharacteristicCurve::decode(*s, join(path, "sampled"));
    }
    return spec;
}

// ---- DonorLayerSpec ----

DonorCaptureLayer FilmStockDefinition::DonorLayerSpec::layer() const {
    return DonorCaptureLayer{name, sensitivity, curve.curve(), inhibition, releaseGamma ? *releaseGamma : 1.0f,
                             depthUM};
}

// ---- DevelopmentSpec ----

std::optional<FilmDevelopmentBasis> FilmStockDefinition::DevelopmentSpec::ConditionSpec::resolvedBasis() const {
    if (basis == "measured" && !basisStock) return FilmDevelopmentBasis::measured();
    if (basis == "transferred" && basisStock) return FilmDevelopmentBasis::transferred(*basisStock);
    if (basis == "estimated" && !basisStock) return FilmDevelopmentBasis::estimated();
    return std::nullopt;
}

FilmDevelopmentCondition FilmStockDefinition::DevelopmentSpec::ConditionSpec::condition() const {
    FilmDevelopmentCondition c;
    c.stops = stops;
    c.label = label;
    c.timeMinutes = timeMinutes;
    c.exposureIndex = exposureIndex;
    auto resolved = resolvedBasis();
    c.basis = resolved ? *resolved : FilmDevelopmentBasis::estimated();
    for (const auto& spec : curves) c.curves.push_back(spec.curve());
    if (donorCurves) {
        for (const auto& spec : *donorCurves) c.donorCurves.push_back(spec.curve());
    }
    c.grainStrength = grainStrength;
    c.grainSizeMM = grainSizeMM;
    c.grainLayerWeights = grainLayerWeights;
    c.grainFogDensity = grainFogDensity;
    c.adjacencyStrength = adjacencyStrength;
    return c;
}

FilmDevelopmentProfile FilmStockDefinition::DevelopmentSpec::profile() const {
    FilmDevelopmentProfile p;
    p.developer = developer;
    p.dilution = dilution;
    p.temperatureC = temperatureC;
    p.agitation = agitation;
    p.source = source;
    p.sourcePage = sourcePage;
    for (const auto& spec : conditions) p.conditions.push_back(spec.condition());
    return p;
}

// ---- SpectralSpec ----

FilmSpectralProfile FilmStockDefinition::SpectralSpec::profile() const {
    switch (kind) {
        case Kind::separatedBase:
            return FilmSpectralProfile(layerSensitivity, imageDyeDensity, minimumDensity);
        case Kind::samples:
            return FilmSpectralProfile(layerSensitivity, imageDyeDensity);
        case Kind::measured:
            return FilmSpectralProfile::measured(layerSensitivity, dyeFamily);
        case Kind::color:
            if (widthsNM) return FilmSpectralProfile::color(peaksNM, *widthsNM, dyeFamily);
            return FilmSpectralProfile::color(peaksNM, dyeFamily);
        case Kind::monochrome:
            return FilmSpectralProfile::monochrome(rgbWeights);
    }
    throw std::runtime_error("unreachable spectral kind");
}

FilmStockDefinition::SpectralSpec FilmStockDefinition::SpectralSpec::decode(const JsonValue& value,
                                                                            const std::string& path) {
    const JsonValue& c = json::object(value, path);
    std::string kind = json::requiredString(c, "kind", path);
    SpectralSpec spec;
    if (kind == "separatedBase") {
        spec.kind = Kind::separatedBase;
        spec.layerSensitivity = json::requiredFloatMatrix(c, "layerSensitivity", path);
        spec.imageDyeDensity = json::requiredFloatMatrix(c, "imageDyeDensity", path);
        spec.minimumDensity = json::requiredFloatArray(c, "minimumDensity", path);
    } else if (kind == "samples") {
        spec.kind = Kind::samples;
        spec.layerSensitivity = json::requiredFloatMatrix(c, "layerSensitivity", path);
        spec.imageDyeDensity = json::requiredFloatMatrix(c, "imageDyeDensity", path);
    } else if (kind == "measured") {
        spec.kind = Kind::measured;
        spec.layerSensitivity = json::requiredFloatMatrix(c, "layerSensitivity", path);
        spec.dyeFamily = decodeDyeFamily(c, "dyeFamily", path);
    } else if (kind == "color") {
        spec.kind = Kind::color;
        spec.peaksNM = json::requiredFloatArray(c, "peaksNM", path);
        spec.widthsNM = json::optionalFloatArray(c, "widthsNM", path);
        spec.dyeFamily = decodeDyeFamily(c, "dyeFamily", path);
    } else if (kind == "monochrome") {
        spec.kind = Kind::monochrome;
        spec.rgbWeights = json::requiredFloatArray(c, "rgbWeights", path);
    } else {
        throw DecodingError("dataCorrupted at " + join(path, "kind") + ": unknown spectral kind '" + kind +
                            "'; expected samples, separatedBase, measured, color or monochrome");
    }
    return spec;
}

// ---- FilmStockDefinition ----

FilmStockDefinition FilmStockDefinition::decode(const JsonValue& value) {
    const std::string path = "";
    const JsonValue& o = json::object(value, "<root>");
    FilmStockDefinition d;
    d.schemaVersion = json::asInt(json::required(o, "schemaVersion", path), "schemaVersion");
    d.id = json::requiredString(o, "id", path);
    d.name = json::requiredString(o, "name", path);
    d.subtitle = json::optionalString(o, "subtitle", path);
    d.nativeFormatID = json::optionalString(o, "nativeFormatID", path);
    d.referenceIlluminantKelvin = json::optionalFloat(o, "referenceIlluminantKelvin", path);
    d.sensitivity = json::requiredFloatMatrix(o, "sensitivity", path);
    d.spectral = SpectralSpec::decode(json::required(o, "spectral", path), "spectral");
    d.sensitivityFloor = json::optionalFloat(o, "sensitivityFloor", path);
    d.curves = decodeCurveArray(json::required(o, "curves", path), "curves");
    if (const JsonValue* v = json::optional(o, "donorLayers")) {
        if (!v->isArray()) throw DecodingError("typeMismatch: expected array at donorLayers");
        std::vector<DonorLayerSpec> donors;
        for (size_t i = 0; i < v->array.size(); ++i) {
            std::string p = json::index("donorLayers", i);
            const JsonValue& lo = json::object(v->array[i], p);
            DonorLayerSpec spec;
            spec.name = json::requiredString(lo, "name", p);
            spec.sensitivity = json::requiredFloatArray(lo, "sensitivity", p);
            spec.curve = CurveSpec::decode(json::required(lo, "curve", p), join(p, "curve"));
            spec.inhibition = json::requiredFloatArray(lo, "inhibition", p);
            spec.releaseGamma = json::optionalFloat(lo, "releaseGamma", p);
            spec.depthUM = json::optionalFloat(lo, "depthUM", p);
            donors.push_back(std::move(spec));
        }
        d.donorLayers = std::move(donors);
    }
    d.flare = json::optionalFloat(o, "flare", path);
    d.emulsionDiffusionMM = json::optionalFloatArray(o, "emulsionDiffusionMM", path);
    d.emulsionDiffusionSecondaryMM = json::optionalFloatArray(o, "emulsionDiffusionSecondaryMM", path);
    d.emulsionDiffusionPrimaryShare = json::optionalFloatArray(o, "emulsionDiffusionPrimaryShare", path);
    d.mtfLumaShare = json::optionalFloat(o, "mtfLumaShare", path);
    d.lumaDiffusionMM = json::optionalFloat(o, "lumaDiffusionMM", path);
    d.couplerInhibition = json::requiredFloatMatrix(o, "couplerInhibition", path);
    d.couplerReleaseGamma = json::optionalFloatArray(o, "couplerReleaseGamma", path);
    if (const JsonValue* v = json::optional(o, "couplerGeometry")) {
        d.couplerGeometry = CouplerGeometry::decode(*v, "couplerGeometry");
    }
    d.couplerDiffusionMM = json::requiredFloat(o, "couplerDiffusionMM", path);
    d.chromaticFringeAmount = json::optionalFloat(o, "chromaticFringeAmount", path);
    d.chromaticFringeRadiusMM = json::optionalFloat(o, "chromaticFringeRadiusMM", path);
    d.adjacencyStrength = json::optionalFloat(o, "adjacencyStrength", path);
    if (auto raw = json::optionalString(o, "adjacencyModel", path)) {
        auto model = adjacencyModelFromRawValue(*raw);
        if (!model) {
            throw DecodingError("dataCorrupted at adjacencyModel: Cannot initialize AdjacencyModel from invalid "
                                "String value " + *raw);
        }
        d.adjacencyModel = *model;
    }
    d.adjacencyRadiusMM = json::optionalFloat(o, "adjacencyRadiusMM", path);
    d.grainStrength = json::requiredFloat(o, "grainStrength", path);
    d.grainSizeMM = json::requiredFloat(o, "grainSizeMM", path);
    d.grainLayerWeights = json::requiredFloatArray(o, "grainLayerWeights", path);
    d.grainLumaCorrelation = json::optionalFloat(o, "grainLumaCorrelation", path);
    d.grainMottleShare = json::optionalFloat(o, "grainMottleShare", path);
    d.grainMottleSizeRatio = json::optionalFloat(o, "grainMottleSizeRatio", path);
    d.grainLayerSizeRatio = json::optionalFloatArray(o, "grainLayerSizeRatio", path);
    if (const JsonValue* v = json::optional(o, "grainDensityLaw")) {
        int32_t raw = json::asInt32(*v, "grainDensityLaw");
        auto law = grainDensityLawFromRawValue(raw);
        if (!law) {
            throw DecodingError("dataCorrupted at grainDensityLaw: Cannot initialize GrainDensityLaw from invalid "
                                "Int32 value " + std::to_string(raw));
        }
        d.grainDensityLaw = *law;
    }
    if (const JsonValue* v = json::optional(o, "grainDensityProfile")) {
        d.grainDensityProfile = GrainDensityProfile::decode(*v, "grainDensityProfile");
    }
    d.crystalGrainPopulation =
        CrystalGrainPopulation::decode(json::required(o, "crystalGrainPopulation", path), "crystalGrainPopulation");
    d.grainReversalProfile = json::optionalFloatArray(o, "grainReversalProfile", path);
    d.grainFogDensity = json::optionalFloat(o, "grainFogDensity", path);
    if (auto raw = json::optionalString(o, "granularityReadDensity", path)) {
        auto density = granularityReadDensityFromRawValue(*raw);
        if (!density) {
            throw DecodingError("dataCorrupted at granularityReadDensity: Cannot initialize GranularityReadDensity "
                                "from invalid String value " + *raw);
        }
        d.granularityReadDensity = *density;
    }
    d.halationStrength = json::requiredFloatArray(o, "halationStrength", path);
    d.halationLookScale = json::optionalFloat(o, "halationLookScale", path);
    d.halationHazeMM = json::optionalFloat(o, "halationHazeMM", path);
    if (const JsonValue* v = json::optional(o, "halationProfile")) {
        d.halationProfile = HalationProfile::decode(*v, "halationProfile");
    }
    if (const JsonValue* v = json::optional(o, "layeredTransport")) {
        d.layeredTransport = LayeredTransport::decode(*v, "layeredTransport");
    }
    if (const JsonValue* v = json::optional(o, "estimatedHalationProfile")) {
        d.estimatedHalationProfile = HalationProfile::decode(*v, "estimatedHalationProfile");
    }
    d.halationReturnMatrix = json::optionalFloatMatrix(o, "halationReturnMatrix", path);
    d.paperCurve = CurveSpec::decode(json::required(o, "paperCurve", path), "paperCurve");
    d.paperMidDensity = json::optionalFloat(o, "paperMidDensity", path);
    if (const JsonValue* v = json::optional(o, "reciprocityFailure")) {
        d.reciprocityFailure = ReciprocityFailure::decode(*v, "reciprocityFailure");
    }
    if (const JsonValue* v = json::optional(o, "development")) {
        const std::string dp = "development";
        const JsonValue& dv = json::object(*v, dp);
        DevelopmentSpec spec;
        spec.developer = json::requiredString(dv, "developer", dp);
        spec.dilution = json::optionalString(dv, "dilution", dp);
        spec.temperatureC = json::requiredFloat(dv, "temperatureC", dp);
        spec.agitation = json::requiredString(dv, "agitation", dp);
        spec.source = json::requiredString(dv, "source", dp);
        spec.sourcePage = json::asInt(json::required(dv, "sourcePage", dp), join(dp, "sourcePage"));
        const JsonValue& conditions = json::required(dv, "conditions", dp);
        std::string cp = join(dp, "conditions");
        if (!conditions.isArray()) throw DecodingError("typeMismatch: expected array at " + cp);
        for (size_t i = 0; i < conditions.array.size(); ++i) {
            std::string p = json::index(cp, i);
            const JsonValue& co = json::object(conditions.array[i], p);
            DevelopmentSpec::ConditionSpec c;
            c.stops = json::requiredFloat(co, "stops", p);
            c.label = json::requiredString(co, "label", p);
            c.timeMinutes = json::requiredFloat(co, "timeMinutes", p);
            c.exposureIndex = json::optionalFloat(co, "exposureIndex", p);
            c.basis = json::requiredString(co, "basis", p);
            c.basisStock = json::optionalString(co, "basisStock", p);
            c.curves = decodeCurveArray(json::required(co, "curves", p), join(p, "curves"));
            if (const JsonValue* dc = json::optional(co, "donorCurves")) {
                c.donorCurves = decodeCurveArray(*dc, join(p, "donorCurves"));
            }
            c.grainStrength = json::optionalFloat(co, "grainStrength", p);
            c.grainSizeMM = json::optionalFloat(co, "grainSizeMM", p);
            c.grainLayerWeights = json::optionalFloatArray(co, "grainLayerWeights", p);
            c.grainFogDensity = json::optionalFloat(co, "grainFogDensity", p);
            c.adjacencyStrength = json::optionalFloat(co, "adjacencyStrength", p);
            spec.conditions.push_back(std::move(c));
        }
        d.development = std::move(spec);
    }
    d.isMonochrome = json::optionalBool(o, "isMonochrome", path);
    d.isReversal = json::optionalBool(o, "isReversal", path);
    d.isExample = json::optionalBool(o, "isExample", path);
    d.isReflectionPrint = json::optionalBool(o, "isReflectionPrint", path);
    d.nativePrintMedium = json::optionalString(o, "nativePrintMedium", path);
    d.spectralLineage = json::optionalString(o, "spectralLineage", path);
    return d;
}

FilmStock FilmStockDefinition::stock() const {
    FilmStock::Init init;
    init.name = name;
    init.sensitivity = sensitivity;
    init.referenceIlluminantKelvin = referenceIlluminantKelvin ? *referenceIlluminantKelvin : 5500.0f;
    init.spectralProfile = spectral.profile().floored(sensitivityFloor ? *sensitivityFloor : 0.0f);
    for (const auto& spec : curves) init.curves.push_back(spec.curve());
    if (donorLayers) {
        for (const auto& spec : *donorLayers) init.donorLayers.push_back(spec.layer());
    }
    init.flare = flare ? *flare : 0.008f;
    init.emulsionDiffusionMM = emulsionDiffusionMM ? *emulsionDiffusionMM : std::vector<float>{0, 0, 0};
    init.emulsionDiffusionSecondaryMM =
        emulsionDiffusionSecondaryMM ? *emulsionDiffusionSecondaryMM : std::vector<float>{0, 0, 0};
    init.emulsionDiffusionPrimaryShare =
        emulsionDiffusionPrimaryShare ? *emulsionDiffusionPrimaryShare : std::vector<float>{1, 1, 1};
    init.mtfLumaShare = mtfLumaShare ? *mtfLumaShare : 0.0f;
    init.lumaDiffusionMM = lumaDiffusionMM;
    init.couplerInhibition = couplerInhibition;
    init.couplerReleaseGamma = couplerReleaseGamma ? *couplerReleaseGamma : std::vector<float>{1, 1, 1};
    init.couplerGeometry = couplerGeometry;
    init.couplerDiffusionMM = couplerDiffusionMM;
    init.chromaticFringeAmount = chromaticFringeAmount ? *chromaticFringeAmount : 0.0f;
    init.chromaticFringeRadiusMM = chromaticFringeRadiusMM ? *chromaticFringeRadiusMM : 0.1f;
    init.adjacencyStrength = adjacencyStrength ? *adjacencyStrength : 0.0f;
    init.adjacencyModel = adjacencyModel ? *adjacencyModel : AdjacencyModel::gaussian;
    init.adjacencyRadiusMM = adjacencyRadiusMM ? *adjacencyRadiusMM : 0.0f;
    init.grainStrength = grainStrength;
    init.grainSizeMM = grainSizeMM;
    init.grainLayerWeights = grainLayerWeights;
    init.grainLumaCorrelation = grainLumaCorrelation ? *grainLumaCorrelation : 0.0f;
    init.grainMottleShare = grainMottleShare ? *grainMottleShare : 0.0f;
    init.grainMottleSizeRatio = grainMottleSizeRatio ? *grainMottleSizeRatio : 3.0f;
    init.grainLayerSizeRatio = grainLayerSizeRatio ? *grainLayerSizeRatio : std::vector<float>{1, 1, 1};
    init.grainDensityLaw = grainDensityLaw;
    init.grainDensityProfile = grainDensityProfile;
    init.crystalGrainPopulation = crystalGrainPopulation;
    init.grainReversalProfile = grainReversalProfile ? *grainReversalProfile : FilmStock::defaultGrainReversalProfile();
    init.grainFogDensity = grainFogDensity ? *grainFogDensity : FilmStock::defaultGrainFogDensity;
    init.granularityReadDensity = granularityReadDensity;
    init.halationStrength = halationStrength;
    init.halationLookScale = halationLookScale ? *halationLookScale : 1.0f;
    init.halationHazeMM = halationHazeMM ? *halationHazeMM : 0.0f;
    init.halationProfile = halationProfile;
    init.layeredTransport = layeredTransport;
    init.estimatedHalationProfile = estimatedHalationProfile;
    init.halationReturnMatrix = halationReturnMatrix;
    init.paperCurve = paperCurve.curve();
    init.paperMidDensity = paperMidDensity ? *paperMidDensity : 0.744f;
    init.reciprocityFailure = reciprocityFailure;
    if (development) init.developmentProfile = development->profile();
    init.isMonochrome = isMonochrome.value_or(false);
    init.isReversal = isReversal.value_or(false);
    init.isReflectionPrint = isReflectionPrint.value_or(false);
    if (nativePrintMedium) init.nativePrintMedium = printPaperPreset(*nativePrintMedium);
    return FilmStock(init);
}

FilmStockDefinition decodeFilmStockDefinition(const std::string& json) {
    FilmStockDefinition definition = FilmStockDefinition::decode(parseJson(json));
    definition.validate();
    return definition;
}

FilmStock decodeFilmStock(const std::string& json) { return decodeFilmStockDefinition(json).stock(); }

// ---- FilmStockRegistry ----

FilmStockRegistry& FilmStockRegistry::shared() {
    static FilmStockRegistry registry;
    return registry;
}

std::string FilmStockRegistry::registerJSON(const std::string& json) {
    FilmStockDefinition definition = decodeFilmStockDefinition(json);
    FilmStock stock = definition.stock();
    std::string id = definition.id;
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(id);
    entries_.emplace(id, Entry{std::move(definition), std::move(stock)});
    return id;
}

void FilmStockRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

std::optional<FilmStock> FilmStockRegistry::named(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return std::nullopt;
    return found->second.stock;
}

std::optional<FilmStockDefinition> FilmStockRegistry::definition(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return std::nullopt;
    return found->second.definition;
}

std::optional<std::string> FilmStockRegistry::nativeFormatID(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return std::nullopt;
    return found->second.definition.nativeFormatID;
}

std::vector<std::string> FilmStockRegistry::allPresetIDs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& entry : entries_) ids.push_back(entry.first);  // std::map keeps them sorted
    return ids;
}

std::vector<std::string> FilmStockRegistry::presetIDs() const {
    std::vector<std::string> all = allPresetIDs();
    std::vector<std::string> installed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& id : all) {
            auto found = entries_.find(id);
            if (found != entries_.end() && found->second.definition.isExample != std::optional<bool>(true)) {
                installed.push_back(id);
            }
        }
    }
    return installed.empty() ? all : installed;
}

std::optional<FilmStock> FilmStockRegistry::defaultStock() const {
    std::vector<std::string> ids = presetIDs();
    if (ids.empty()) return std::nullopt;
    return named(ids.front());
}

}  // namespace fotufilm
