// Not an upstream file; see FotufilmFrameBuilder.h.
#include "FotufilmFrameBuilder.h"

#include <cstdio>
#include <stdexcept>

#include "FilmStockPack.h"

namespace fotufilm {

FilmStock FotufilmFrameBuilder::stockNamed(const std::string& id) {
    std::optional<FilmStock> stock = FilmStockRegistry::shared().named(id);
    if (!stock) throw std::runtime_error("unknown film stock: " + id);
    return *stock;
}

FotufilmFrameBuilder::FotufilmFrameBuilder(const Request& request)
    : invocation_(stockNamed(request.stockID), request.options, request.width, request.height, request.frameIndex,
                  false, true, request.checkCancellation),
      width_(request.width),
      height_(request.height) {}

void FotufilmFrameBuilder::meter(const float* pixels, int width, int height) {
    preconditionCheck(pixels != nullptr && width > 0 && height > 0, "metering needs a non-empty frame");
    if (wantsSceneMetering()) {
        // Upstream meters the frame at its own size. The measurement is a per-cell mean of log
        // luminance on a grid of at most 64 cells a side, sized from the buffer, so a downsampled
        // copy meters the same regions with each cell's log-mean taken over fewer, box-averaged
        // pixels; the grid size the configuration records is the copy's, which the renderer reads
        // in frame-normalised coordinates.
        ToneBaseMeasurement measurement = invocation_.toneBaseMeasurement(width, height);
        measurement.addLinearRGBA(pixels, 0, height);
        sceneHighlightStops_ = invocation_.sceneHighlightStops(measurement);
        sceneToneStops_ = invocation_.sceneToneStops(measurement);
        sceneChannelMedians_ = invocation_.sceneChannelMedians(measurement);
        invocation_.setToneBase(measurement);
        metered_ = true;
    }
    if (wantsFlareMean()) {
        // Area-weighted on a grid of at most 64 cells a side (the realtime hosts' measurement).
        // Each cell is read at its centre on the whole frame, so the tone grid keys it as the
        // renderer will; from a copy, the cell averages are the copy's.
        FilmFlareFrame flare = FilmFlareFrame::linearRec2020RGBA(pixels, width, height);
        if (width != width_ || height != height_) flare = flare.reprojected(width_, height_);
        invocation_.setFlareMean(invocation_.measuredAreaWeightedFlareMean(flare));
        invocation_.featureMask &= ~FilmEngineFeature::flareMeasure;
    }
}

void FotufilmFrameBuilder::measureFlareOnDevice() {
    if (wantsFlareMean()) {
        invocation_.setFlareMean(std::nullopt);
        invocation_.featureMask |= FilmEngineFeature::flareMeasure;
    }
}

FotufilmFrame FotufilmFrameBuilder::frame() const {
    const SpectralPipelineTables& tables = invocation_.spectral;
    FotufilmFrame frame{invocation_.configuration, tables.exposure, tables.filmOutput, tables.paperOutput};
    frame.featureMask = invocation_.featureMask;
    frame.seed = invocation_.seed;
    frame.frameWidth = width_;
    frame.frameHeight = height_;
    frame.spatialSupport = invocation_.spatialSupport;
    frame.spatialSupportSansHalation = invocation_.spatialSupportSansHalation;
    frame.lightSupport = invocation_.lightSupport;
    frame.halationSupport = invocation_.halationSupport;
    for (size_t i = 0; i < 3 && i < invocation_.halationPixelRadii.size(); ++i) {
        frame.halationPixelRadii[i] = invocation_.halationPixelRadii[i];
    }
    if (invocation_.filmTileBinding && invocation_.configuration[FOTUFILM_CONFIG_GRAIN_MODE] == 1) {
        frame.filmTiles = invocation_.filmTileBinding->packed();
        // The population key (stock identity and grain scale) names the bank across bindings.
        uint64_t hash = 0xcbf29ce484222325ull;
        for (unsigned char c : invocation_.filmTileBinding->grain->key) hash = (hash ^ c) * 0x100000001b3ull;
        char text[40];
        std::snprintf(text, sizeof text, "film-tiles-%016llx", static_cast<unsigned long long>(hash));
        frame.filmTilesKey = text;
    }
    frame.tablesKey = invocation_.spectralCacheID;
    frame.metered = metered_;
    frame.sceneHighlightStops = sceneHighlightStops_;
    frame.sceneToneStops = sceneToneStops_;
    frame.sceneChannelMedians = sceneChannelMedians_;
    return frame;
}

FotufilmFrame FotufilmFrameBuilder::build(const Request& request, const float* pixels, int meterWidth,
                                          int meterHeight) {
    FotufilmFrameBuilder builder(request);
    if (pixels && meterWidth > 0 && meterHeight > 0) {
        builder.meter(pixels, meterWidth, meterHeight);
    } else {
        builder.measureFlareOnDevice();
    }
    return builder.frame();
}

}  // namespace fotufilm
