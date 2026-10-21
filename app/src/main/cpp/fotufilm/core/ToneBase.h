// Port of Sources/FotufilmCore/ToneBase.swift: ToneBaseMeasurement and MeteredLevels. The
// FilmEngineInvocation extension (setToneBase, sceneHighlightStops, ...) is declared in
// FilmEngine.h and defined in ToneBase.cpp. The byte-frame accumulators (sRGB / Display P3
// RGBA8) and the kernel cell-row-sum road are ported alongside the float ones.
#ifndef FOTUFILM_CORE_TONE_BASE_H
#define FOTUFILM_CORE_TONE_BASE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "FotufilmConfigLayout.h"
#include "LabScanTiming.h"
#include "Simd.h"

namespace fotufilm {

/// Whole-frame measurement used by local highlight and shadow masks and by the metered levels.
class ToneBaseMeasurement {
public:
    /// Cells along the grid's long edge; mirrors FOTUFILM_TONE_GRID_EDGE.
    static constexpr int gridEdge = FOTUFILM_TONE_GRID_EDGE;
    /// Guided-filter window radius in cells.
    static constexpr int windowRadius = 12;
    /// The edge threshold, in stops².
    static constexpr double epsilon = 0.25;
    /// Cells darker than this, in stops from mid-grey, carry no colour.
    static constexpr float litFloorStops = -12;

    ToneBaseMeasurement(int frameWidth, int frameHeight, Float3 balance, float exposureGain, bool metersColour = false);

    int gridWidth() const { return gridWidth_; }
    int gridHeight() const { return gridHeight_; }
    int frameWidth() const { return frameWidth_; }
    int frameHeight() const { return frameHeight_; }
    bool metersColour() const { return !channelLogSum_.empty(); }

    static Float3 channelMeteringBalance(Float3 balance, int channel);

    /// Frame rows [rowsLower, rowsUpper) as interleaved linear RGBA, `pixels` pointing at the first.
    void addLinearRGBA(const float* pixels, int rowsLower, int rowsUpper);
    /// Frame rows as planar linear RGB, each plane pointing at the first of them.
    void addPlanar(const float* red, const float* green, const float* blue, int rowsLower, int rowsUpper);
    /// Frame rows as interleaved sRGB-encoded RGBA bytes.
    void addSRGBRGBA(const uint8_t* bytes, int rowsLower, int rowsUpper);
    /// Frame rows as transfer-encoded Display P3 RGBA bytes.
    void addEncodedDisplayP3RGBA(const uint8_t* bytes, int rowsLower, int rowsUpper);
    /// Frame rows already reduced to one sum per grid cell column.
    void addCellRowSums(const float* sums, int rowsLower, int rowsUpper);
    void addChannelCellRowSums(int channel, const float* sums, int rowsLower, int rowsUpper);

    /// The accumulated regional log-luminances, one value per covered cell, in stops.
    std::vector<float> regionStops() const;
    /// The frame's colour as Auto Levels reads it; nullopt when not metered or nothing is lit.
    std::optional<Float3> channelMedians() const;

    struct Coefficients {
        std::vector<float> a;
        std::vector<float> b;
    };
    /// The self-guided filter over the accumulated cells.
    Coefficients solvedCoefficients() const;
    static std::vector<double> boxMean(const std::vector<double>& values, int width, int height, int radius);

private:
    static std::optional<float> percentile(const std::vector<float>& values, float q);
    void addCellRowSums(const float* sums, int rowsLower, int rowsUpper, std::optional<int> channel);
    void add(int rowsLower, int rowsUpper, const std::function<Float3(size_t)>& sample);

    int gridWidth_;
    int gridHeight_;
    int frameWidth_;
    int frameHeight_;
    float weightR_;
    float weightG_;
    float weightB_;
    std::vector<double> logSum_;
    std::vector<int64_t> counts_;
    std::vector<double> channelLogSum_;
    Float3 channelWeights_;
};

/// Where metered levels place a frame, in the kernel's contrast and paper mid-point slots.
struct MeteredLevels {
    Float3 scale = Float3::one();
    Float3 shift = Float3();
    /// Lab Scan's keys and lightening on the film's exposure.
    LabScanTiming::Setup film = LabScanTiming::Setup::identity();
    float filmBoost = 0;
    std::optional<float> toneKey;
    float shadowLift = 0;
    float highlightHold = 0;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_TONE_BASE_H
