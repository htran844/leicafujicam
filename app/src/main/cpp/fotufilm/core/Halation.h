// Port of Sources/FotufilmCore/Halation.swift: FilmBase, HalationProfile, the `Halation` model
// (kernels, optics, fits) and `HalationRuntime`.
#ifndef FOTUFILM_CORE_HALATION_H
#define FOTUFILM_CORE_HALATION_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Json.h"

namespace fotufilm {

/// The transparent support a frame is exposed on.
struct FilmBase {
    /// Support thickness in millimeters.
    float thicknessMM;
    /// Refractive index of the support: 1.48 for cellulose triacetate, 1.64 for ESTAR.
    float refractiveIndex;
    /// Optional refractive indices at the red, green, and blue record centers.
    std::optional<std::vector<float>> recordRefractiveIndices;

    FilmBase(float thicknessMM, float refractiveIndex,
             std::optional<std::vector<float>> recordRefractiveIndices = std::nullopt);

    /// Cellulose triacetate ("acetate safety base").
    static FilmBase acetate(float thicknessMM);
    /// Polyester (Kodak ESTAR).
    static FilmBase estar(float thicknessMM);

    /// Angle inside the base beyond which the rear surface reflects everything.
    float criticalAngle() const;
    /// Where the halo lives.
    float haloRadiusMM() const;
    /// Wavelength-dispersed refractive index for record 0 (red), 1 (green), 2 (blue).
    float refractiveIndexForRecord(int record) const;

    bool operator==(const FilmBase& o) const {
        return thicknessMM == o.thicknessMM && refractiveIndex == o.refractiveIndex &&
               recordRefractiveIndices == o.recordRefractiveIndices;
    }
    bool operator!=(const FilmBase& o) const { return !(*this == o); }
};

/// The independently calibratable shape of a stock's returned-light profile.
struct HalationProfile {
    /// Natural-log optical depth at normal incidence for one round trip to the rear reflector.
    std::vector<float> roundTripOpticalDepth;
    /// Exponent q of the normalized angular density (q + 1) cos^q sin.
    std::vector<float> angularExponent;
    /// Share of returned light assigned to short-range scatter inside the emulsion.
    std::vector<float> diffuseShare;
    /// Gaussian sigma in millimeters of that short-range component.
    std::vector<float> diffuseSigmaMM;
    /// Effective survival from one rear-surface return into another round trip.
    std::vector<float> bounceRetention;
    /// Height in millimeters of each imaging record above the front surface of the support.
    std::optional<std::vector<float>> recordDepthMM;

    static HalationProfile decode(const JsonValue& value, const std::string& path);

    bool operator==(const HalationProfile& o) const {
        return roundTripOpticalDepth == o.roundTripOpticalDepth && angularExponent == o.angularExponent &&
               diffuseShare == o.diffuseShare && diffuseSigmaMM == o.diffuseSigmaMM &&
               bounceRetention == o.bounceRetention && recordDepthMM == o.recordDepthMM;
    }
    bool operator!=(const HalationProfile& o) const { return !(*this == o); }
};

/// The physical model of halation: light that the emulsion failed to absorb reflecting off the back
/// of the film base and exposing the emulsion a second time, displaced.
namespace Halation {

/// Three centered Gaussian scales are shared across layers; only the mixing weights differ.
constexpr int scaleCount = 3;
/// Ratio between neighbouring blur scales.
constexpr float scaleRatio = 1.7f;
/// Round trips counted before the trapped light is negligible.
constexpr int bounceCount = 5;
/// Midpoint samples across (0, pi/2) for the angular quadrature.
constexpr int angleSamples = 256;

/// Everything the halation stage needs, in millimeters on the film.
struct Kernel {
    /// Gaussian sigmas of the three centered blur scales, ascending.
    std::vector<float> sigmaMM;
    /// Retained for the renderer configuration ABI. Continuous kernels keep every radius zero.
    std::vector<float> ringRadiusMM;
    /// Per layer, the mixture over those three scales.
    std::vector<std::vector<float>> weights;
    /// Per layer, `s / (1 + s)`.
    std::vector<float> mix;
    /// Receiver rows by source columns, each row summing to that receiver's `mix` share.
    std::vector<std::vector<float>> matrix;

    bool operator==(const Kernel& o) const {
        return sigmaMM == o.sigmaMM && ringRadiusMM == o.ringRadiusMM && weights == o.weights && mix == o.mix &&
               matrix == o.matrix;
    }
};

std::vector<std::vector<float>> mixMatrix(const std::vector<float>& mix,
                                          const std::optional<std::vector<std::vector<float>>>& returnMatrix);
/// The share of a layer's exposure that arrives as returned light.
float mix(float returning);
/// The legacy returned-fraction inversion.
Kernel kernel(const std::vector<float>& returned, const FilmBase& base, float hazeMM = 0,
              const std::optional<std::vector<std::vector<float>>>& returnMatrix = std::nullopt);
/// The independently calibrated physical profile when one is supplied, else the legacy kernel.
Kernel kernel(const std::vector<float>& returned, const FilmBase& base, const std::optional<HalationProfile>& profile,
              float hazeMM = 0, const std::optional<std::vector<std::vector<float>>>& returnMatrix = std::nullopt);
std::vector<float> hazed(const std::vector<float>& sigmas, float hazeMM);

/// The part of the model that depends only on the support's refractive index.
struct Optics {
    float index;
    std::vector<float> cosines;
    std::vector<float> sines;
    std::vector<float> reflectancesS;
    std::vector<float> reflectancesP;
    std::vector<float> lambertian;
    float step;

    explicit Optics(float index);
    /// Memoized per index.
    static std::shared_ptr<const Optics> shared(float index);
};

/// One layer's scattered light, tabulated.
class Profile {
public:
    Profile(float returning, const FilmBase& base, float recordDepthMM, const Optics& optics);
    float edgeSpread(float distance) const;
    float anchorSigmaMM() const;

private:
    FilmBase base_;
    std::vector<float> radii_;
    std::vector<float> weights_;
    std::vector<int> blocks_;
};

/// A returned-light profile whose energy is normalized independently of its spatial shape.
class PhysicalProfile {
public:
    PhysicalProfile(const FilmBase& base, float recordDepthMM, const Optics& optics, float opticalDepth,
                    float angularExponent, float diffuseShare, float diffuseSigmaMM, float bounceRetention);
    float edgeSpread(float distance) const;
    float diffuseShare;
    float diffuseSigmaMM;

private:
    std::vector<float> radii_;
    std::vector<float> weights_;
};

struct PolarizedReflectance {
    float s;
    float p;
};
PolarizedReflectance polarizedReflectance(float cosTheta, float index);
float reflectance(float cosTheta, float index);
float survival(float transmittance, float cosTheta);
float returnedFraction(float transmittance, const Optics& optics);
float returnedFraction(float transmittance, float index);
float transmittance(float returning, const Optics& optics);
float transmittance(float returning, float index);
float gaussianEdgeSpread(float distance, float sigma);
/// Least squares over the simplex: minimize |basis * w - target| with w >= 0, sum(w) = 1.
std::vector<float> simplexFit(const std::vector<std::vector<float>>& basis, const std::vector<float>& target);

}  // namespace Halation

/// Builds and caches halation kernels.
namespace HalationRuntime {
Halation::Kernel kernel(const std::vector<float>& returned, const FilmBase& base,
                        const std::optional<HalationProfile>& profile = std::nullopt, float hazeMM = 0,
                        const std::optional<std::vector<std::vector<float>>>& returnMatrix = std::nullopt);
}  // namespace HalationRuntime

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HALATION_H
