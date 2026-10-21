// Port of Sources/FotufilmCore/SpectralModel.swift: SpectralRuntime's film exposure — the
// reflectance reconstruction, the exposure table's locus-enclosing domain, the donor channel,
// buildLUT, and the scene-light exposure tables ("Scene-light film exposure"). Lens-filter tables
// ("Film exposure through a lens filter") and layered-transport exposure tables are not ported.
#include <cmath>
#include <cstring>
#include <mutex>

#include "BoundedCache.h"
#include "ColorScience.h"
#include "Illuminant.h"
#include "ParallelWork.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace SpectralRuntime {

namespace {

uint64_t bits(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

constexpr uint64_t kPrime = 0x100000001b3ULL;

struct Double3 {
    double x, y, z;
};

/// The spectral locus as the table builder sees it: each observer band in Rec.2020, normalised to
/// unit luminance and taken about the neutral axis.
struct SpectralLocus {
    /// The observer's luminous efficiency below which a band is left off the polygon.
    static constexpr float luminousEfficiencyFloor = 0.01f;
    std::vector<int> bands;
    std::vector<Double3> chroma;

    SpectralLocus() {
        const auto& xBar = SpectralGrid::xBar();
        const auto& yBar = SpectralGrid::yBar();
        const auto& zBar = SpectralGrid::zBar();
        for (int i = 0; i < SpectralGrid::count; ++i) {
            if (yBar[i] >= luminousEfficiencyFloor) bands.push_back(i);
        }
        for (int band : bands) {
            Float3 rgb = SpectralGrid::linearRec2020FromXYZ(Float3(xBar[band], yBar[band], zBar[band]));
            double y = static_cast<double>(yBar[band]);
            chroma.push_back(Double3{static_cast<double>(rgb.x) / y - 1, static_cast<double>(rgb.y) / y - 1,
                                     static_cast<double>(rgb.z) / y - 1});
        }
    }

    static const SpectralLocus& shared() {
        static const SpectralLocus locus;
        return locus;
    }
};

}  // namespace

std::vector<float> reconstructedReflectance(Float3 linearRGB) {
    Float3 rgb(swiftMax(linearRGB.x, 0.0f), swiftMax(linearRGB.y, 0.0f), swiftMax(linearRGB.z, 0.0f));
    float peak = swiftMax(rgb.x, rgb.y, rgb.z);
    if (!(peak > 0)) return std::vector<float>(SpectralGrid::count, 0.0f);
    std::shared_ptr<const MeasuredReflectanceTable> model = MeasuredReflectanceTable::shared();
    if (!model) return std::vector<float>(SpectralGrid::count, (rgb.x + rgb.y + rgb.z) / 3);
    return model->reflectance(rgb);
}

SpectralLUT buildLUT(const std::function<Float3(Float3)>& evaluate, const std::function<float(Float3)>& fourth) {
    const int d = lutDimension;
    std::vector<float> values(static_cast<size_t>(d) * d * d * 4, 0.0f);
    float* buffer = values.data();
    ParallelWork::forEach(d, [&](int z) {
        for (int y = 0; y < d; ++y) {
            for (int x = 0; x < d; ++x) {
                Float3 p(static_cast<float>(x) / static_cast<float>(d - 1), static_cast<float>(y) / static_cast<float>(d - 1),
                         static_cast<float>(z) / static_cast<float>(d - 1));
                Float3 v = evaluate(p);
                size_t i = ((static_cast<size_t>(z) * d + y) * d + x) * 4;
                buffer[i] = v.x;
                buffer[i + 1] = v.y;
                buffer[i + 2] = v.z;
                buffer[i + 3] = fourth ? fourth(p) : 1.0f;
            }
        }
    });
    return SpectralLUT(d, std::move(values));
}

SpectralLUT buildDensityLUT(const FilmStock& stock, const DensityRead& evaluate) {
    float ranges[3] = {stock.curves[0].dMax() - stock.curves[0].dMin, stock.curves[1].dMax() - stock.curves[1].dMin,
                       stock.curves[2].dMax() - stock.curves[2].dMin};
    float dMin[3] = {stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin};
    return buildLUT([&](Float3 p) {
        return evaluate({dMin[0] + p.x * ranges[0], dMin[1] + p.y * ranges[1], dMin[2] + p.z * ranges[2]});
    });
}

std::vector<float> filmReferenceIlluminant(const FilmStock& stock) {
    return Illuminant::atLocus(stock.referenceIlluminantKelvin);
}

Float3 anchoredExposure(Float3 rgb, const FilmStock& stock, const std::vector<float>& illuminant,
                        const std::vector<float>* referenceIlluminant, const SpectralFilter* filter) {
    float peak = swiftMax(rgb.x, rgb.y, rgb.z);
    if (!(peak > 0)) return Float3::repeating(0);
    float radiance = peak / reconstructionAnchor;
    return spectralExposure(rgb / radiance, stock, illuminant, referenceIlluminant, filter) * radiance;
}

Float3 spectralExposure(Float3 rgb, const FilmStock& stock, const std::vector<float>& illuminant,
                        const std::vector<float>* suppliedReference, const SpectralFilter* filter) {
    std::vector<float> ownReference;
    if (!suppliedReference) ownReference = filmReferenceIlluminant(stock);
    const std::vector<float>& referenceIlluminant = suppliedReference ? *suppliedReference : ownReference;
    preconditionCheck(static_cast<int>(illuminant.size()) == SpectralGrid::count, "illuminant on the grid");
    preconditionCheck(static_cast<int>(referenceIlluminant.size()) == SpectralGrid::count, "reference on the grid");
    const auto& layers = stock.spectralProfile.layerSensitivity;
    std::shared_ptr<const MeasuredReflectanceTable> model = MeasuredReflectanceTable::shared();
    if (!model) {
        // The compact fallback was fitted against the P3 basis.
        const auto& s = stock.sensitivity;
        Float3 p3 = ColorScience::linearRec2020ToDisplayP3(rgb);
        auto dot = [](const std::vector<float>& row, Float3 v) { return row[0] * v.x + row[1] * v.y + row[2] * v.z; };
        Float3 plain(dot(s[0], p3), dot(s[1], p3), dot(s[2], p3));
        float sceneY = Illuminant::luminance(illuminant);
        float referenceY = Illuminant::luminance(referenceIlluminant);
        Float3 sceneIntegral = Float3::repeating(0);
        Float3 referenceIntegral = Float3::repeating(0);
        for (int i = 0; i < SpectralGrid::count; ++i) {
            float light = illuminant[i] / sceneY;
            float reference = referenceIlluminant[i] / referenceY;
            for (int layer = 0; layer < 3; ++layer) {
                sceneIntegral[layer] += light * layers[layer][i];
                referenceIntegral[layer] += reference * layers[layer][i];
            }
        }
        Float3 denominator(swiftMax(referenceIntegral.x, 1e-12f), swiftMax(referenceIntegral.y, 1e-12f),
                           swiftMax(referenceIntegral.z, 1e-12f));
        Float3 illuminantRatio = sceneIntegral / denominator;
        if (!filter) return plain * illuminantRatio;
        return plain * illuminantRatio * filter->layerRatio * filter->gain;
    }
    std::vector<float> reflectance = model->reflectance(rgb);
    Float3 exposure = Float3::repeating(0);
    Float3 referenceExposure = Float3::repeating(0);
    float sceneY = Illuminant::luminance(illuminant);
    float referenceY = Illuminant::luminance(referenceIlluminant);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        float light = illuminant[i] / sceneY;
        float referenceLight = referenceIlluminant[i] / referenceY;
        float through = filter ? reflectance[i] * filter->transmittance[i] : reflectance[i];
        exposure.x += through * light * layers[0][i];
        exposure.y += through * light * layers[1][i];
        exposure.z += through * light * layers[2][i];
        referenceExposure.x += referenceLight * layers[0][i];
        referenceExposure.y += referenceLight * layers[1][i];
        referenceExposure.z += referenceLight * layers[2][i];
    }
    float gain = filter ? filter->gain : 1.0f;
    return Float3(gain * exposure.x / swiftMax(referenceExposure.x, 1e-12f),
                  gain * exposure.y / swiftMax(referenceExposure.y, 1e-12f),
                  gain * exposure.z / swiftMax(referenceExposure.z, 1e-12f));
}

// ---- The exposure table's domain ----

Float3 extrapolatedExposure(Float3 face, Float3 mirror) {
    Float3 continued = face * 2 - mirror;
    return Float3(swiftMax(continued.x, 0.0f), swiftMax(continued.y, 0.0f), swiftMax(continued.z, 0.0f));
}

float extrapolatedExposure(float face, float mirror) { return swiftMax(face * 2 - mirror, 0.0f); }

std::optional<SpectralLocusHit> spectralLocusBoundary(double dx, double dy, double dz) {
    (void)dz;
    const SpectralLocus& locus = SpectralLocus::shared();
    std::optional<SpectralLocusHit> best;
    size_t count = locus.chroma.size();
    for (size_t lower = 0; lower < count; ++lower) {
        size_t upper = (lower + 1) % count;
        const Double3& from = locus.chroma[lower];
        Double3 edge{locus.chroma[upper].x - from.x, locus.chroma[upper].y - from.y, locus.chroma[upper].z - from.z};
        // alpha * direction - beta * edge = from, solved in the plane's x and y.
        double determinant = -dx * edge.y + edge.x * dy;
        if (!(std::fabs(determinant) > 1e-18)) continue;
        double alpha = (-from.x * edge.y + edge.x * from.y) / determinant;
        double beta = (dx * from.y - dy * from.x) / determinant;
        if (!(alpha > 0 && beta >= -1e-9 && beta <= 1 + 1e-9)) continue;
        if (!best || alpha < best->reach) {
            best = SpectralLocusHit{alpha, locus.bands[lower], locus.bands[upper],
                                    swiftMin(swiftMax(beta, 0.0), 1.0)};
        }
    }
    return best;
}

float projectionMargin() { return 1.5f / static_cast<float>(lutDimension - 1); }

SceneLight sceneLight(Float3 rgb) {
    SceneLight result{};
    float peak = swiftMax(rgb.x, rgb.y, rgb.z);
    float binding = swiftMin(rgb.x, rgb.y, rgb.z);
    // A cell on a face of the Rec.2020 cube reaches it through the seam with rounding noise.
    if (!(binding < -1e-6f * swiftMax(peak, 1.0f))) {
        result.kind = SceneLight::Kind::reflectance;
        result.colour = Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f));
        return result;
    }
    Float3 weights = ColorScience::luminanceWeights;
    float luminance = weights.x * rgb.x + weights.y * rgb.y + weights.z * rgb.z;
    if (!(luminance > 0)) {
        result.kind = SceneLight::Kind::none;
        return result;
    }
    float cube = 1;
    int bindingChannel = 0;
    int peakChannel = 0;
    for (int channel = 0; channel < 3; ++channel) {
        if (rgb[channel] < 0) {
            float reach = luminance / (luminance - rgb[channel]);
            if (reach < cube) {
                cube = reach;
                bindingChannel = channel;
            }
        }
        if (rgb[channel] > rgb[peakChannel]) peakChannel = channel;
    }
    Float3 neutral = Float3::repeating(luminance);
    Float3 face = neutral + cube * (rgb - neutral);
    face = Float3(swiftMax(face.x, 0.0f), swiftMax(face.y, 0.0f), swiftMax(face.z, 0.0f));
    auto mirrored = [&](Float3 point) {
        Float3 mirror = face * 2 - point;
        return Float3(swiftMax(mirror.x, 0.0f), swiftMax(mirror.y, 0.0f), swiftMax(mirror.z, 0.0f));
    };
    // Within the margin the reflectance model continues by its own slope.
    float margin = projectionMargin();
    if (!(binding < -margin * peak)) {
        result.kind = SceneLight::Kind::extrapolated;
        result.colour = face;
        result.mirror = mirrored(rgb);
        return result;
    }
    double dl = static_cast<double>(luminance);
    std::optional<SpectralLocusHit> hit =
        spectralLocusBoundary(static_cast<double>(rgb.x) / dl - 1, static_cast<double>(rgb.y) / dl - 1,
                              static_cast<double>(rgb.z) / dl - 1);
    if (!hit) {
        result.kind = SceneLight::Kind::extrapolated;
        result.colour = face;
        result.mirror = mirrored(rgb);
        return result;
    }
    // Along the colour's own ray from the neutral axis: the colour at 1, the face at `cube`, the
    // margin at `start`, the locus at `reach`.
    float depth = luminance - rgb[bindingChannel];
    float rise = rgb[peakChannel] - luminance;
    float start = luminance * (1 + margin) / (depth - margin * rise);
    Float3 marginPoint = neutral + start * (rgb - neutral);
    float reach = static_cast<float>(hit->reach);
    float share = reach - start > 1e-6f ? swiftMin(swiftMax((1 - start) / (reach - start), 0.0f), 1.0f) : 1.0f;
    result.kind = SceneLight::Kind::locus;
    result.locus = LocusLight{face, mirrored(marginPoint), luminance, hit->lowerBand, hit->upperBand,
                              static_cast<float>(hit->upperShare), share};
    return result;
}

std::vector<float> monochromaticExposure(const LocusLight& light, const std::vector<std::vector<float>>& sensitivities,
                                         const std::vector<float>& illuminant,
                                         const std::vector<float>& referenceIlluminant, const SpectralFilter* filter) {
    const auto& yBar = SpectralGrid::yBar();
    float sceneY = Illuminant::luminance(illuminant);
    float referenceY = Illuminant::luminance(referenceIlluminant);
    float white = 0;
    for (int i = 0; i < SpectralGrid::count; ++i) white += illuminant[i] / sceneY * yBar[i];
    float target = light.luminance * white;
    int lower = light.lowerBand;
    int upper = light.upperBand;
    float lowerPower = (1 - light.upperShare) * target / swiftMax(yBar[lower], 1e-12f);
    float upperPower = light.upperShare * target / swiftMax(yBar[upper], 1e-12f);
    float lowerThrough = lowerPower * (filter ? filter->transmittance[lower] : 1.0f);
    float upperThrough = upperPower * (filter ? filter->transmittance[upper] : 1.0f);
    float gain = filter ? filter->gain : 1.0f;
    std::vector<float> result;
    result.reserve(sensitivities.size());
    for (const std::vector<float>& sensitivity : sensitivities) {
        float exposure = lowerThrough * sensitivity[lower] + upperThrough * sensitivity[upper];
        float reference = 0;
        for (int i = 0; i < SpectralGrid::count; ++i) reference += referenceIlluminant[i] / referenceY * sensitivity[i];
        result.push_back(gain * exposure / swiftMax(reference, 1e-12f));
    }
    return result;
}

Float3 domainExposure(Float3 point, const FilmStock& stock, const std::vector<float>& illuminant,
                      const std::vector<float>* referenceIlluminant, const SpectralFilter* filter) {
    Float3 rgb = ColorScience::linearExposureDomainToRec2020(point);
    auto reflectance = [&](Float3 colour) {
        return anchoredExposure(colour, stock, illuminant, referenceIlluminant, filter);
    };
    SceneLight light = sceneLight(rgb);
    switch (light.kind) {
        case SceneLight::Kind::none: return Float3();
        case SceneLight::Kind::reflectance: return reflectance(light.colour);
        case SceneLight::Kind::extrapolated:
            return extrapolatedExposure(reflectance(light.colour), reflectance(light.mirror));
        case SceneLight::Kind::locus: {
            const LocusLight& locus = light.locus;
            Float3 edge = extrapolatedExposure(reflectance(locus.face), reflectance(locus.mirror));
            const auto& layers = stock.spectralProfile.layerSensitivity;
            std::vector<std::vector<float>> prefix(layers.begin(), layers.begin() + std::min<size_t>(3, layers.size()));
            std::vector<float> ownReference;
            if (!referenceIlluminant) ownReference = filmReferenceIlluminant(stock);
            std::vector<float> mono = monochromaticExposure(
                locus, prefix, illuminant, referenceIlluminant ? *referenceIlluminant : ownReference, filter);
            return (1 - locus.share) * edge + locus.share * Float3(mono[0], mono[1], mono[2]);
        }
    }
    return Float3();
}

float domainDonorExposure(Float3 point, const DonorCaptureLayer& donor, const std::vector<float>& illuminant,
                          const std::vector<float>& referenceIlluminant, const SpectralFilter* filter) {
    Float3 rgb = ColorScience::linearExposureDomainToRec2020(point);
    auto reflectance = [&](Float3 colour) {
        return donorAnchoredExposure(colour, donor, illuminant, referenceIlluminant, filter);
    };
    SceneLight light = sceneLight(rgb);
    switch (light.kind) {
        case SceneLight::Kind::none: return 0;
        case SceneLight::Kind::reflectance: return reflectance(light.colour);
        case SceneLight::Kind::extrapolated:
            return extrapolatedExposure(reflectance(light.colour), reflectance(light.mirror));
        case SceneLight::Kind::locus: {
            const LocusLight& locus = light.locus;
            float edge = extrapolatedExposure(reflectance(locus.face), reflectance(locus.mirror));
            float mono = monochromaticExposure(locus, {donor.sensitivity}, illuminant, referenceIlluminant, filter)[0];
            return (1 - locus.share) * edge + locus.share * mono;
        }
    }
    return 0;
}

SpectralLUT exposureTable(const FilmStock& stock, const std::vector<float>& illuminant,
                          const std::vector<float>* referenceIlluminant, const SpectralFilter* filter) {
    return buildLUT(
        [&](Float3 point) { return domainExposure(point, stock, illuminant, referenceIlluminant, filter); },
        donorChannel(stock, illuminant, referenceIlluminant, filter));
}

float donorAnchoredExposure(Float3 rgb, const DonorCaptureLayer& donor, const std::vector<float>& illuminant,
                            const std::vector<float>& suppliedReference, const SpectralFilter* filter) {
    float peak = swiftMax(rgb.x, rgb.y, rgb.z);
    if (!(peak > 0)) return 0;
    float radiance = peak / reconstructionAnchor;
    std::shared_ptr<const MeasuredReflectanceTable> model = MeasuredReflectanceTable::shared();
    // The compact fallback has no spectrum to weigh a fourth record with; the donor stage is gated
    // on the model, so this value is never read.
    if (!model) return 0;
    std::vector<float> reflectance = model->reflectance(rgb / radiance);
    float exposure = 0;
    float referenceExposure = 0;
    float sceneY = Illuminant::luminance(illuminant);
    float referenceY = Illuminant::luminance(suppliedReference);
    for (int i = 0; i < SpectralGrid::count; ++i) {
        float light = illuminant[i] / sceneY;
        float through = filter ? reflectance[i] * filter->transmittance[i] : reflectance[i];
        exposure += through * light * donor.sensitivity[i];
        referenceExposure += suppliedReference[i] / referenceY * donor.sensitivity[i];
    }
    return (filter ? filter->gain : 1.0f) * exposure / swiftMax(referenceExposure, 1e-12f) * radiance;
}

bool hasReconstructionModel() { return MeasuredReflectanceTable::shared() != nullptr; }

std::optional<int> reconstructionSourceCount() {
    std::shared_ptr<const MeasuredReflectanceTable> model = MeasuredReflectanceTable::shared();
    if (!model) return std::nullopt;
    return model->sourceCount;
}

std::function<float(Float3)> donorChannel(const FilmStock& stock, const std::vector<float>& illuminant,
                                          const std::vector<float>* suppliedReference, const SpectralFilter* filter) {
    if (stock.donorLayers.empty() || !MeasuredReflectanceTable::shared()) return {};
    DonorCaptureLayer donor = stock.donorLayers.front();
    std::vector<float> reference = suppliedReference ? *suppliedReference : filmReferenceIlluminant(stock);
    std::vector<float> light = illuminant;
    std::optional<SpectralFilter> ownFilter;
    if (filter) ownFilter = *filter;
    return [donor, light, reference, ownFilter](Float3 point) {
        return domainDonorExposure(point, donor, light, reference, ownFilter ? &*ownFilter : nullptr);
    };
}

uint64_t donorSignature(const FilmStock& stock) {
    uint64_t h = 0;
    for (const DonorCaptureLayer& donor : stock.donorLayers) {
        h = (h ^ 0x444F4E4F52ULL) * kPrime;  // "DONOR"
        for (float v : donor.sensitivity) h = (h ^ bits(v)) * kPrime;
        for (float v : {donor.curve.dMin, donor.curve.gamma, donor.curve.toe, donor.curve.toeWidth,
                        donor.curve.shoulder, donor.curve.shoulderWidth}) {
            h = (h ^ bits(v)) * kPrime;
        }
        if (donor.curve.secondary) {
            const CharacteristicCurveComponent& secondary = *donor.curve.secondary;
            h = (h ^ 1) * kPrime;
            for (float v : {secondary.gamma, secondary.toe, secondary.toeWidth, secondary.shoulder,
                            secondary.shoulderWidth}) {
                h = (h ^ bits(v)) * kPrime;
            }
        } else {
            h = h * kPrime;
        }
        for (float v : donor.inhibition) h = (h ^ bits(v)) * kPrime;
    }
    return h;
}

// ---- Scene-light film exposure ----

std::optional<float> sceneLightKelvin(std::optional<float> cct) {
    if (!cct || !std::isfinite(*cct) || !(*cct > 0)) return std::nullopt;
    return cct;
}

float sceneLightWarmWeight(float cct) {
    // DualIlluminantMatrices.daylightKelvin / tungstenKelvin (CameraSpectralProfile.swift).
    const float daylightKelvin = 6504;
    const float tungstenKelvin = 2856;
    float coolMired = 1 / daylightKelvin;
    float warmMired = 1 / tungstenKelvin;
    float mired = 1 / swiftMax(cct, 1.0f);
    return clamp((mired - coolMired) / (warmMired - coolMired), 0.0f, 1.0f);
}

namespace {
std::mutex& sceneLightLock() {
    static std::mutex lock;
    return lock;
}
BoundedCache<uint64_t, SceneLightAnchors>& sceneAnchorCache() {
    static BoundedCache<uint64_t, SceneLightAnchors> cache(16);
    return cache;
}
BoundedCache<uint64_t, SpectralLUT>& sceneBlendCache() {
    static BoundedCache<uint64_t, SpectralLUT> cache(32);
    return cache;
}

/// What the exposure table depends on: the spectral profile plus the fallback's RGB matrix.
uint64_t sceneLightKey(const FilmStock& stock) {
    uint64_t h = stock.spectralProfile.signature();
    h = (h ^ bits(stock.referenceIlluminantKelvin)) * kPrime;
    for (const auto& row : stock.sensitivity) {
        for (float v : row) h = (h ^ bits(v)) * kPrime;
    }
    uint64_t donor = donorSignature(stock);
    if (donor != 0) h = (h ^ donor) * kPrime;
    return h;
}
}  // namespace

uint64_t illuminantSignature(const std::vector<float>& illuminant) {
    preconditionCheck(static_cast<int>(illuminant.size()) == SpectralGrid::count, "illuminant on the grid");
    uint64_t h = 0xcbf29ce484222325ULL;
    bool energy = false;
    for (float value : illuminant) {
        preconditionCheck(std::isfinite(value) && value >= 0, "illuminant SPD must be finite and nonnegative");
        h = (h ^ bits(value)) * kPrime;
        if (value > 0) energy = true;
    }
    preconditionCheck(energy, "illuminant SPD must contain energy");
    return h;
}

SceneLightAnchors sceneLightAnchors(const FilmStock& stock) {
    uint64_t key = sceneLightKey(stock);
    std::lock_guard<std::mutex> guard(sceneLightLock());
    if (auto found = sceneAnchorCache().value(key)) return *found;
    SceneLightAnchors built{exposureTable(stock, Illuminant::a()), exposureTable(stock, SpectralGrid::d65())};
    sceneAnchorCache().insert(built, key);
    return built;
}

SpectralLUT sceneExposure(const FilmStock& stock, float cct) {
    return sceneExposure(stock, Illuminant::atLocus(cct));
}

SpectralLUT sceneExposure(const FilmStock& stock, const std::vector<float>& illuminant) {
    uint64_t key = (sceneLightKey(stock) ^ illuminantSignature(illuminant)) * kPrime;
    {
        std::lock_guard<std::mutex> guard(sceneLightLock());
        if (auto found = sceneBlendCache().value(key)) return *found;
    }
    SpectralLUT built = exposureTable(stock, illuminant);
    {
        std::lock_guard<std::mutex> guard(sceneLightLock());
        sceneBlendCache().insert(built, key);
    }
    return built;
}

std::vector<float> sceneIlluminant(std::optional<float> cct) {
    if (!cct) return SpectralGrid::d65();
    return Illuminant::atLocus(*cct);
}

}  // namespace SpectralRuntime
}  // namespace fotufilm
