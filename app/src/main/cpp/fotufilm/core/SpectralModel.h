// Port of Sources/FotufilmCore/SpectralModel.swift: SpectralLUT, SpectralPipelineTables,
// SpectralRuntime, MeasuredReflectanceTable, ReceiverNeutralCalibration and ScreenToeBalance.
// FilmDyeFamily/NegativeViewing/FilmSpectralProfile are in FilmSpectralProfile.h and SpectralGrid
// in SpectralGrid.h. SpectralRuntime's extensions in FilmAbsorption.swift and
// PrintIllumination.swift are declared here and defined in FilmAbsorption.cpp and
// PrintIllumination.cpp. The .cpp is split by topic:
//   SpectralModel.cpp          SpectralLUT, MeasuredReflectanceTable, ReceiverNeutralCalibration,
//                              ScreenToeBalance, SpectralGrid.paper*
//   SpectralRuntimeExposure.cpp the exposure table's domain, scene-light tables
//   SpectralRuntime.cpp        tables / buildTables / cacheIdentifier / scan calibration / readers
//   SpectralRuntimePrint.cpp   neutralPrintingBalance, neutralToneScale, print receiver, trims
#ifndef FOTUFILM_CORE_SPECTRAL_MODEL_H
#define FOTUFILM_CORE_SPECTRAL_MODEL_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "Densitometry.h"
#include "DigitalReferenceReceiver.h"
#include "FilmStock.h"
#include "PrintPaper.h"
#include "PrinterLights.h"
#include "PrinterProfile.h"
#include "Simd.h"
#include "StatusADyeUnmix.h"

namespace fotufilm {

/// A compact three-channel 3D lookup table, RGBA-interleaved for direct upload into an RGBA32F 3D
/// texture. Copies share the immutable storage.
class SpectralLUT {
public:
    SpectralLUT(int dimension, std::vector<float> values);

    int dimension() const { return dimension_; }
    const std::vector<float>& values() const { return *values_; }
    const float* data() const { return values_->data(); }

    /// Tetrahedral interpolation; non-finite coordinates sample to zero.
    Float3 sample(Float3 p) const { return sample(p, values_->data(), dimension_); }
    static Float3 sample(Float3 p, const float* values, int dimension);

private:
    int dimension_;
    std::shared_ptr<const std::vector<float>> values_;
};

/// Runtime tables consumed by the renderer.
struct SpectralPipelineTables {
    /// Exposure-domain scene light -> normalized film-layer exposure, indexed in the basis
    /// returned by `ColorScience::linearRec2020ToExposureDomain`.
    SpectralLUT exposure;
    /// Negative: density -> relative log paper exposures.
    SpectralLUT filmOutput;
    /// Paper layer density -> display-linear RGB; nullopt when film is viewed directly.
    std::optional<SpectralLUT> paperOutput;
};

/// Bounded spectra fitted to the Gaussian prior of the measured-reflectance corpus
/// (`rec2020-reflectance-prior.coeff`).
class MeasuredReflectanceTable {
public:
    /// Parses the resource and installs it as the process-wide table. Returns false (and leaves the
    /// current table in place) when the bytes are not a valid RPR1 resource. Install before the
    /// first table is built: tables cached before the install keep the model they were built with.
    static bool install(const uint8_t* bytes, size_t size);
    /// `MeasuredReflectanceTable.shared`; nullptr until installed.
    static std::shared_ptr<const MeasuredReflectanceTable> shared();
    /// Parses without installing; nullptr on malformed input.
    static std::shared_ptr<const MeasuredReflectanceTable> load(const uint8_t* bytes, size_t size);

    MeasuredReflectanceTable(int dimension, int bandCount, int sourceCount, float anchor, std::vector<float> data);

    /// Recovers at the table anchor and then follows the engine's brightness ray.
    std::vector<float> reflectance(Float3 input) const;

    int dimension;
    int bandCount;
    int sourceCount;
    float anchor;
    std::vector<float> data;
};

/// Per record, moved receiver bands' read of a stock's neutral wedge against the paper bands'
/// read of the same step, rising.
struct ReceiverNeutralCalibration {
    std::vector<std::vector<Float2>> wedges;

    ReceiverNeutralCalibration(const FilmStock& stock, const std::function<Float3(const std::vector<float>&)>& read,
                               const std::function<Float3(const std::vector<float>&)>& paperRead);
    Float3 operator()(Float3 read) const;
};

/// Each record's toe carried onto green's along a neutral wedge (Auto Levels).
struct ScreenToeBalance {
    std::optional<std::vector<std::vector<Float2>>> wedges;
    static constexpr float start = 0.5f;

    Float3 operator()(Float3 read) const;
};

/// Builds and caches the LUT acceleration of the wavelength-domain model.
namespace SpectralRuntime {

using DensityRead = std::function<Float3(const std::vector<float>&)>;
using DensityMap = std::function<std::vector<float>(const std::vector<float>&)>;

constexpr int lutDimension = 33;

/// The `tables(for:...)` / `cacheIdentifier(for:...)` parameters after the stock and paper, with
/// the upstream defaults.
struct TableOptions {
    float bleachBypass = 0;
    std::optional<float> printViewingKelvin;
    float callier = 1;
    std::optional<PrinterProfile> printer;
    std::optional<PrinterLights> printerLights;
    DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle;
    float screenGrade = 2;
    float screenExposureEV = 0;
    float labScanLook = 1;
    ReceiverBands receiverBands = ReceiverBands::paper();
};

/// Evaluates the model over the 33³ grid, z slabs in parallel. `fourth` fills each node's fourth
/// float; empty leaves the constant 1.
SpectralLUT buildLUT(const std::function<Float3(Float3)>& evaluate, const std::function<float(Float3)>& fourth = {});
/// `buildLUT` indexed by each record's density from D-min to D-max.
SpectralLUT buildDensityLUT(const FilmStock& stock, const DensityRead& evaluate);

/// Diagnostic: how many table sets the cache holds and their LUT bytes.
struct RetainedTables {
    size_t sets;
    size_t bytes;
};
RetainedTables retainedTables();

/// Bucket width for print-viewing temperatures, in kelvin.
constexpr float printLightBucketKelvin = 100;
/// The print-viewing gate: nullopt keeps the medium's reference illuminant.
std::optional<float> printLightKelvin(std::optional<float> cct);
/// The lamp the finished print hangs under: daylight from 4000 K, Planckian below.
std::vector<float> printLightSPD(float kelvin);
/// The illuminant a medium's own calibration is judged under.
std::optional<std::vector<float>> referenceViewingLight(PrintPaper paper);

SpectralPipelineTables tables(const FilmStock& stock, PrintPaper paper = kDefaultPrintPaper,
                              const TableOptions& options = TableOptions());

constexpr float reconstructionAnchor = 0.25f;
/// Direct full-grid reconstruction; `linearRGB` is linear Rec.2020.
std::vector<float> reconstructedReflectance(Float3 linearRGB);

constexpr float retainedSilverPerDye = 1;
float retainedSilverFraction(float requested, const FilmStock& stock);
float retainedSilverDensity(const std::vector<float>& density, const std::vector<float>& dMin, float fraction);
float callierCoefficient(float requested, const FilmStock& stock, PrintPaper paper);

/// Identity of the tables a stock needs.
uint64_t cacheIdentifier(const FilmStock& stock, PrintPaper paper = kDefaultPrintPaper,
                         const TableOptions& options = TableOptions());

/// The stock's own neutral density at a log exposure: the mean of its three records.
float neutralDensity(const FilmStock& stock, float logExposure);

Float3 heldToGamut(Float3 colour, float luminance);
Float3 compressedToDisplayGamut(Float3 colour, float luminance);

constexpr float lightBoxBaseLevel = 0.9f;
/// The negative's own dyes viewed by transmission: density -> display RGB.
SpectralLUT negativeViewing(const FilmStock& stock, NegativeViewing look, float bleachBypass = 0);

/// What a direct positive's developed dyes are divided by before they are shown, per layer.
Float3 reversalBalance(const FilmStock& stock, const DensityMap& aligned);

/// Carries record densities onto the image-dye amount basis.
struct NeutralDensityBasis {
    std::vector<Float4> coefficients;
    std::optional<StatusADyeUnmix> statusA;

    std::vector<float> operator()(const std::vector<float>& density) const;
};
NeutralDensityBasis neutralDensityBasis(const FilmStock& stock);

/// The fixed illuminant this emulsion was balanced under.
std::vector<float> filmReferenceIlluminant(const FilmStock& stock);

/// A lens filter as the integration wants it. Lens filters are not ported; the type is kept so the
/// exposure functions keep their upstream shape, and is always absent here.
struct SpectralFilter {
    std::vector<float> transmittance;
    float gain;
    Float3 layerRatio;
};

Float3 anchoredExposure(Float3 rgb, const FilmStock& stock, const std::vector<float>& illuminant,
                        const std::vector<float>* referenceIlluminant = nullptr,
                        const SpectralFilter* filter = nullptr);
Float3 spectralExposure(Float3 rgb, const FilmStock& stock, const std::vector<float>& illuminant,
                        const std::vector<float>* referenceIlluminant = nullptr,
                        const SpectralFilter* filter = nullptr);

/// A colour beyond the margin outside the Rec.2020 cube, as the mixture that stands for it.
struct LocusLight {
    Float3 face;
    Float3 mirror;
    float luminance;
    int lowerBand;
    int upperBand;
    float upperShare;
    float share;
};

/// A cell of the exposure table, read as the light it stands for.
struct SceneLight {
    enum class Kind { reflectance, none, extrapolated, locus };
    Kind kind;
    /// `.reflectance(colour)`; `.extrapolated(face:)`.
    Float3 colour;
    /// `.extrapolated(mirror:)`.
    Float3 mirror;
    /// `.locus(light)`.
    LocusLight locus;
};

Float3 extrapolatedExposure(Float3 face, Float3 mirror);
float extrapolatedExposure(float face, float mirror);

/// The spectral locus as the table builder sees it.
struct SpectralLocusHit {
    double reach;
    int lowerBand;
    int upperBand;
    double upperShare;
};
std::optional<SpectralLocusHit> spectralLocusBoundary(double dx, double dy, double dz);

float projectionMargin();
SceneLight sceneLight(Float3 rgb);
std::vector<float> monochromaticExposure(const LocusLight& light, const std::vector<std::vector<float>>& sensitivities,
                                         const std::vector<float>& illuminant,
                                         const std::vector<float>& referenceIlluminant, const SpectralFilter* filter);
Float3 domainExposure(Float3 point, const FilmStock& stock, const std::vector<float>& illuminant,
                      const std::vector<float>* referenceIlluminant = nullptr, const SpectralFilter* filter = nullptr);
float domainDonorExposure(Float3 point, const DonorCaptureLayer& donor, const std::vector<float>& illuminant,
                          const std::vector<float>& referenceIlluminant, const SpectralFilter* filter = nullptr);

/// The exposure table for a stock under a scene light, over the locus-enclosing domain.
SpectralLUT exposureTable(const FilmStock& stock, const std::vector<float>& illuminant,
                          const std::vector<float>* referenceIlluminant = nullptr,
                          const SpectralFilter* filter = nullptr);
float donorAnchoredExposure(Float3 rgb, const DonorCaptureLayer& donor, const std::vector<float>& illuminant,
                            const std::vector<float>& referenceIlluminant, const SpectralFilter* filter = nullptr);
/// Whether the wavelength-domain reconstruction is loaded.
bool hasReconstructionModel();
std::optional<int> reconstructionSourceCount();
/// The exposure cube's fourth channel, or an empty function (the constant 1).
std::function<float(Float3)> donorChannel(const FilmStock& stock, const std::vector<float>& illuminant,
                                          const std::vector<float>* referenceIlluminant = nullptr,
                                          const SpectralFilter* filter = nullptr);
uint64_t donorSignature(const FilmStock& stock);

/// Density per dye-aligned record through `dyes`, integrated to Display P3.
Float3 transmissionRGB(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                       float neutralDensity = 0, const std::vector<float>* illuminant = nullptr,
                       const std::vector<float>* densityOffset = nullptr);
Float3 paperExposure(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                     const std::vector<float>& lamp, const std::vector<std::vector<float>>& paperSensitivity,
                     float neutralDensity = 0, const std::vector<float>* densityOffset = nullptr);

float effectiveScreenGrade(float grade, const FilmStock& stock, PrintPaper paper,
                           DigitalReferenceStyle digitalReference);
float directViewExposure(float stops, const FilmStock& stock, PrintPaper paper,
                         DigitalReferenceStyle digitalReference);
float effectiveLabScanLook(float look, PrintPaper paper);
ReceiverBands effectiveReceiverBands(const ReceiverBands& bands, const FilmStock& stock, PrintPaper paper);
float screenExposureShift(float stops, const FilmStock& stock, PrintPaper paper,
                          DigitalReferenceStyle digitalReference, float screenGrade);
ScreenToeBalance screenToeBalance(const FilmStock& stock, DigitalReferenceStyle digitalReference,
                                  const DensityRead& read);
DensityRead screenReader(const FilmStock& stock, const ReceiverBands& bands, const DensityRead& relativeEnergy,
                         const std::function<DensityRead()>& paperRelativeEnergy);
/// Empty at the paper's bands.
DensityMap positiveBandShift(const FilmStock& stock, const ReceiverBands& bands);
float screenShadowScale(const FilmStock& stock, PrintPaper paper, DigitalReferenceStyle digitalReference);

/// A transparent positive as Digital Reference reads it.
struct PositiveScreenRead {
    FilmStock stock;
    NeutralDensityBasis basis;
    Float3 balance;
    /// The clear base's brightest channel, as a density below display white.
    float base;

    /// Display-linear RGB at record densities, mid-grey at 18%.
    Float3 transmittance(const std::vector<float>& density) const;
    /// The neutral wedge's luminance at `stops` over mid-grey, as a density below display white.
    float luminance(float stops) const;
};
std::shared_ptr<const PositiveScreenRead> positiveScreenRead(const FilmStock& stock);

Float3 referenceCastOffset(Float3 midEnergy, const FilmStock& stock, PrintPaper paper);
Float3 referenceCastOffset(Float3 midEnergy, const FilmStock& stock, PrintPaper paper, Float2 reference,
                           float ceiling);

/// The density-to-display step a finished positive is read through.
struct PrintReceiver {
    std::vector<std::vector<float>> dyes;
    std::optional<std::vector<float>> viewingLight;
    std::optional<PrintDyeUnmix> unmix;

    Float3 rgb(Float3 density) const;
};
PrintReceiver printReceiver(const FilmStock& stock, PrintPaper paper,
                            const std::optional<std::vector<float>>& viewingLight);

Float3 reflectionPrintDensityTrim(PrintPaper paper);

struct LabScanReferenceSolve {
    float midRatioRed;
    float midRatioBlue;
    std::vector<float> balance;
};
LabScanReferenceSolve labScanReferenceSolve(const FilmStock& stock);

/// The per-channel scaling of print exposure that carries a stock's neutral wedge onto one
/// printed scale, on this medium. FilmStock::printingContrastScale links against this.
std::vector<float> neutralPrintingBalance(const FilmStock& stock, PrintPaper paper);

/// `neutralToneScale(stops:stock:paper:printCorrection:...)`'s defaulted parameters.
struct ToneScaleOptions {
    float callier = 1;
    DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle;
    std::optional<float> sceneHighlightStops;
    float screenGrade = 2;
    float screenExposureEV = 0;
    Float3 screenCMY = Float3();
    ReceiverBands receiverBands = ReceiverBands::paper();
};
/// Display-linear luminance of the finished output for a neutral patch at each of `stops`.
std::vector<float> neutralToneScale(const std::vector<float>& stops, const FilmStock& stock, PrintPaper paper,
                                    float printCorrection, const ToneScaleOptions& options = ToneScaleOptions());

// ---- Scene-light film exposure ----
std::optional<float> sceneLightKelvin(std::optional<float> cct);
/// Weight of the warm (illuminant A) anchor at `cct`, linear in mired between 2856 K and 6504 K.
float sceneLightWarmWeight(float cct);
uint64_t illuminantSignature(const std::vector<float>& illuminant);
struct SceneLightAnchors {
    SpectralLUT warm;
    SpectralLUT cool;
};
SceneLightAnchors sceneLightAnchors(const FilmStock& stock);
/// The exposure-domain table under the exact CIE-locus SPD for the stated CCT.
SpectralLUT sceneExposure(const FilmStock& stock, float cct);
/// The exposure-domain table under a caller-supplied SPD on SpectralGrid.
SpectralLUT sceneExposure(const FilmStock& stock, const std::vector<float>& illuminant);
/// The scene's light as an exact CIE-locus spectrum (D65 table when nullopt).
std::vector<float> sceneIlluminant(std::optional<float> cct);

// ---- FilmAbsorption.swift ----
/// D(lambda) = Dmin(lambda) + sum((record - recordDmin) * dye(lambda)), as the offset over the
/// dye sum; nullopt without a separated base.
std::optional<std::vector<float>> filmDensityOffset(const FilmStock& stock, float scale = 1);
Float3 transmissionRGB(const std::vector<float>& density, const FilmStock& stock, float neutralDensity = 0,
                       const std::vector<float>* illuminant = nullptr);
Float3 paperExposure(const std::vector<float>& density, const FilmStock& stock, const std::vector<float>& lamp,
                     const std::vector<std::vector<float>>& paperSensitivity, float neutralDensity = 0,
                     float densityScale = 1);

// ---- PrintIllumination.swift ----
struct PrintingIllumination {
    std::vector<float> lamp;
    Float3 referenceEnergy;
};
PrintingIllumination printingIllumination(const FilmStock& stock, PrintPaper paper, const std::vector<float>& density,
                                          const std::vector<std::vector<float>>& dyes, float neutralDensity = 0,
                                          float densityScale = 1,
                                          const ReceiverBands& receiverBands = ReceiverBands::paper(),
                                          const std::optional<PrinterLights>& lights = std::nullopt);
std::vector<float> releasePrinterLamp(const std::vector<float>& density, const std::vector<std::vector<float>>& dyes,
                                      const std::vector<std::vector<float>>& sensitivity, Float3 target,
                                      float neutralDensity = 0, const std::vector<float>* densityOffset = nullptr,
                                      Float3 beamGain = Float3::one());

}  // namespace SpectralRuntime
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SPECTRAL_MODEL_H
