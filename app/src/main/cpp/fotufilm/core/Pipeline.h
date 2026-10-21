// Port of Sources/FotufilmCore/Pipeline.swift: FotufilmEngine.Options, with its extensions from
// UnexposedEdge.swift (`pixelsPerMM`, gate and carrier configuration) and HalationModel.swift
// (`transportConstruction`, `withoutLayeredTransport`). The engine's `process*` entry points are
// the GLES graph's job and are not ported. Options whose features are not ported keep their
// upstream defaults: `layeredTransport`/`halationModel`/`transportBackend` and the three layered
// controls are carried but never select a construction; `lensFilters` (LensFilterStack) and
// `scanReading` (ApproximateNegativeScan) are absent and behave as their defaults (no filter, a
// print span handed densities).
#ifndef FOTUFILM_CORE_PIPELINE_H
#define FOTUFILM_CORE_PIPELINE_H

#include <cstdint>
#include <optional>
#include <vector>

#include "AdjacencyModel.h"
#include "ColorGrade.h"
#include "DiffusionFilter.h"
#include "DigitalReferenceReceiver.h"
#include "Enlarger.h"
#include "FilmFormat.h"
#include "FilmGrain.h"
#include "FilmSpectralProfile.h"
#include "GrainModel.h"
#include "HalationModel.h"
#include "LayeredTransport.h"
#include "PipelineStage.h"
#include "PrintFinish.h"
#include "PrintPaper.h"
#include "PrinterLights.h"
#include "PrinterProfile.h"
#include "Simd.h"
#include "UnexposedEdge.h"
#include "WhiteBalance.h"

namespace fotufilm {

struct FilmStock;

/// The film simulation pipeline.
struct FotufilmEngine {
    struct Options {
        /// Exposure compensation in stops (applied to the camera exposure).
        float exposureEV = 0;
        /// Source range above diffuse white as a linear multiple. One is SDR identity.
        float sceneHeadroom = 1;
        /// The fraction of `sceneHeadroom`'s stops above white that reaches the film, 0...1.
        float hdrRange = 1;
        /// How strongly the kept range is rolled off into the film's latitude, 0...2.
        float hdrRollOff = 1;
        /// Spectral scene-light edit (mired displacement from D65, tint in delta-uv units).
        WhiteBalance whiteBalance = WhiteBalance::neutral();
        /// Scene-referred highlight shaping, -1...1.
        float highlights = 0;
        /// Scene-referred shadow shaping, -1...1.
        float shadows = 0;
        /// Whether the highlight/shadow shifts key to the regional base.
        bool localTone = true;
        /// Chroma multiplier about each pixel's own luminance.
        float saturation = 1;
        /// Signed chroma boost weighted toward the least colourful pixels, -1...1.
        float vibrance = 0;
        /// Multiplier on the stock's grain strength (0 disables grain).
        float grainScale = 1;
        /// Override of the stock's grain-size mixture share, 0...0.9; nil renders the stock's own.
        std::optional<float> grainMottleShare;
        /// Coarse-population clump radius as a multiple of `grainSizeMM`, clamped to 1...8.
        std::optional<float> grainMottleSizeRatio;
        static constexpr float deliveryMottleSizeRatio = 8;
        /// Applies `deliveryMottleSizeRatio` when a mixture share is explicit but its size is not.
        void completeDeliveryMottle();
        /// Which model develops the grain.
        GrainModel grainModel = GrainModel::clumpField;
        /// How the film grain model lays its grain.
        FilmGrain::Look filmGrain;
        /// Multiplier on the fraction of light the base returns (0 disables halation).
        float halationScale = 1;
        /// Red returned/direct exposure ratio, 0...1; nil follows the stock.
        std::optional<float> halationReturnRatio;
        /// How much the halo keeps the source's own colour instead of the stock's layered red, 0…1.
        float halationSourceColour = 0;
        /// Per-wavelength gain on the full-grid halation return spectrum; empty preserves the stock.
        std::vector<float> halationReturnGain;
        /// Override of the stock's `halationHazeMM`.
        std::optional<float> halationHazeMM;
        /// Uses a provisional spatial profile when no independently calibrated profile exists.
        bool useEstimatedHalationProfile = false;
        HalationModel halationModel = HalationModel::legacy;
        /// Explicit research construction override (layered transport is not ported).
        std::optional<LayeredTransport> layeredTransport;
        TransportBackend transportBackend = TransportBackend::cpu;
        float antiHalationScale = 1;
        float baseThicknessScale = 1;
        float pressurePlateReflectance = 0;
        /// Multiplier on taking-lens veiling glare. 0 by default.
        float flareScale = 0;
        /// Uniform additive camera film preflash relative to metered mid-grey, 0...0.25.
        float cameraPreflash = 0;
        /// Strength of DIR coupler inhibition (0 disables inter-image effects).
        float couplerScale = 1;
        /// Optional adjacency-model override.
        std::optional<AdjacencyModel> adjacencyModel;
        std::optional<float> chromaticFringeAmount;
        std::optional<float> chromaticFringeRadiusMM;
        /// Multiplier on how far the released inhibitor reaches through the layer stack.
        float couplerRangeScale = 1;
        /// The same reach per interlayer; nil applies `couplerRangeScale` to both.
        std::optional<std::vector<float>> couplerGapReachScales;
        /// Multiplier on the retained self-inhibition.
        float couplerSelfScale = 1;
        /// Years the roll sat past its process-by date at room temperature.
        float expiredYears = 0;
        /// Shutter duration in seconds, for reciprocity.
        std::optional<float> shutterSeconds;
        /// Stops of push (positive) or pull (negative) development.
        float developmentEV = 0;
        /// How much of the developed silver the bleach leaves in the negative, 0...1.
        float bleachBypass = 0;
        /// Optional digital correction of film channel-contrast mismatch. Off by default.
        float printCorrection = 0;
        /// The lamp house the negative is enlarged under.
        Enlarger enlarger = Enlarger::diffuser;
        /// Optional simulated optical printer.
        std::optional<PrinterProfile> printer;
        /// The release printer's red, green and blue lights, in printer points.
        PrinterLights printerLights = PrinterLights::balanced();
        /// Uniform additive paper preflash relative to paper mid-exposure, 0...0.10.
        float printerPreflash = 0;
        /// Where the developed image is finished; nil takes the stock's own medium. Read it
        /// through `paperFor`.
        std::optional<PrintPaper> paper;
        /// The lamp a physical print is viewed under; nil uses the medium's reference.
        std::optional<float> printViewingKelvin;
        /// The grade laid over the finished image.
        ColorGrade grade = ColorGrade::neutral();
        ColorGrade::Space gradeSpace = ColorGrade::Space::linear;
        /// Film gauge the frame is exposed on.
        FilmFormat format = FilmFormat::still35();
        /// Fraction of the film frame's short edge the rendered image spans, 0...1.
        float frameCoverage = 1;
        /// Seed for the grain generator.
        uint64_t seed = 0x46494C4D;
        /// Show the developed negative rather than the positive medium.
        std::optional<NegativeViewing> negativeViewing;
        DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle;
        /// The frame's brightest content, metered by the host; nil requests the whole-frame meter.
        std::optional<float> sceneHighlightStops;
        /// The frame's per-record medians, metered like `sceneHighlightStops`.
        std::optional<Float3> sceneChannelMedians;
        /// The frame's median, darker tenth and brighter twentieth (x, y, z).
        std::optional<Float3> sceneToneStops;
        /// Set when this develop is the unexposed film just outside the camera gate.
        std::optional<UnexposedEdge::Develop> unexposedEdge;
        /// Variable-contrast paper grade 0…5 for Digital Reference's graded styles.
        float screenGrade = 2;
        /// The peaks of the three bands Digital Reference reads a colour negative through.
        ReceiverBands receiverBands = ReceiverBands::paper();
        /// The exposure of the screen conversion, in stops.
        float screenExposureEV = 0;
        /// Lab Scan's dodging, as a share of the scanner's own.
        float labScanDodging = 1;
        /// Lab Scan's finish, as a share of the minilab scanner's colour and tone.
        float labScanLook = 1;
        /// Digital Reference and Lab Scan colour-negative correction, -1...1.
        Float3 screenCMY = Float3();
        /// Which span of the pipeline this render performs.
        PipelineStage stage = PipelineStage::full;
        /// Which spatial stages `stage == texture` lays over the frame.
        TextureStages textureStages = TextureStages::all();
        /// A scanned negative's light controls on its print, read only by `stage == print`.
        PrintFinish printFinish = PrintFinish::neutral();
        /// Explicit source illuminant for film exposure; nil uses the stock's own reference.
        std::optional<float> sceneIlluminantKelvin;
        /// Explicit source xy white.
        std::optional<Float2> sceneIlluminantChromaticity;
        /// Measured scene SPD on 380...780 nm in 5 nm steps (81 samples).
        std::vector<float> sceneIlluminantSpectrum;
        /// A diffusion filter on the front of the lens; nil is no filter.
        std::optional<DiffusionFilter> diffusionFilter;
        /// The lens's focal length in millimetres; nil is the gauge's normal lens.
        std::optional<float> focalLengthMM;

        WhiteBalance resolvedSceneIlluminant(float referenceKelvin = WhiteBalance::neutralKelvin) const;
        std::vector<float> resolvedSceneSpectrum(float referenceKelvin = WhiteBalance::neutralKelvin) const;
        /// Colorimetric approximation when no film is loaded.
        Float3 sceneLightGains() const;

        /// `paper(for:)`: the medium this stock actually develops onto under these options.
        PrintPaper paperFor(const FilmStock& stock) const;
        /// Whether this render may deliver light above display white.
        bool supportsHDRDelivery(const FilmStock& stock) const;
        /// The SDR delivery knee.
        float sdrShoulderKnee(const FilmStock& stock) const;

        // HalationModel.swift
        /// The layered construction this render would use; the layered renderer is not ported, so
        /// this engine never takes one and the answer is always nil.
        std::optional<LayeredTransport> transportConstruction(const FilmStock& stock) const;
        Options withoutLayeredTransport() const;

        // UnexposedEdge.swift
        /// Pixels per millimetre of film for a buffer of this size.
        float pixelsPerMM(int width, int height) const;
        double pixelPitchMM(int width, int height) const;
        int frameShortEdgePixels(int width, int height) const;
        /// FOTUFILM_CONFIG_GATE for a buffer of this size.
        std::vector<float> gateConfiguration(int width, int height) const;
        /// FOTUFILM_CONFIG_CARRIER for a buffer of this size.
        std::vector<float> carrierConfiguration(int width, int height) const;
        /// FOTUFILM_CONFIG_GATE_CORNER.
        float gateCornerConfiguration(int width, int height) const;
    };
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PIPELINE_H
