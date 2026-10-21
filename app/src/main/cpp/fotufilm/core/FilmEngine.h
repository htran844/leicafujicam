// Port of Sources/FotufilmCore/FilmEngine.swift: FilmEngineFeature, FilmOutputTransform,
// FilmInputTransform, FilmFlareFrame and FilmEngineInvocation. The ToneBase.swift extension of
// FilmEngineInvocation is declared here and defined in ToneBase.cpp. `withSpectralPointers`
// (lending the LUTs to a Halide call) has no counterpart: the host reads `spectral` directly.
#ifndef FOTUFILM_CORE_FILM_ENGINE_H
#define FOTUFILM_CORE_FILM_ENGINE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "ExposureSampler.h"
#include "FilmGrain.h"
#include "FotufilmHalide.h"
#include "Pipeline.h"
#include "SpectralModel.h"
#include "ToneBase.h"

namespace fotufilm {

/// Feature bits selecting which physical stages a pipeline variant includes.
namespace FilmEngineFeature {
constexpr int32_t flare = FOTUFILM_FRAME_FLARE;
constexpr int32_t mtf = FOTUFILM_FRAME_MTF;
constexpr int32_t halation = FOTUFILM_FRAME_HALATION;
constexpr int32_t couplers = FOTUFILM_FRAME_COUPLERS;
constexpr int32_t adjacency = FOTUFILM_FRAME_ADJACENCY;
constexpr int32_t grain = FOTUFILM_FRAME_GRAIN;
constexpr int32_t reversal = FOTUFILM_FRAME_REVERSAL;
constexpr int32_t monochrome = FOTUFILM_FRAME_MONOCHROME;
constexpr int32_t floatIO = FOTUFILM_FRAME_FLOAT_IO;
constexpr int32_t mtfLuma = FOTUFILM_FRAME_MTF_LUMA;
constexpr int32_t couplerDiffusion = FOTUFILM_FRAME_COUPLER_DIFFUSION;
constexpr int32_t realtime = FOTUFILM_FRAME_REALTIME;
constexpr int32_t exactMath = FOTUFILM_FRAME_EXACT_MATH;
constexpr int32_t grainMottle = FOTUFILM_FRAME_GRAIN_MOTTLE;
constexpr int32_t printMTF = FOTUFILM_FRAME_PRINT_MTF;
constexpr int32_t flareMeasure = FOTUFILM_FRAME_FLARE_MEASURE;
constexpr int32_t encodeOut = FOTUFILM_FRAME_ENCODE_OUT;
constexpr int32_t lightOut = FOTUFILM_FRAME_LIGHT_OUT;
constexpr int32_t fieldsIn = FOTUFILM_FRAME_FIELDS_IN;
constexpr int32_t noFilm = FOTUFILM_FRAME_NO_FILM;
constexpr int32_t outputLinear = FOTUFILM_FRAME_OUTPUT_LINEAR;
constexpr int32_t outputPower = FOTUFILM_FRAME_OUTPUT_POWER;
constexpr int32_t outputLog = FOTUFILM_FRAME_OUTPUT_LOG;
constexpr int32_t densityOut = FOTUFILM_FRAME_DENSITY_OUT;
constexpr int32_t densityIn = FOTUFILM_FRAME_DENSITY_IN;
constexpr int32_t texture = FOTUFILM_FRAME_TEXTURE;
constexpr int32_t diffusion = FOTUFILM_FRAME_DIFFUSION;
constexpr int32_t donorLayer = FOTUFILM_FRAME_DONOR_LAYER;
constexpr int32_t annularHalation = FOTUFILM_FRAME_HALATION_ANNULAR;
}  // namespace FilmEngineFeature

/// The step out of the print's delivery basis and into a host's own space.
struct FilmOutputTransform {
    /// Mirrors FOTUFILM_CONFIG_OUTPUT_TRANSFER.
    enum class Transfer : int32_t { linear = 0, powerLaw = 1, logarithmic = 2 };

    /// Row-major 3x3 out of the print's Display P3 delivery basis into the host's primaries.
    std::vector<float> matrix;
    Transfer transfer;
    /// c0 through c5.
    std::vector<float> coefficients;
    bool premultiplied;
    /// The SDR shoulder knee; nil is no shoulder.
    std::optional<float> shoulderKnee;
    /// Optional luminance weights in the host's primaries enabling the gamut fit.
    std::optional<Float3> gamutLuminance;

    FilmOutputTransform(std::vector<float> matrix, Transfer transfer, std::vector<float> coefficients,
                        bool premultiplied, std::optional<float> shoulderKnee = std::nullopt,
                        std::optional<Float3> gamutLuminance = std::nullopt);
};

/// The step the other way: out of a host's own space and into the engine's scene working space.
struct FilmInputTransform {
    /// Mirrors FOTUFILM_DECODE_TRANSFER.
    enum class Transfer : int32_t { linear = 0, powerLaw = 1, exponential = 2 };

    std::vector<float> matrix;
    Transfer transfer;
    std::vector<float> coefficients;
    bool premultiplied;

    FilmInputTransform(std::vector<float> matrix, Transfer transfer, std::vector<float> coefficients,
                       bool premultiplied);
    /// The FOTUFILM_DECODE_* layout (17 floats).
    std::vector<float> parameters() const;
};

/// Area-weighted scene samples shared by every stock rendered from one realtime frame.
class FilmFlareFrame {
public:
    /// Float scene rows already in the working space, linear Rec.2020 RGBA.
    static FilmFlareFrame linearRec2020RGBA(const float* pixels, int width, int height);
    static FilmFlareFrame encodedDisplayP3RGBA(const uint8_t* pixels, int width, int height);
    static FilmFlareFrame srgbRGBA(const uint8_t* pixels, int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    int gridWidth() const { return gridWidth_; }
    const std::vector<Float3>& averages() const { return averages_; }
    const std::vector<int64_t>& counts() const { return counts_; }
    struct Position {
        int x;
        int y;
    };
    const std::vector<Position>& positions() const { return positions_; }

    /// Not upstream: the same samples with each cell's position read on a frame of
    /// `frameWidth`×`frameHeight` (by the same centre rule), for a measurement taken on a
    /// downsampled copy of that frame. Averages, counts and the mean's area stay the copy's.
    FilmFlareFrame reprojected(int frameWidth, int frameHeight) const;

private:
    FilmFlareFrame(int width, int height, const std::function<Float3(size_t)>& sample);
    static FilmFlareFrame encoded(const uint8_t* pixels, int width, int height, bool convertFromSRGB);

    int width_;
    int height_;
    int gridWidth_;
    std::vector<Float3> averages_;
    std::vector<int64_t> counts_;
    std::vector<Position> positions_;
};

/// One packed description of a render: the packed configuration, feature mask, seed, and spectral
/// LUTs the renderer consumes.
class FilmEngineInvocation {
public:
    static constexpr int couplerWarpSamples = FOTUFILM_COUPLER_WARP_SAMPLES;
    static constexpr float couplerWarpMin = FOTUFILM_COUPLER_WARP_MIN;
    static constexpr float couplerWarpMax = FOTUFILM_COUPLER_WARP_MAX;
    static constexpr int toneGridCells = FOTUFILM_TONE_GRID_CELLS;
    static constexpr int sampledCurveStride = FOTUFILM_SAMPLED_CURVE_STRIDE;
    static constexpr int sampledCurvesOffset = FOTUFILM_CONFIG_SAMPLED_CURVES;
    static constexpr int configurationCount = FOTUFILM_FRAME_CONFIGURATION_COUNT;
    static constexpr int gradeSpaceOffset = FOTUFILM_CONFIG_GRADE_SPACE;
    static constexpr int mottleSigmaOffset = FOTUFILM_CONFIG_MOTTLE_SIGMA;
    static constexpr int mottleOffset = FOTUFILM_CONFIG_MOTTLE;
    static constexpr int paperRedOffset = FOTUFILM_CONFIG_PAPER_RED;
    static constexpr int paperBlueOffset = FOTUFILM_CONFIG_PAPER_BLUE;
    static constexpr int paperMidpointRedOffset = FOTUFILM_CONFIG_PAPER_MIDPOINT_RED;
    static constexpr int paperMidpointBlueOffset = FOTUFILM_CONFIG_PAPER_MIDPOINT_BLUE;
    static constexpr int grainLawOffset = FOTUFILM_CONFIG_GRAIN_LAW;
    static constexpr int grainAnchorOffset = FOTUFILM_CONFIG_GRAIN_ANCHOR;
    static constexpr int grainFogOffset = FOTUFILM_CONFIG_GRAIN_FOG;
    static constexpr int grainSigmaLayerOffset = FOTUFILM_CONFIG_GRAIN_SIGMA_LAYER;
    static constexpr int mottleSigmaLayerOffset = FOTUFILM_CONFIG_MOTTLE_SIGMA_LAYER;
    static constexpr int printMTFOffset = FOTUFILM_CONFIG_PRINT_MTF_SIGMA;
    static constexpr int outputMatrixOffset = FOTUFILM_CONFIG_OUTPUT_MATRIX;
    static constexpr int outputTransferOffset = FOTUFILM_CONFIG_OUTPUT_TRANSFER;
    static constexpr int outputCoefficientsOffset = FOTUFILM_CONFIG_OUTPUT_COEFFICIENTS;
    static constexpr int outputPremultipliedOffset = FOTUFILM_CONFIG_OUTPUT_PREMULTIPLIED;
    static constexpr int printSharpenOffset = FOTUFILM_CONFIG_PRINT_SHARPEN;
    static constexpr int diffusionDirectOffset = FOTUFILM_CONFIG_DIFFUSION_DIRECT;
    static constexpr int diffusionKernelOffset = FOTUFILM_CONFIG_DIFFUSION_KERNEL;
    static constexpr int diffusionRadiusOffset = FOTUFILM_CONFIG_DIFFUSION_RADIUS;
    static constexpr int donorCurveOffset = FOTUFILM_CONFIG_DONOR_CURVE;
    static constexpr int donorReleaseOffset = FOTUFILM_CONFIG_DONOR_RELEASE;
    static constexpr int halationRingRadiusOffset = FOTUFILM_CONFIG_HALATION_RING_RADIUS;
    static constexpr int halationMatrixOffset = FOTUFILM_CONFIG_HALATION_MATRIX;
    static constexpr int curveSecondaryOffset = FOTUFILM_CONFIG_CURVE_SECONDARY;
    static constexpr int mtfSecondarySigmaOffset = FOTUFILM_CONFIG_MTF_SECONDARY_SIGMA;
    static constexpr int mtfSecondaryRadiusOffset = FOTUFILM_CONFIG_MTF_SECONDARY_RADIUS;
    static constexpr int mtfPrimaryShareOffset = FOTUFILM_CONFIG_MTF_PRIMARY_SHARE;
    static constexpr int couplerReleaseGammaOffset = FOTUFILM_CONFIG_COUPLER_RELEASE_GAMMA;
    static constexpr int donorReleaseGammaOffset = FOTUFILM_CONFIG_DONOR_RELEASE_GAMMA;
    static constexpr int donorDiffusionKernelOffset = FOTUFILM_CONFIG_DONOR_DIFFUSION_KERNEL;
    static constexpr int developComplementOffset = FOTUFILM_CONFIG_DEVELOP_COMPLEMENT;
    static constexpr int grainDensityProfileOffset = FOTUFILM_CONFIG_GRAIN_DENSITY_PROFILE;
    static constexpr int grainDensityRecordsOffset = FOTUFILM_CONFIG_GRAIN_DENSITY_RECORDS;
    static constexpr int outputShoulderOffset = FOTUFILM_CONFIG_OUTPUT_SHOULDER;
    static constexpr int outputGamutOffset = FOTUFILM_CONFIG_OUTPUT_GAMUT;
    static constexpr int adjacencyModelOffset = FOTUFILM_CONFIG_ADJACENCY_MODEL;
    static constexpr int adjacencySecondarySigmaOffset = FOTUFILM_CONFIG_ADJACENCY_SECONDARY_SIGMA;
    static constexpr int adjacencySecondaryRadiusOffset = FOTUFILM_CONFIG_ADJACENCY_SECONDARY_RADIUS;
    static constexpr int chromaticFringeAmountOffset = FOTUFILM_CONFIG_CHROMATIC_FRINGE_AMOUNT;
    static constexpr int chromaticFringeSigmaOffset = FOTUFILM_CONFIG_CHROMATIC_FRINGE_SIGMA;
    static constexpr int chromaticFringeRadiusOffset = FOTUFILM_CONFIG_CHROMATIC_FRINGE_RADIUS;
    static constexpr int grainReversalProfileOffset = FOTUFILM_CONFIG_GRAIN_REVERSAL_PROFILE;
    static constexpr int grainModeOffset = FOTUFILM_CONFIG_GRAIN_MODE;
    static constexpr int cameraPreflashOffset = FOTUFILM_CONFIG_CAMERA_PREFLASH;
    static constexpr int printerPreflashOffset = FOTUFILM_CONFIG_PRINTER_PREFLASH;
    static constexpr int byteBasisOffset = FOTUFILM_CONFIG_BYTE_BASIS;
    static constexpr int grainOffset = FOTUFILM_CONFIG_GRAIN;
    static constexpr int grainSigmaOffset = FOTUFILM_CONFIG_GRAIN_SIGMA;
    static constexpr int flareOffset = FOTUFILM_CONFIG_FLARE;
    static constexpr int flareMeanOffset = FOTUFILM_CONFIG_FLARE_MEAN;
    static constexpr int couplerWarpOffset = FOTUFILM_CONFIG_COUPLER_WARP;
    static constexpr int whiteBalanceOffset = FOTUFILM_CONFIG_WHITE_BALANCE;
    static constexpr int sceneAdjustOffset = FOTUFILM_CONFIG_HIGHLIGHTS;
    static constexpr int halationKernelOffset = FOTUFILM_CONFIG_HALATION_KERNEL;
    static constexpr int gradeOffset = FOTUFILM_CONFIG_GRADE_LIFT;
    static constexpr int frameSizeOffset = FOTUFILM_CONFIG_FRAME_WIDTH;
    static constexpr int toneGridSizeOffset = FOTUFILM_CONFIG_TONE_GRID_WIDTH;
    static constexpr int toneGridAOffset = FOTUFILM_CONFIG_TONE_GRID_A;
    static constexpr int toneGridBOffset = FOTUFILM_CONFIG_TONE_GRID_B;
    static constexpr int exposureGainOffset = FOTUFILM_CONFIG_EXPOSURE_GAIN;

    std::vector<float> configuration;
    /// Keeps the frame's canonical Film bank alive across cache eviction.
    std::shared_ptr<const FilmGrain::TileBinding> filmTileBinding;
    /// The Film grain population this develop draws, bound or not; nil under another model.
    std::optional<FilmGrain::Population> filmGrainPopulation;
    SpectralPipelineTables spectral;
    uint64_t spectralCacheID = 0;
    int32_t featureMask = 0;
    uint32_t seed = 0;
    /// Whether the tone controls may key to the regional base (Options.localTone).
    bool localToneEnabled = false;
    /// Pixels of context a tile must carry on each cut edge.
    int spatialSupport = 0;
    /// `spatialSupport` with halation's reach removed.
    int spatialSupportSansHalation = 0;
    /// The reach of the light chain alone.
    int lightSupport = 0;
    /// Halation's own reach.
    int halationSupport = 0;
    /// The three halation radii in pixels.
    std::vector<int32_t> halationPixelRadii;

    /// `init(validating:...)`: throws FilmDevelopmentError (unsupported development), TransportError
    /// (invalid halation return) or whatever `checkCancellation` throws. `bindsFilmGrain: false`
    /// skips preparing the Film grain population and leaves the film tile block zero.
    FilmEngineInvocation(const FilmStock& stock, const FotufilmEngine::Options& options, int width, int height,
                         uint64_t frameIndex = 0, bool noFilm = false, bool bindsFilmGrain = true,
                         const CheckCancellation& checkCancellation = {});

    // ---- grain lattice mathematics ----
    static constexpr float grainSigmaFloorPixels = 0.151f;
    static float sampledGaussianCorrelation(float sigma);
    static float gaussianClumpCorrelation(float clumpSigmaPixels, float foldSigmaPixels, float foldKeep);
    static float discreteGrainSigma(float clumpSigmaPixels, float foldSigmaPixels, float foldKeep);
    static float booleanGrainCorrelation(float discRadiusPixels, float coverage, float foldSigmaPixels,
                                         float foldKeep);
    static float silverEquivalentClumpPixels(float discRadiusPixels, float coverage, float foldSigmaPixels,
                                             float foldKeep);
    static float discreteSilverGrainSigma(float discRadiusPixels, float coverage, float foldSigmaPixels,
                                          float foldKeep);
    static constexpr float noSpatialReachPixels = 0.1f;
    static int gaussianRadius(float sigma);
    static int halationBoxRadius(float sigmaPixels);
    static int boxPyramidSupport(int radius, int stride, float ringRadius = 0);
    static float sampledGaussianVariance(float sigma);
    static int32_t sceneSideFeatures();
    static int32_t staged(int32_t mask, PipelineStage stage);
    static float effectiveCouplerScale(float requested);
    static std::optional<float> sampledFilmDensity(const std::vector<float>& configuration, int channel,
                                                   float logExposure);
    static std::vector<float> couplerWarp(const std::vector<CharacteristicCurve>& curves,
                                          const std::vector<std::vector<float>>& inhibition, float scale,
                                          const std::vector<float>& releaseGamma = {1, 1, 1},
                                          const DonorCaptureLayer* donor = nullptr);
    static float inhibitorRelease(float activation, float gamma);

    /// Correctly shaped, entirely zero tables for a no-film invocation.
    static const SpectralPipelineTables& unusedSpectralTables();

    void setOutputTransform(const FilmOutputTransform& transform);
    std::optional<Float3> flareMean() const;
    void setFlareMean(std::optional<Float3> value);

    Float3 measuredFlareMean(const float* linearRGBA, int width, int rows) const;
    Float3 measuredAreaWeightedFlareMean(const float* linearRGBA, int width, int height) const;
    Float3 measuredAreaWeightedFlareMean(const FilmFlareFrame& frame) const;
    Double3 flareExposureSum(const float* linearRGBA, int width, int rows) const;
    void flareExposureRowSums(const float* linearRGBA, int width, int rows, int firstRow, Double3* sums) const;
    void flareExposureRowSums(const float* red, const float* green, const float* blue, int width, int rows,
                              int firstRow, Double3* sums) const;

    /// Re-packs the film's records (and the donor's) as `setup` exposes them.
    void packFilmCurves(const FilmStock& stock, const LabScanTiming::Setup& setup);

    // ---- ToneBase.swift ----
    bool toneControlsActive() const;
    bool localToneActive() const { return localToneEnabled && toneControlsActive(); }
    bool toneKeyedLocally() const;
    bool screenLevelsReachLight() const;
    bool labScanMeters() const;
    /// Metered levels and local tone share one whole-frame measurement.
    bool sceneMeteringActive() const { return localToneActive() || meterStock_.has_value(); }
    void copyMeteredLevels(const FilmEngineInvocation& measured);
    /// A fresh accumulator sized and weighted for this invocation's frame.
    ToneBaseMeasurement toneBaseMeasurement() const;
    /// The same accumulator for a buffer of another size (a downsampled whole frame).
    ToneBaseMeasurement toneBaseMeasurement(int frameWidth, int frameHeight) const;
    std::optional<float> sceneHighlightStops(const ToneBaseMeasurement& measurement) const;
    std::optional<Float3> sceneToneStops(const ToneBaseMeasurement& measurement) const;
    std::optional<Float3> sceneChannelMedians(const ToneBaseMeasurement& measurement) const;
    /// Solves the accumulated measurement and pins the grid into the packed configuration.
    void setToneBase(const ToneBaseMeasurement& measurement);
    void measureToneBasePlanar(const float* red, const float* green, const float* blue, int width, int height);
    void measureToneBase(const float* linearRGBA, int width, int height);
    void measureToneBaseSRGB(const uint8_t* bytes, int width, int height);
    void measureToneBaseEncodedDisplayP3(const uint8_t* bytes, int width, int height);

private:
    static std::vector<float> parameters(const CharacteristicCurve& curve);
    static std::vector<float> sampledParameters(const CharacteristicCurve& curve);
    static std::vector<float> secondaryParameters(const CharacteristicCurve& curve);
    void withExposureSampler(const std::function<void(const ExposureSampler&)>& body) const;
    void applyMeteredLevels(MeteredLevels levels);
    std::optional<FilmStock> screenMeterStock() const;
    std::optional<AutoAdjustment::SceneStops> screenScene(const ToneBaseMeasurement& measurement) const;

    /// The stock and medium this develop meters its levels for, when it meters at all.
    std::optional<FilmStock> meterStock_;
    PrintPaper meterMedium_ = PrintPaper::screen;
    float meterExposureEV_ = 0;
    float meterDodging_ = 1;
    float meterScanExposure_ = 0;
    Float3 meterKeys_ = Float3();
    MeteredLevels meterLevels_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_ENGINE_H
