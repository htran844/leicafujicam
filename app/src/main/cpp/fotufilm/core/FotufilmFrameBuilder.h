// Not an upstream file: the core's entry point for the GLES renderer. It does what the upstream
// realtime hosts (HalideMetalFilmRenderer.swift) do around a FilmEngineInvocation: build the
// invocation for a registered stock, meter the whole frame when the invocation asks for it
// (Auto Levels / Lab Scan levels, local tone, veiling-glare mean), and hand the renderer the packed
// configuration, the three 33³ RGBA tables, the feature mask, the seed, the supports and the
// film grain model's tile bank.
//
// Before the first build the host registers the stock JSON (FilmStockRegistry::shared()) and
// installs the reflectance prior (MeasuredReflectanceTable::install).
#ifndef FOTUFILM_CORE_FOTUFILM_FRAME_BUILDER_H
#define FOTUFILM_CORE_FOTUFILM_FRAME_BUILDER_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FilmEngine.h"
#include "Pipeline.h"
#include "SpectralModel.h"

namespace fotufilm {

/// One frame's engine input, as FotufilmFrame.kt takes it.
struct FotufilmFrame {
    /// FOTUFILM_FRAME_CONFIGURATION_COUNT floats.
    std::vector<float> configuration;
    /// 33³ RGBA tables, value `((z * 33 + y) * 33 + x) * 4 + channel`.
    SpectralLUT exposure;
    SpectralLUT filmOutput;
    /// Absent when the film is viewed directly (no paper stage).
    std::optional<SpectralLUT> paperOutput;
    int32_t featureMask = 0;
    uint32_t seed = 0;
    int frameWidth = 0;
    int frameHeight = 0;
    int spatialSupport = 0;
    int spatialSupportSansHalation = 0;
    int lightSupport = 0;
    int halationSupport = 0;
    std::array<int32_t, 3> halationPixelRadii{{0, 0, 0}};
    /// The film grain model's bank, `(257², 17, 3)` floats, entry fastest; null unless the
    /// configuration's grain mode is 1. Shared with the binding, never copied.
    std::shared_ptr<const std::vector<float>> filmTiles;
    /// Identifies `filmTiles` for a texture cache; empty without tiles.
    std::string filmTilesKey;
    /// Identifies the three tables (upstream `spectralCacheID`).
    uint64_t tablesKey = 0;
    /// Whether a whole-frame measurement was written into the configuration.
    bool metered = false;
    /// The readings a metered frame took, in upstream's `Options.scene*` form: a host that
    /// develops related frames on the same levels (preview frames, an unexposed edge) can hand
    /// them on instead of metering again.
    std::optional<float> sceneHighlightStops;
    std::optional<Float3> sceneToneStops;
    std::optional<Float3> sceneChannelMedians;
};

class FotufilmFrameBuilder {
public:
    struct Request {
        /// A FilmStockRegistry id.
        std::string stockID;
        FotufilmEngine::Options options;
        /// The whole frame every absolute coordinate refers to.
        int width = 0;
        int height = 0;
        /// Advances the grain seed per video frame; 0 for stills.
        uint64_t frameIndex = 0;
        /// Throws to cancel the (possibly seconds-long) film grain preparation.
        CheckCancellation checkCancellation;
    };

    /// Builds the invocation: development, tables, configuration and, under the film grain
    /// model, the tile bank. Throws std::runtime_error for an unknown stock id, and the
    /// invocation's own errors (FilmDevelopmentError, TransportError, cancellation).
    explicit FotufilmFrameBuilder(const Request& request);

    /// Whether the frame's levels or local tone read a whole-frame measurement
    /// (`sceneMeteringActive`): Auto Levels or Lab Scan with no host-supplied
    /// `sceneHighlightStops`, or active tone controls under `localTone`.
    bool wantsSceneMetering() const { return invocation_.sceneMeteringActive(); }
    /// Whether the frame lays veiling glare and so needs its frame mean.
    bool wantsFlareMean() const { return (invocation_.featureMask & FilmEngineFeature::flare) != 0; }

    /// Meters a whole frame of linear Rec.2020 RGBA floats (alpha ignored) and writes the
    /// readings into the configuration: tone base and levels when `wantsSceneMetering`, and the
    /// veiling-glare mean when `wantsFlareMean` (area-weighted on a 64-cell grid, as the realtime
    /// hosts measure it). The buffer may be the frame itself or a downsampled copy of the whole
    /// frame with the same aspect; see the implementation notes on what a copy changes. Call once,
    /// after construction and before `frame()`.
    void meter(const float* linearRec2020RGBA, int width, int height);

    /// Leaves the veiling-glare mean to the renderer (FOTUFILM_FRAME_FLARE_MEASURE), for a frame
    /// metered without a buffer. No effect when the frame lays no glare.
    void measureFlareOnDevice();

    FotufilmFrame frame() const;
    const FilmEngineInvocation& invocation() const { return invocation_; }

    /// Constructs, meters (when `linearRec2020RGBA` is given and the frame asks for it; otherwise
    /// glare is left to the renderer) and returns the frame.
    static FotufilmFrame build(const Request& request, const float* linearRec2020RGBA = nullptr, int meterWidth = 0,
                               int meterHeight = 0);

private:
    static FilmStock stockNamed(const std::string& id);

    FilmEngineInvocation invocation_;
    int width_;
    int height_;
    bool metered_ = false;
    std::optional<float> sceneHighlightStops_;
    std::optional<Float3> sceneToneStops_;
    std::optional<Float3> sceneChannelMedians_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FOTUFILM_FRAME_BUILDER_H
