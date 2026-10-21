// Port of Sources/FotufilmCore/LayeredTransport.swift: data type, decoding and validation.
#include "LayeredTransport.h"

#include <cmath>
#include <set>

#include "Simd.h"
#include "SpectralGrid.h"

namespace fotufilm {

void LayeredTransport::validate() const {
    auto require = [](bool condition, const std::string& text) {
        if (!condition) throw TransportError(text);
    };
    auto spectrum = [&](const std::vector<double>& values, double lo, double hi, const std::string& name) {
        require(values.size() == 1 || values.size() == static_cast<size_t>(SpectralGrid::count),
                name + " must have one or " + std::to_string(SpectralGrid::count) + " samples");
        bool ok = true;
        for (double v : values) ok = ok && std::isfinite(v) && inClosedRange(v, lo, hi);
        require(ok, name + " contains an invalid value");
    };
    require(revision == 1 && kind == "conditional-launch", "unsupported transport revision/kind");
    require(!constructionID.empty() && constructionID.size() <= 128, "invalid construction ID");
    std::vector<double> grid;
    for (float w : SpectralGrid::wavelengths()) grid.push_back(static_cast<double>(w));
    require(wavelengthsNM == grid, "transport wavelength grid mismatch");
    require(!layers.empty() && layers.size() <= 16, "expected 1...16 optical layers");
    std::set<std::string> ids;
    for (const auto& layer : layers) ids.insert(layer.id);
    require(ids.size() == layers.size(), "duplicate optical layer IDs");
    for (const auto& layer : layers) {
        require(!layer.id.empty() && layer.id.size() <= 128, "invalid optical layer ID");
        require(std::isfinite(layer.thicknessMM) && layer.thicknessMM > 0 && layer.thicknessMM <= 2,
                "invalid layer thickness");
        spectrum(layer.refractiveIndex, 1, 4, "refractiveIndex");
        spectrum(layer.absorptionPerMM, 0, 100000, "absorptionPerMM");
    }
    spectrum(frontIndex, 1, 4, "frontIndex");
    spectrum(rearIndex, 1, 4, "rearIndex");
    if (rearReflectance) spectrum(*rearReflectance, 0, 1, "rearReflectance");
    if (rearPlateReflectance) {
        require(!rearReflectance, "an opaque backing leaves no rear surface for a plate");
        spectrum(*rearPlateReflectance, 0, 1, "rearPlateReflectance");
    }
    double thickness = 0;
    for (const auto& layer : layers) thickness = thickness + layer.thicknessMM;
    bool depthsInside = recordDepthMM.size() == 3;
    for (double d : recordDepthMM) depthsInside = depthsInside && std::isfinite(d) && d > 0 && d < thickness;
    require(depthsInside, "receiver planes must lie inside the stack");
    bool coreValid = coreSigmaMM.size() == 3;
    for (double s : coreSigmaMM) coreValid = coreValid && std::isfinite(s) && inClosedRange(s, 0.0, 0.5);
    require(coreValid, "invalid core sigma");
    struct RowSet {
        const std::vector<std::vector<double>>* rows;
        double lo, hi;
        const char* name;
    };
    const RowSet sets[] = {{&angularExponent, 0.0, 32.0, "angularExponent"},
                           {&captureProbability, 0.001, 1.0, "captureProbability"},
                           {&returnedToDirect, 0.0, 100.0, "returnedToDirect"}};
    for (const auto& set : sets) {
        require(set.rows->size() == 3, std::string(set.name) + " needs three receiver rows");
        for (const auto& row : *set.rows) spectrum(row, set.lo, set.hi, set.name);
    }
}

LayeredTransport LayeredTransport::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& o = json::object(value, path);
    LayeredTransport t;
    t.revision = json::asInt(json::required(o, "revision", path), json::join(path, "revision"));
    t.kind = json::requiredString(o, "kind", path);
    t.constructionID = json::requiredString(o, "constructionID", path);
    t.wavelengthsNM = json::asDoubleArray(json::required(o, "wavelengthsNM", path), json::join(path, "wavelengthsNM"));
    const JsonValue& layers = json::required(o, "layers", path);
    std::string layersPath = json::join(path, "layers");
    if (!layers.isArray()) throw DecodingError("typeMismatch: expected array at " + layersPath);
    for (size_t i = 0; i < layers.array.size(); ++i) {
        std::string lp = json::index(layersPath, i);
        const JsonValue& l = json::object(layers.array[i], lp);
        Layer layer;
        layer.id = json::requiredString(l, "id", lp);
        layer.thicknessMM = json::asDouble(json::required(l, "thicknessMM", lp), json::join(lp, "thicknessMM"));
        layer.refractiveIndex =
            json::asDoubleArray(json::required(l, "refractiveIndex", lp), json::join(lp, "refractiveIndex"));
        layer.absorptionPerMM =
            json::asDoubleArray(json::required(l, "absorptionPerMM", lp), json::join(lp, "absorptionPerMM"));
        t.layers.push_back(std::move(layer));
    }
    t.frontIndex = json::asDoubleArray(json::required(o, "frontIndex", path), json::join(path, "frontIndex"));
    t.rearIndex = json::asDoubleArray(json::required(o, "rearIndex", path), json::join(path, "rearIndex"));
    if (const JsonValue* v = json::optional(o, "rearReflectance")) {
        t.rearReflectance = json::asDoubleArray(*v, json::join(path, "rearReflectance"));
    }
    if (const JsonValue* v = json::optional(o, "rearPlateReflectance")) {
        t.rearPlateReflectance = json::asDoubleArray(*v, json::join(path, "rearPlateReflectance"));
    }
    t.recordDepthMM = json::asDoubleArray(json::required(o, "recordDepthMM", path), json::join(path, "recordDepthMM"));
    t.angularExponent =
        json::asDoubleMatrix(json::required(o, "angularExponent", path), json::join(path, "angularExponent"));
    t.captureProbability =
        json::asDoubleMatrix(json::required(o, "captureProbability", path), json::join(path, "captureProbability"));
    t.returnedToDirect =
        json::asDoubleMatrix(json::required(o, "returnedToDirect", path), json::join(path, "returnedToDirect"));
    t.coreSigmaMM = json::asDoubleArray(json::required(o, "coreSigmaMM", path), json::join(path, "coreSigmaMM"));
    return t;
}

bool LayeredTransport::operator==(const LayeredTransport& o) const {
    return revision == o.revision && kind == o.kind && constructionID == o.constructionID &&
           wavelengthsNM == o.wavelengthsNM && layers == o.layers && frontIndex == o.frontIndex &&
           rearIndex == o.rearIndex && rearReflectance == o.rearReflectance &&
           rearPlateReflectance == o.rearPlateReflectance && recordDepthMM == o.recordDepthMM &&
           angularExponent == o.angularExponent && captureProbability == o.captureProbability &&
           returnedToDirect == o.returnedToDirect && coreSigmaMM == o.coreSigmaMM;
}

}  // namespace fotufilm
