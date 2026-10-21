// Port of Sources/FotufilmCore/SpectralModel.swift: FilmDyeFamily, NegativeViewing and
// FilmSpectralProfile.
#ifndef FOTUFILM_CORE_FILM_SPECTRAL_PROFILE_H
#define FOTUFILM_CORE_FILM_SPECTRAL_PROFILE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fotufilm {

/// FilmStock's layer/dye limits, here so the profile can check them without including FilmStock.
namespace FilmStockLimits {
constexpr int supportedCaptureLayerMin = 3;
constexpr int supportedCaptureLayerMax = 3;
constexpr int supportedDonorLayerMin = 0;
constexpr int supportedDonorLayerMax = 1;
constexpr int dyeCount = 3;
}  // namespace FilmStockLimits

/// Image-dye families used by the wavelength-domain output model.
enum class FilmDyeFamily { kodakNegative, fujiNegative, motionNegative, kodachrome, monochrome };

const char* rawValue(FilmDyeFamily family);
std::optional<FilmDyeFamily> filmDyeFamilyFromRawValue(const std::string& raw);

/// How a negative is shown when the reader asks for the film instead of the print.
enum class NegativeViewing { lightBox, scanner };

const char* rawValue(NegativeViewing viewing);
std::optional<NegativeViewing> negativeViewingFromRawValue(const std::string& raw);
inline std::string id(NegativeViewing viewing) { return rawValue(viewing); }
const char* name(NegativeViewing viewing);
const char* detail(NegativeViewing viewing);
/// A small stable integer for cache identities.
uint64_t ordinal(NegativeViewing viewing);
const std::vector<NegativeViewing>& allNegativeViewings();

/// Wavelength-domain description of one film's capture layers and image dyes.
struct FilmSpectralProfile {
    std::vector<std::vector<float>> layerSensitivity;
    std::vector<std::vector<float>> imageDyeDensity;
    /// Optional whole-film minimum spectrum, in diffuse optical density.
    std::optional<std::vector<float>> minimumDensity;

    FilmSpectralProfile(std::vector<std::vector<float>> layerSensitivity,
                        std::vector<std::vector<float>> imageDyeDensity,
                        std::optional<std::vector<float>> minimumDensity = std::nullopt);

    int captureLayerCount() const { return static_cast<int>(layerSensitivity.size()); }

    /// Every capture layer held at or above `share` of its own peak.
    FilmSpectralProfile floored(float share) const;

    /// Manufacturer-graph sensitivity samples on SpectralGrid's wavelength axis.
    static FilmSpectralProfile measured(const std::vector<std::vector<float>>& layerSensitivity,
                                        FilmDyeFamily dyeFamily);

    /// Smooth compatibility approximations for custom profiles without a retained trace.
    static FilmSpectralProfile color(const std::vector<float>& peaksNM,
                                     const std::vector<float>& widthsNM,
                                     FilmDyeFamily dyeFamily);
    static FilmSpectralProfile color(const std::vector<float>& peaksNM, FilmDyeFamily dyeFamily) {
        return color(peaksNM, {58, 46, 42}, dyeFamily);
    }

    struct Sigmas {
        float left;
        float right;
    };
    static Sigmas colorLayerSigmas(int layer, float widthNM);
    static float colorLayerSensitivity(int layer, float peakNM, float widthNM, float wavelength);

    /// Where a panchromatic emulsion's three bands sit, and how wide they are.
    static const std::vector<float>& monochromeBandCentresNM();
    static const std::vector<float>& monochromeBandWidthsNM();

    /// Panchromatic silver response constructed from broad spectral bands.
    static FilmSpectralProfile monochrome(const std::vector<float>& rgbWeights);

    /// Compatibility profile for stocks with the historical RGB matrix but no spectral data.
    static FilmSpectralProfile approximation(const std::vector<std::vector<float>>& sensitivity,
                                             bool monochrome, bool reversal);

    uint64_t signature() const;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_SPECTRAL_PROFILE_H
