// Port of Sources/FotufilmCore/FilmStockValidation.swift. The frame-only `sheetNotches` and
// `edgePrinting` checks are not ported along with those fields.
#include "FilmStockValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "FilmStockPack.h"
#include "Simd.h"
#include "SpectralGrid.h"

namespace fotufilm {

namespace {

/// Swift's Float description: the shortest text that round-trips, with ".0" on whole numbers.
std::string describe(float value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    char buffer[64];
    for (int precision = 1; precision <= 9; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, static_cast<double>(value));
        if (std::strtof(buffer, nullptr) == value) break;
    }
    std::string text = buffer;
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}

std::string describe(const std::vector<float>& values) {
    std::string text = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) text += ", ";
        text += describe(values[i]);
    }
    return text + "]";
}

/// Unicode scalars in a UTF-8 string (Swift `String.count` for text without combining sequences).
size_t characterCount(const std::string& value) {
    size_t count = 0;
    for (unsigned char c : value) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::vector<uint32_t> scalars(const std::string& value) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < value.size();) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        uint32_t code;
        size_t length;
        if (c < 0x80) {
            code = c;
            length = 1;
        } else if ((c >> 5) == 0x6) {
            code = c & 0x1F;
            length = 2;
        } else if ((c >> 4) == 0xE) {
            code = c & 0x0F;
            length = 3;
        } else {
            code = c & 0x07;
            length = 4;
        }
        for (size_t k = 1; k < length && i + k < value.size(); ++k) {
            code = (code << 6) | (static_cast<unsigned char>(value[i + k]) & 0x3F);
        }
        out.push_back(code);
        i += length;
    }
    return out;
}

/// CharacterSet.whitespacesAndNewlines.
bool isWhitespaceOrNewline(uint32_t c) {
    return c == 0x20 || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

struct Checker {
    std::string stockID;

    ValidationFailure fail(const std::string& field, const std::string& reason) const {
        return ValidationFailure((stockID.empty() ? std::string("<no id>") : stockID) + "." + field, reason);
    }

    void check(const std::string& field, float value, float lo, float hi) const {
        if (!std::isfinite(value)) throw fail(field, "is not a finite number");
        if (!inClosedRange(value, lo, hi)) {
            throw fail(field, "is " + describe(value) + "; expected " + describe(lo) + "..." + describe(hi));
        }
    }

    void check(const std::string& field, const std::vector<float>& values, size_t count, float lo, float hi) const {
        if (values.size() != count) {
            throw fail(field, "has " + std::to_string(values.size()) + " entries; expected " + std::to_string(count));
        }
        for (float value : values) check(field, value, lo, hi);
    }

    void check(const std::string& field, const std::vector<std::vector<float>>& rows, size_t rowsCount,
               size_t columns, float lo, float hi) const {
        if (rows.size() != rowsCount) {
            throw fail(field, "has " + std::to_string(rows.size()) + " rows; expected " + std::to_string(rowsCount));
        }
        for (const auto& row : rows) check(field, row, columns, lo, hi);
    }

    void checkIdentifier(const std::string& value, const std::string& field, size_t limit) const {
        if (value.empty()) throw fail(field, "is empty");
        if (characterCount(value) > limit) throw fail(field, "is longer than " + std::to_string(limit) + " characters");
        // CharacterSet.alphanumerics ∪ "-_."; only ASCII identifiers are accepted here.
        for (uint32_t c : scalars(value)) {
            bool allowed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
                           c == '_' || c == '.';
            if (!allowed) throw fail(field, "may hold only letters, digits, '-', '_' and '.'");
        }
    }

    void checkText(const std::optional<std::string>& value, const std::string& field, size_t limit,
                   bool required) const {
        if (!value) {
            if (required) throw fail(field, "is missing");
            return;
        }
        if (required) {
            bool blank = true;
            for (uint32_t c : scalars(*value)) blank = blank && isWhitespaceOrNewline(c);
            if (blank) throw fail(field, "is empty");
        }
        if (characterCount(*value) > limit) {
            throw fail(field, "is longer than " + std::to_string(limit) + " characters");
        }
    }
};

}  // namespace

void FilmStockDefinition::CurveSpec::validate(const std::string& field, const std::string& stockID) const {
    Checker c{stockID};
    const std::pair<const char*, float> values[] = {{"dMin", dMin},         {"gamma", gamma},
                                                    {"toe", toe},           {"toeWidth", toeWidth},
                                                    {"shoulder", shoulder}, {"shoulderWidth", shoulderWidth}};
    for (const auto& entry : values) {
        if (!std::isfinite(entry.second)) throw c.fail(field + "." + entry.first, "is not a finite number");
    }
    if (!inClosedRange(dMin, -1.0f, 6.0f)) throw c.fail(field + ".dMin", "is " + describe(dMin) + "; expected -1...6");
    if (!inClosedRange(gamma, 0.01f, 20.0f)) {
        throw c.fail(field + ".gamma", "is " + describe(gamma) + "; expected 0.01...20");
    }
    if (!(inClosedRange(toe, -12.0f, 12.0f) && inClosedRange(shoulder, -12.0f, 12.0f))) {
        throw c.fail(field, "puts toe or shoulder outside -12...12 log exposure");
    }
    if (!(shoulder > toe)) throw c.fail(field, "has its shoulder at or below its toe");
    if (!(inClosedRange(toeWidth, 0.001f, 20.0f) && inClosedRange(shoulderWidth, 0.001f, 20.0f))) {
        throw c.fail(field, "has a toe or shoulder width outside 0.001...20");
    }
    if (secondary) secondary->validate(field + ".secondary", stockID);
    if (sampled) {
        bool ok = true;
        for (float x : sampled->logExposure) ok = ok && inClosedRange(x, -12.0f, 12.0f);
        for (float d : sampled->density) ok = ok && d >= dMin && d <= 20;
        ok = ok && !sampled->density.empty() && sampled->density.front() == dMin;
        if (!ok) throw c.fail(field + ".sampled", "requires bounded densities and a dMin first endpoint");
    }
}

void FilmStockDefinition::CurveSpec::ComponentSpec::validate(const std::string& field,
                                                             const std::string& stockID) const {
    Checker c{stockID};
    const std::pair<const char*, float> values[] = {{"gamma", gamma},
                                                    {"toe", toe},
                                                    {"toeWidth", toeWidth},
                                                    {"shoulder", shoulder},
                                                    {"shoulderWidth", shoulderWidth}};
    for (const auto& entry : values) {
        if (!std::isfinite(entry.second)) throw c.fail(field + "." + entry.first, "is not a finite number");
    }
    if (!inClosedRange(gamma, 0.001f, 20.0f)) {
        throw c.fail(field + ".gamma", "is " + describe(gamma) + "; expected 0.001...20");
    }
    if (!(inClosedRange(toe, -12.0f, 12.0f) && inClosedRange(shoulder, -12.0f, 12.0f) && shoulder > toe)) {
        throw c.fail(field, "has invalid toe/shoulder positions");
    }
    if (!(inClosedRange(toeWidth, 0.001f, 20.0f) && inClosedRange(shoulderWidth, 0.001f, 20.0f))) {
        throw c.fail(field, "has a toe or shoulder width outside 0.001...20");
    }
}

void FilmStockDefinition::SpectralSpec::validate(const std::string& stockID) const {
    Checker c{stockID};
    auto checkSamples = [&](const std::vector<std::vector<float>>& rows, const std::string& field, float lo, float hi,
                            bool needsSignal) {
        if (rows.size() != 3) {
            throw c.fail(field, "has " + std::to_string(rows.size()) + " layers; expected 3, in R, G, B order");
        }
        for (size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            std::string rowField = field + "[" + std::to_string(index) + "]";
            if (row.size() != static_cast<size_t>(SpectralGrid::count)) {
                throw c.fail(rowField, "has " + std::to_string(row.size()) + " samples; expected " +
                                           std::to_string(SpectralGrid::count) + ", 380...780 nm at 5 nm");
            }
            for (float value : row) {
                if (!std::isfinite(value)) throw c.fail(rowField, "holds a value that is not finite");
                if (!inClosedRange(value, lo, hi)) {
                    throw c.fail(rowField, "holds " + describe(value) + "; expected " + describe(lo) + "..." +
                                               describe(hi));
                }
            }
            if (needsSignal && maxOr(row, 0) <= 0) throw c.fail(rowField, "is zero at every wavelength");
        }
    };

    switch (kind) {
        case Kind::separatedBase: {
            checkSamples(layerSensitivity, "spectral.layerSensitivity", 0, 1e4f, true);
            checkSamples(imageDyeDensity, "spectral.imageDyeDensity", 0, 100, false);
            bool ok = minimumDensity.size() == static_cast<size_t>(SpectralGrid::count);
            for (float v : minimumDensity) ok = ok && std::isfinite(v) && inClosedRange(v, 0.0f, 100.0f);
            if (!ok) {
                throw c.fail("spectral.minimumDensity",
                             "requires 81 finite densities in 0...100 on the 380...780 nm grid");
            }
            break;
        }
        case Kind::samples:
            checkSamples(layerSensitivity, "spectral.layerSensitivity", 0, 1e4f, true);
            checkSamples(imageDyeDensity, "spectral.imageDyeDensity", 0, 100, false);
            break;
        case Kind::measured:
            checkSamples(layerSensitivity, "spectral.layerSensitivity", 0, 1e4f, true);
            break;
        case Kind::color:
            if (peaksNM.size() != 3) {
                throw c.fail("spectral.peaksNM", "has " + std::to_string(peaksNM.size()) + " entries; expected 3");
            }
            for (float peak : peaksNM) {
                if (!(std::isfinite(peak) && inClosedRange(peak, 300.0f, 900.0f))) {
                    throw c.fail("spectral.peaksNM", "holds " + describe(peak) + " nm; expected 300...900");
                }
            }
            if (widthsNM) {
                if (widthsNM->size() != 3) {
                    throw c.fail("spectral.widthsNM",
                                 "has " + std::to_string(widthsNM->size()) + " entries; expected 3");
                }
                for (float width : *widthsNM) {
                    if (!(std::isfinite(width) && inClosedRange(width, 1.0f, 400.0f))) {
                        throw c.fail("spectral.widthsNM", "holds " + describe(width) + " nm; expected 1...400");
                    }
                }
            }
            break;
        case Kind::monochrome: {
            if (rgbWeights.size() != 3) {
                throw c.fail("spectral.rgbWeights",
                             "has " + std::to_string(rgbWeights.size()) + " entries; expected 3");
            }
            for (float weight : rgbWeights) {
                if (!(std::isfinite(weight) && inClosedRange(weight, 0.0f, 100.0f))) {
                    throw c.fail("spectral.rgbWeights", "holds " + describe(weight) + "; expected 0...100");
                }
            }
            float sum = 0;
            for (float w : rgbWeights) sum = sum + w;
            if (!(sum > 0)) throw c.fail("spectral.rgbWeights", "sums to zero");
            break;
        }
    }
}

void FilmStockDefinition::validate() const {
    Checker c{id};

    if (!(1 <= schemaVersion && schemaVersion <= layeredTransportSchemaVersion)) {
        throw c.fail("schemaVersion",
                     "is " + std::to_string(schemaVersion) + "; this build requires 1, 2 or 3");
    }
    if ((schemaVersion == 3) != layeredTransport.has_value()) {
        throw c.fail("layeredTransport",
                     "layered transport requires schema 3 and schema 3 requires a transport definition");
    }
    if (layeredTransport) {
        try {
            layeredTransport->validate();
        } catch (const TransportError& error) {
            throw c.fail("layeredTransport", error.what());
        }
    }
    c.checkIdentifier(id, "id", 64);
    c.checkText(name, "name", 64, true);
    c.checkText(subtitle, "subtitle", 128, false);
    if (nativeFormatID) c.checkIdentifier(*nativeFormatID, "nativeFormatID", 32);
    c.check("referenceIlluminantKelvin", referenceIlluminantKelvin ? *referenceIlluminantKelvin : 5500.0f, 2000, 12000);
    if (spectralLineage) c.checkIdentifier(*spectralLineage, "spectralLineage", 130);

    c.check("sensitivity", sensitivity, 3, 3, -10, 10);
    for (size_t index = 0; index < sensitivity.size(); ++index) {
        float sum = 0;
        for (float v : sensitivity[index]) sum = sum + v;
        if (std::fabs(sum) < 1e-6f) {
            throw c.fail("sensitivity", "row " + std::to_string(index) + " sums to zero and cannot be normalised");
        }
    }

    spectral.validate(id);

    // `curves` declares how many capture layers the emulsion has.
    const int layers = static_cast<int>(curves.size());
    if (!(FilmStock::supportedCaptureLayerMin <= layers && layers <= FilmStock::supportedCaptureLayerMax)) {
        std::string supported =
            FilmStock::supportedCaptureLayerMin == FilmStock::supportedCaptureLayerMax
                ? std::to_string(FilmStock::supportedCaptureLayerMin)
                : std::to_string(FilmStock::supportedCaptureLayerMin) + "-" +
                      std::to_string(FilmStock::supportedCaptureLayerMax);
        throw c.fail("curves", "declares " + std::to_string(layers) +
                                   " dye-forming capture layers; this build renders " + supported +
                                   ". A layer that develops without forming a dye (REALA 500D's 4th Color Layer, "
                                   "say) belongs in donorLayers; a fourth *dye-forming* layer needs an N-to-dye "
                                   "forming step and an image pipeline that carries N planes; see "
                                   "FilmStock.supportedCaptureLayerCounts");
    }
    for (size_t index = 0; index < curves.size(); ++index) {
        if (curves[index].sampled && schemaVersion < 2) {
            throw c.fail("schemaVersion", "sampled characteristic curves require schema version 2");
        }
        curves[index].validate("curves[" + std::to_string(index) + "]", id);
    }

    const std::vector<DonorLayerSpec> donors = donorLayers ? *donorLayers : std::vector<DonorLayerSpec>{};
    const int donorCount = static_cast<int>(donors.size());
    if (!(FilmStock::supportedDonorLayerMin <= donorCount && donorCount <= FilmStock::supportedDonorLayerMax)) {
        throw c.fail("donorLayers", "declares " + std::to_string(donorCount) +
                                        " donor layers; the packed kernel configuration carries at most " +
                                        std::to_string(FilmStock::supportedDonorLayerMax) +
                                        "; see FilmStock.supportedDonorLayerCounts");
    }
    for (size_t index = 0; index < donors.size(); ++index) {
        const DonorLayerSpec& donor = donors[index];
        std::string field = "donorLayers[" + std::to_string(index) + "]";
        c.checkText(donor.name, field + ".name", 16, true);
        if (donor.sensitivity.size() != static_cast<size_t>(SpectralGrid::count)) {
            throw c.fail(field + ".sensitivity", "has " + std::to_string(donor.sensitivity.size()) +
                                                     " samples; expected " + std::to_string(SpectralGrid::count) +
                                                     ", 380...780 nm at 5 nm");
        }
        for (float value : donor.sensitivity) {
            if (!(std::isfinite(value) && inClosedRange(value, 0.0f, 1e4f))) {
                throw c.fail(field + ".sensitivity", "holds " + describe(value) + "; expected 0...10000.0");
            }
        }
        if (!(maxOr(donor.sensitivity, 0) > 0)) throw c.fail(field + ".sensitivity", "is zero at every wavelength");
        donor.curve.validate(field + ".curve", id);
        if (donor.curve.sampled) {
            throw c.fail(field + ".curve.sampled", "sampled records are supported only for dye-forming film curves");
        }
        // The same bound as `couplerInhibition`.
        c.check(field + ".inhibition", donor.inhibition, static_cast<size_t>(layers), 0, 4);
        c.check(field + ".releaseGamma", donor.releaseGamma ? *donor.releaseGamma : 1.0f, 0.25f, 4);
        if (donor.depthUM) c.check(field + ".depthUM", *donor.depthUM, -1000, 1000);
    }
    paperCurve.validate("paperCurve", id);
    if (paperCurve.sampled) {
        throw c.fail("paperCurve.sampled", "sampled records are supported only for dye-forming film curves");
    }
    // Bound the density scale too: 3.0 clears the measured sheet's 2.10 and every carried value.
    float paperRange = paperCurve.gamma * (paperCurve.shoulder - paperCurve.toe);
    if (!(paperRange <= 3.0f)) {
        throw c.fail("paperCurve", "spans " + describe(paperRange) + " D above base; expected at most 3.0");
    }
    c.check("paperMidDensity", paperMidDensity ? *paperMidDensity : 0.744f, 0.01f, 4);
    // An id that is merely misspelled would print on the default sheet and look deliberate.
    if (nativePrintMedium && !printPaperPreset(*nativePrintMedium)) {
        std::string expected;
        for (size_t i = 0; i < allPrintPapers().size(); ++i) {
            if (i > 0) expected += ", ";
            expected += rawValue(allPrintPapers()[i]);
        }
        throw c.fail("nativePrintMedium", "is \"" + *nativePrintMedium + "\"; expected one of " + expected);
    }

    const size_t n = static_cast<size_t>(layers);
    c.check("flare", flare ? *flare : 0.0f, 0, 1);
    c.check("sensitivityFloor", sensitivityFloor ? *sensitivityFloor : 0.0f, 0, 0.1f);
    c.check("emulsionDiffusionMM", emulsionDiffusionMM ? *emulsionDiffusionMM : std::vector<float>(n, 0.0f), n, 0, 2);
    c.check("emulsionDiffusionSecondaryMM",
            emulsionDiffusionSecondaryMM ? *emulsionDiffusionSecondaryMM : std::vector<float>(n, 0.0f), n, 0, 2);
    c.check("emulsionDiffusionPrimaryShare",
            emulsionDiffusionPrimaryShare ? *emulsionDiffusionPrimaryShare : std::vector<float>(n, 1.0f), n, 0, 1);
    c.check("mtfLumaShare", mtfLumaShare ? *mtfLumaShare : 0.0f, 0, 1);
    c.check("lumaDiffusionMM", lumaDiffusionMM ? *lumaDiffusionMM : 0.0f, 0, 2);

    c.check("couplerInhibition", couplerInhibition, n, n, 0, 4);
    c.check("couplerReleaseGamma", couplerReleaseGamma ? *couplerReleaseGamma : std::vector<float>{1, 1, 1}, n, 0.25f,
            4);
    if (couplerGeometry) {
        c.check("couplerGeometry.layerDepthUM", couplerGeometry->layerDepthUM, n, -1000, 1000);
        // Gaps are read off neighbouring array entries, so the stack must be listed in order.
        const auto& depths = couplerGeometry->layerDepthUM;
        bool rising = true, falling = true;
        for (size_t i = 0; i + 1 < depths.size(); ++i) {
            rising = rising && depths[i] < depths[i + 1];
            falling = falling && depths[i] > depths[i + 1];
        }
        if (!(rising || falling)) {
            throw c.fail("couplerGeometry.layerDepthUM",
                         "must be strictly monotonic so neighbouring entries are neighbouring layers; got " +
                             describe(depths));
        }
        c.check("couplerGeometry.interlayerTransmission", couplerGeometry->interlayerTransmission,
                depths.size() - 1, 0, 1);
        c.check("couplerGeometry.release", couplerGeometry->release, n, 0, 4);
        c.check("couplerGeometry.selfRetention", couplerGeometry->selfRetention, 0, 1);
    }
    c.check("couplerDiffusionMM", couplerDiffusionMM, 0, 2);
    c.check("chromaticFringeAmount", chromaticFringeAmount ? *chromaticFringeAmount : 0.0f, 0, 1);
    c.check("chromaticFringeRadiusMM", chromaticFringeRadiusMM ? *chromaticFringeRadiusMM : 0.1f, 0, 2);
    c.check("adjacencyStrength", adjacencyStrength ? *adjacencyStrength : 0.0f, 0, 4);
    c.check("adjacencyRadiusMM", adjacencyRadiusMM ? *adjacencyRadiusMM : 0.0f, 0, 2);

    c.check("grainStrength", grainStrength, 0, 1);
    c.check("grainSizeMM", grainSizeMM, 0.0005f, 0.5f);
    c.check("grainLayerWeights", grainLayerWeights, n, 0, 10);
    c.check("grainLumaCorrelation", grainLumaCorrelation ? *grainLumaCorrelation : 0.0f, 0, 1);
    c.check("grainMottleShare", grainMottleShare ? *grainMottleShare : 0.0f, 0, 0.9f);
    c.check("grainMottleSizeRatio", grainMottleSizeRatio ? *grainMottleSizeRatio : 3.0f, 1, 8);
    const GrainDensityProfile& profile =
        grainDensityProfile ? *grainDensityProfile : FilmStock::defaultGrainDensityProfile();
    for (size_t record = 0; record < profile.records().size(); ++record) {
        const auto& row = profile.records()[record];
        std::string name = "grainDensityProfile[" + std::to_string(record) + "]";
        c.check(name, row, GrainDensityProfile::coefficientCount, 0, 100);
        c.check(name + ".toeDensity", row[1], 0.001f, 1);
        c.check(name + ".decayDensity", row[2], 0.01f, 4);
        c.check(name + ".humpAmplitude", row[3], 0, 20);
        c.check(name + ".humpDensity", row[4], 0, 4);
        c.check(name + ".humpWidth", row[5], 0.05f, 2);
    }
    if (grainReversalProfile) {
        c.check("grainReversalProfile", *grainReversalProfile, 2, 0.1f, 10);
        c.check("grainReversalProfile.exponent", (*grainReversalProfile)[0], 0.1f, 2);
    }
    c.check("halationStrength", halationStrength, n, 0, 1);
    c.check("halationLookScale", halationLookScale ? *halationLookScale : 1.0f, 0, 100);
    c.check("halationHazeMM", halationHazeMM ? *halationHazeMM : 0.0f, 0, 0.5f);
    const std::pair<const char*, const std::optional<HalationProfile>*> profiles[] = {
        {"halationProfile", &halationProfile}, {"estimatedHalationProfile", &estimatedHalationProfile}};
    for (const auto& entry : profiles) {
        if (!*entry.second) continue;
        const HalationProfile& p = **entry.second;
        std::string field = entry.first;
        c.check(field + ".roundTripOpticalDepth", p.roundTripOpticalDepth, n, 0, 20);
        c.check(field + ".angularExponent", p.angularExponent, n, 0, 16);
        c.check(field + ".diffuseShare", p.diffuseShare, n, 0, 1);
        c.check(field + ".diffuseSigmaMM", p.diffuseSigmaMM, n, 0, 1);
        c.check(field + ".bounceRetention", p.bounceRetention, n, 0, 1);
        if (p.recordDepthMM) c.check(field + ".recordDepthMM", *p.recordDepthMM, n, 0, 0.1f);
        for (size_t layer = 0; layer < n; ++layer) {
            if (p.diffuseShare[layer] > 0 && p.diffuseSigmaMM[layer] <= 0) {
                throw c.fail(field + ".diffuseSigmaMM",
                             "layer " + std::to_string(layer) + " must be positive when diffuseShare is non-zero");
            }
        }
    }
    if (halationReturnMatrix) {
        if (halationReturnMatrix->size() != n) {
            throw c.fail("halationReturnMatrix", "expects " + std::to_string(n) + " receiver rows, found " +
                                                     std::to_string(halationReturnMatrix->size()));
        }
        for (const auto& row : *halationReturnMatrix) c.check("halationReturnMatrix", row, n, 0, 1);
    }

    if (reciprocityFailure) {
        c.check("reciprocityFailure.thresholdSeconds", reciprocityFailure->thresholdSeconds, 0.01f, 100000);
        c.check("reciprocityFailure.lostStopsPerDecade", reciprocityFailure->lostStopsPerDecade, 0, 4);
        if (reciprocityFailure->statedThroughSeconds) {
            c.check("reciprocityFailure.statedThroughSeconds", *reciprocityFailure->statedThroughSeconds,
                    reciprocityFailure->thresholdSeconds, 100000);
        }
        if (reciprocityFailure->layerWeights) {
            c.check("reciprocityFailure.layerWeights", *reciprocityFailure->layerWeights, 3, 0, 4);
        }
    }

    if (development) {
        c.checkText(development->developer, "development.developer", 96, true);
        c.checkText(development->dilution, "development.dilution", 32, false);
        c.checkText(development->agitation, "development.agitation", 128, true);
        c.checkText(development->source, "development.source", 192, true);
        c.check("development.temperatureC", development->temperatureC, 0, 50);
        if (!(1 <= development->sourcePage && development->sourcePage <= 10000)) {
            throw c.fail("development.sourcePage",
                         "is " + std::to_string(development->sourcePage) + "; expected 1...10000");
        }
        if (development->conditions.empty()) throw c.fail("development.conditions", "is empty");
        std::vector<float> sortedStops;
        for (const auto& condition : development->conditions) sortedStops.push_back(condition.stops);
        std::stable_sort(sortedStops.begin(), sortedStops.end());
        for (size_t i = 0; i + 1 < sortedStops.size(); ++i) {
            if (std::fabs(sortedStops[i] - sortedStops[i + 1]) <= 1e-4f) {
                throw c.fail("development.conditions", "repeats " + describe(sortedStops[i]) + " stops");
            }
        }
        for (size_t index = 0; index < development->conditions.size(); ++index) {
            const auto& condition = development->conditions[index];
            std::string field = "development.conditions[" + std::to_string(index) + "]";
            c.check(field + ".stops", condition.stops, -6, 6);
            if (!(std::fabs(condition.stops) > 1e-4f)) {
                throw c.fail(field + ".stops", "reference development is already the stock and must not be repeated");
            }
            c.checkText(condition.label, field + ".label", 64, true);
            c.check(field + ".timeMinutes", condition.timeMinutes, 0.1f, 120);
            if (!condition.resolvedBasis()) {
                throw c.fail(field + ".basis", "is " + condition.basis +
                                                   "; expected measured, transferred with basisStock, or estimated "
                                                   "without it");
            }
            c.checkText(condition.basisStock, field + ".basisStock", 64, condition.basis == "transferred");
            if (condition.exposureIndex) c.check(field + ".exposureIndex", *condition.exposureIndex, 1, 1000000);
            if (condition.curves.size() != n) {
                throw c.fail(field + ".curves", "has " + std::to_string(condition.curves.size()) +
                                                    " entries; expected " + std::to_string(n));
            }
            for (size_t curveIndex = 0; curveIndex < condition.curves.size(); ++curveIndex) {
                const auto& curve = condition.curves[curveIndex];
                if (curve.sampled && schemaVersion < 2) {
                    throw c.fail("schemaVersion", "sampled characteristic curves require schema version 2");
                }
                curve.validate(field + ".curves[" + std::to_string(curveIndex) + "]", id);
            }
            const std::vector<CurveSpec> donorCurves =
                condition.donorCurves ? *condition.donorCurves : std::vector<CurveSpec>{};
            if (donorCurves.size() != donors.size()) {
                throw c.fail(field + ".donorCurves", "has " + std::to_string(donorCurves.size()) +
                                                         " entries; expected " + std::to_string(donors.size()));
            }
            for (size_t curveIndex = 0; curveIndex < donorCurves.size(); ++curveIndex) {
                const auto& curve = donorCurves[curveIndex];
                std::string curveField = field + ".donorCurves[" + std::to_string(curveIndex) + "]";
                if (curve.sampled) {
                    throw c.fail(curveField + ".sampled",
                                 "sampled records are supported only for dye-forming film curves");
                }
                curve.validate(curveField, id);
            }
            if (condition.grainStrength) c.check(field + ".grainStrength", *condition.grainStrength, 0, 1);
            if (condition.grainSizeMM) c.check(field + ".grainSizeMM", *condition.grainSizeMM, 0.0005f, 0.5f);
            if (condition.grainLayerWeights) {
                c.check(field + ".grainLayerWeights", *condition.grainLayerWeights, n, 0, 10);
            }
            if (condition.grainFogDensity) c.check(field + ".grainFogDensity", *condition.grainFogDensity, 0, 6);
            if (condition.adjacencyStrength) {
                c.check(field + ".adjacencyStrength", *condition.adjacencyStrength, 0, 4);
            }
        }
    }
}

}  // namespace fotufilm
