// Port of Sources/FotufilmCore/DiffusionFilter.swift. The filter's glass (`substrate`, `coating`,
// `glass`) belongs to the lens filter stack, which is not ported: it only adds the clear glass's
// veiling glare through `LensFilterStack.addedVeilingGlare`, so it is left out here.
#ifndef FOTUFILM_CORE_DIFFUSION_FILTER_H
#define FOTUFILM_CORE_DIFFUSION_FILTER_H

#include <functional>
#include <string>
#include <vector>

namespace fotufilm {

struct FilmStock;

/// Runtime diffusion halo represented by three Gaussian scales and per-capture-record weights.
struct DiffusionHalo {
    /// Gaussian sigmas in pixels, ascending. Shared by the records.
    std::vector<float> sigmasPixels;
    /// `weights[record][scale]`; an optional fourth row is the donor capture layer.
    std::vector<std::vector<float>> weights;
    float directShare = 1;
    float scatteredShare = 0;
    float truncatedShare = 0;

    bool isIdentity() const;
};

/// Optical model for a diffusion filter containing suspended particles.
struct DiffusionFilter {
    std::string id;
    std::string name;
    /// The fraction of the beam that meets a particle at all — the filter's grade.
    float interactedFraction;
    /// Single scattering albedo of the particles, 0…1.
    float albedo;
    /// Median particle radius in micrometres, and the width of the log-normal spread about it.
    float particleRadiusUM;
    float sizeSpread;

    DiffusionFilter(std::string id, std::string name, float interactedFraction, float albedo, float particleRadiusUM,
                    float sizeSpread = 0.45f);

    float directShare() const { return 1 - interactedFraction; }
    float scatteredShare() const { return interactedFraction * albedo; }
    float absorbedShare() const { return interactedFraction * (1 - albedo); }

    /// Per-capture-record sensitivity centroid under daylight.
    static std::vector<float> recordWavelengths(const FilmStock& stock);
    static float scatteredEnergyWithin(float radiusPixels, float wavelengthNM, float medianRadiusUM, float spread,
                                       float focalLengthMM, float pixelPitchMM);
    static float airyEnergyWithin(float u);

    /// The halo this filter makes on a given emulsion, imaged by a given lens onto a frame.
    DiffusionHalo halo(const FilmStock& stock, float focalLengthMM, float pixelPitchMM,
                       float maximumSigmaPixels = 512) const;

    static constexpr float coreQuantile = 0.10f;
    static constexpr float tailQuantile = 0.96f;

    static std::vector<float> solveWeights(const std::vector<float>& sigmas, float reachPixels,
                                           const std::function<float(float)>& profile);

    enum class Grade { eighth, quarter, half, one, two };
    static const char* rawValue(Grade grade);
    static float gradeInteractedFraction(Grade grade);

    enum class Family { proMist, blackProMist, glimmerglass, blackGlimmerglass, fog, blackFog };
    static const char* rawValue(Family family);
    static const char* label(Family family);
    static float familyParticleRadiusUM(Family family);
    static float familyAlbedo(Family family);

    static DiffusionFilter preset(Family family, Grade grade);

    bool operator==(const DiffusionFilter& o) const {
        return id == o.id && name == o.name && interactedFraction == o.interactedFraction && albedo == o.albedo &&
               particleRadiusUM == o.particleRadiusUM && sizeSpread == o.sizeSpread;
    }
};

float besselJ0(float x);
float besselJ1(float x);

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_DIFFUSION_FILTER_H
