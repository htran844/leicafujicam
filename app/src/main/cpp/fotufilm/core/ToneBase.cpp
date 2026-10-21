// Port of Sources/FotufilmCore/ToneBase.swift.
#include "ToneBase.h"

#include <algorithm>
#include <cmath>

#include "AutoAdjustment.h"
#include "ColorScience.h"
#include "DigitalReferenceReceiver.h"
#include "FilmEngine.h"
#include "ParallelWork.h"

namespace fotufilm {

// MARK: - ToneBaseMeasurement

ToneBaseMeasurement::ToneBaseMeasurement(int frameWidth, int frameHeight, Float3 balance, float exposureGain,
                                         bool metersColour) {
    frameWidth_ = std::max(frameWidth, 1);
    frameHeight_ = std::max(frameHeight, 1);
    int longSide = std::max(frameWidth_, frameHeight_);
    auto cells = [&](int side) { return std::min(side, std::max(1, (side * gridEdge + longSide / 2) / longSide)); };
    gridWidth_ = cells(frameWidth_);
    gridHeight_ = cells(frameHeight_);
    Float3 luma = ColorScience::luminanceWeights;
    float gain = exposureGain / 0.18f;
    weightR_ = luma.x * balance.x * gain;
    weightG_ = luma.y * balance.y * gain;
    weightB_ = luma.z * balance.z * gain;
    size_t plane = static_cast<size_t>(gridWidth_) * gridHeight_;
    logSum_.assign(plane, 0.0);
    counts_.assign(plane, 0);
    channelWeights_ = balance * gain;
    if (metersColour) channelLogSum_.assign(3 * plane, 0.0);
}

Float3 ToneBaseMeasurement::channelMeteringBalance(Float3 balance, int channel) {
    Float3 weights = ColorScience::luminanceWeights;
    Float3 isolated;
    isolated[channel] = balance[channel] / weights[channel];
    return isolated;
}

void ToneBaseMeasurement::addLinearRGBA(const float* pixels, int rowsLower, int rowsUpper) {
    add(rowsLower, rowsUpper, [pixels](size_t index) {
        return Float3(pixels[index * 4], pixels[index * 4 + 1], pixels[index * 4 + 2]);
    });
}

void ToneBaseMeasurement::addPlanar(const float* red, const float* green, const float* blue, int rowsLower,
                                    int rowsUpper) {
    add(rowsLower, rowsUpper, [=](size_t index) { return Float3(red[index], green[index], blue[index]); });
}

namespace {

const std::vector<float>& srgbDecodeTable() {
    static const std::vector<float> table = [] {
        std::vector<float> values;
        for (int i = 0; i < 256; ++i) values.push_back(ColorScience::srgbToLinear(static_cast<float>(i) / 255));
        return values;
    }();
    return table;
}

Float3 decodeBytes(const uint8_t* bytes, size_t index) {
    const std::vector<float>& decode = srgbDecodeTable();
    size_t offset = index * 4;
    uint8_t alpha = bytes[offset + 3];
    float denominator = alpha > 0 && alpha < 255 ? static_cast<float>(alpha) : 255.0f;
    bool whole = alpha == 0 || alpha == 255;
    auto channel = [&](size_t c) {
        return whole ? decode[bytes[offset + c]]
                     : ColorScience::srgbToLinear(swiftMin(static_cast<float>(bytes[offset + c]) / denominator, 1.0f));
    };
    return Float3(channel(0), channel(1), channel(2));
}

}  // namespace

void ToneBaseMeasurement::addSRGBRGBA(const uint8_t* bytes, int rowsLower, int rowsUpper) {
    add(rowsLower, rowsUpper,
        [bytes](size_t index) { return ColorScience::linearSRGBToRec2020(decodeBytes(bytes, index)); });
}

void ToneBaseMeasurement::addEncodedDisplayP3RGBA(const uint8_t* bytes, int rowsLower, int rowsUpper) {
    add(rowsLower, rowsUpper,
        [bytes](size_t index) { return ColorScience::linearDisplayP3ToRec2020(decodeBytes(bytes, index)); });
}

void ToneBaseMeasurement::addCellRowSums(const float* sums, int rowsLower, int rowsUpper) {
    addCellRowSums(sums, rowsLower, rowsUpper, std::nullopt);
}

void ToneBaseMeasurement::addChannelCellRowSums(int channel, const float* sums, int rowsLower, int rowsUpper) {
    if (!metersColour()) return;
    addCellRowSums(sums, rowsLower, rowsUpper, channel);
}

void ToneBaseMeasurement::addCellRowSums(const float* sums, int rowsLower, int rowsUpper, std::optional<int> channel) {
    if (rowsLower >= rowsUpper) return;
    int gw = gridWidth_, gh = gridHeight_;
    int fw = frameWidth_, fh = frameHeight_;
    for (int y = rowsLower; y < rowsUpper; ++y) {
        int cy = y * gh / fh;
        size_t row = static_cast<size_t>(y - rowsLower) * gw;
        for (int cx = 0; cx < gw; ++cx) {
            if (channel) {
                channelLogSum_[static_cast<size_t>(*channel) * gw * gh + static_cast<size_t>(cy) * gw + cx] +=
                    static_cast<double>(sums[row + cx]);
                continue;
            }
            int xLow = (cx * fw + gw - 1) / gw;
            int xHigh = ((cx + 1) * fw + gw - 1) / gw;
            logSum_[static_cast<size_t>(cy) * gw + cx] += static_cast<double>(sums[row + cx]);
            counts_[static_cast<size_t>(cy) * gw + cx] += xHigh - xLow;
        }
    }
}

void ToneBaseMeasurement::add(int rowsLower, int rowsUpper, const std::function<Float3(size_t)>& sample) {
    if (rowsLower >= rowsUpper) return;
    const int gw = gridWidth_, gh = gridHeight_;
    const int fw = frameWidth_, fh = frameHeight_;
    const float wr = weightR_, wg = weightG_, wb = weightB_;
    const bool colour = metersColour();
    const Float3 channelWeights = channelWeights_;
    const size_t plane = static_cast<size_t>(gw) * gh;
    int firstCell = static_cast<int>(static_cast<int64_t>(rowsLower) * gh / fh);
    int lastCell = static_cast<int>(static_cast<int64_t>(rowsUpper - 1) * gh / fh);
    ParallelWork::forEach(lastCell - firstCell + 1, [&](int task) {
        int cy = firstCell + task;
        int yLow = std::max(rowsLower, static_cast<int>((static_cast<int64_t>(cy) * fh + gh - 1) / gh));
        int yHigh = std::min(rowsUpper, static_cast<int>((static_cast<int64_t>(cy + 1) * fh + gh - 1) / gh));
        for (int y = yLow; y < yHigh; ++y) {
            size_t row = static_cast<size_t>(y - rowsLower) * fw;
            for (int cx = 0; cx < gw; ++cx) {
                int xLow = (cx * fw + gw - 1) / gw;
                int xHigh = ((cx + 1) * fw + gw - 1) / gw;
                double sum = 0.0;
                double channelSum[3] = {0.0, 0.0, 0.0};
                for (int x = xLow; x < xHigh; ++x) {
                    Float3 rgb = sample(row + x);
                    float metered = wr * swiftMax(rgb.x, 0.0f) + wg * swiftMax(rgb.y, 0.0f) + wb * swiftMax(rgb.z, 0.0f);
                    sum += static_cast<double>(std::log2(swiftMax(metered, 1e-6f)));
                    if (colour) {
                        Float3 each =
                            channelWeights * Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f));
                        channelSum[0] += static_cast<double>(std::log2(swiftMax(each.x, 1e-6f)));
                        channelSum[1] += static_cast<double>(std::log2(swiftMax(each.y, 1e-6f)));
                        channelSum[2] += static_cast<double>(std::log2(swiftMax(each.z, 1e-6f)));
                    }
                }
                logSum_[static_cast<size_t>(cy) * gw + cx] += sum;
                counts_[static_cast<size_t>(cy) * gw + cx] += xHigh - xLow;
                if (colour) {
                    for (int c = 0; c < 3; ++c) {
                        channelLogSum_[c * plane + static_cast<size_t>(cy) * gw + cx] += channelSum[c];
                    }
                }
            }
        }
    });
}

std::vector<float> ToneBaseMeasurement::regionStops() const {
    std::vector<float> stops;
    stops.reserve(logSum_.size());
    for (size_t i = 0; i < logSum_.size(); ++i) {
        if (counts_[i] > 0) stops.push_back(static_cast<float>(logSum_[i] / static_cast<double>(counts_[i])));
    }
    return stops;
}

std::optional<float> ToneBaseMeasurement::percentile(const std::vector<float>& values, float q) {
    if (!AutoAdjustment::SceneStops::fromRegionStops(values)) return std::nullopt;
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    float position = q * static_cast<float>(sorted.size() - 1);
    int low = static_cast<int>(position), high = std::min(low + 1, static_cast<int>(sorted.size()) - 1);
    return sorted[low] + (position - static_cast<float>(low)) * (sorted[high] - sorted[low]);
}

std::optional<Float3> ToneBaseMeasurement::channelMedians() const {
    if (!metersColour()) return std::nullopt;
    size_t plane = logSum_.size();
    std::vector<float> luma;
    std::vector<Float3> cells;
    for (size_t i = 0; i < plane; ++i) {
        if (!(counts_[i] > 0)) continue;
        double count = static_cast<double>(counts_[i]);
        luma.push_back(static_cast<float>(logSum_[i] / count));
        cells.push_back(Float3(static_cast<float>(channelLogSum_[i] / count),
                               static_cast<float>(channelLogSum_[plane + i] / count),
                               static_cast<float>(channelLogSum_[2 * plane + i] / count)));
    }
    std::optional<float> quartile = percentile(luma, 0.25f);
    if (!quartile) return std::nullopt;
    float floor = swiftMax(*quartile, litFloorStops);
    std::vector<float> greens, reds, blues;
    for (size_t i = 0; i < luma.size(); ++i) {
        if (!(luma[i] >= floor)) continue;
        greens.push_back(cells[i].y);
        reds.push_back(cells[i].x - cells[i].y);
        blues.push_back(cells[i].z - cells[i].y);
    }
    std::optional<float> green = percentile(greens, 0.5f);
    if (!green) return std::nullopt;
    std::optional<float> red = percentile(reds, 0.5f);
    if (!red) return std::nullopt;
    std::optional<float> blue = percentile(blues, 0.5f);
    if (!blue) return std::nullopt;
    return Float3(*green + *red, *green, *green + *blue);
}

ToneBaseMeasurement::Coefficients ToneBaseMeasurement::solvedCoefficients() const {
    int gw = gridWidth_, gh = gridHeight_;
    size_t cells = static_cast<size_t>(gw) * gh;
    std::vector<double> g(cells, 0.0);
    for (size_t i = 0; i < cells; ++i) {
        if (counts_[i] > 0) g[i] = logSum_[i] / static_cast<double>(counts_[i]);
    }
    int radius = std::min(windowRadius, std::max(gw, gh) - 1);
    std::vector<double> meanG = boxMean(g, gw, gh, radius);
    std::vector<double> gg(cells);
    for (size_t i = 0; i < cells; ++i) gg[i] = g[i] * g[i];
    std::vector<double> meanGG = boxMean(gg, gw, gh, radius);
    std::vector<double> a(cells, 0.0), b(cells, 0.0);
    for (size_t i = 0; i < cells; ++i) {
        double variance = swiftMax(0.0, meanGG[i] - meanG[i] * meanG[i]);
        a[i] = variance / (variance + epsilon);
        b[i] = (1 - a[i]) * meanG[i];
    }
    std::vector<double> smoothA = boxMean(a, gw, gh, radius);
    std::vector<double> smoothB = boxMean(b, gw, gh, radius);
    return Coefficients{std::vector<float>(smoothA.begin(), smoothA.end()),
                        std::vector<float>(smoothB.begin(), smoothB.end())};
}

std::vector<double> ToneBaseMeasurement::boxMean(const std::vector<double>& values, int width, int height,
                                                 int radius) {
    std::vector<double> sat(static_cast<size_t>(width + 1) * (height + 1), 0.0);
    for (int y = 0; y < height; ++y) {
        double rowSum = 0.0;
        for (int x = 0; x < width; ++x) {
            rowSum += values[static_cast<size_t>(y) * width + x];
            sat[static_cast<size_t>(y + 1) * (width + 1) + x + 1] = sat[static_cast<size_t>(y) * (width + 1) + x + 1] + rowSum;
        }
    }
    std::vector<double> result(static_cast<size_t>(width) * height, 0.0);
    for (int y = 0; y < height; ++y) {
        int y0 = std::max(0, y - radius), y1 = std::min(height - 1, y + radius);
        for (int x = 0; x < width; ++x) {
            int x0 = std::max(0, x - radius), x1 = std::min(width - 1, x + radius);
            double sum = sat[static_cast<size_t>(y1 + 1) * (width + 1) + x1 + 1] -
                         sat[static_cast<size_t>(y0) * (width + 1) + x1 + 1] -
                         sat[static_cast<size_t>(y1 + 1) * (width + 1) + x0] + sat[static_cast<size_t>(y0) * (width + 1) + x0];
            result[static_cast<size_t>(y) * width + x] = sum / static_cast<double>((y1 - y0 + 1) * (x1 - x0 + 1));
        }
    }
    return result;
}

// MARK: - FilmEngineInvocation (ToneBase.swift)

bool FilmEngineInvocation::toneControlsActive() const {
    return configuration[sceneAdjustOffset] + meterLevels_.highlightHold != 0 ||
           configuration[sceneAdjustOffset + 1] - meterLevels_.shadowLift != 0;
}

bool FilmEngineInvocation::toneKeyedLocally() const {
    bool labScanDodges =
        meterMedium_ == PrintPaper::labScan && (meterLevels_.highlightHold != 0 || meterLevels_.shadowLift != 0);
    return localToneEnabled && (toneControlsActive() || labScanDodges);
}

bool FilmEngineInvocation::screenLevelsReachLight() const {
    return meterMedium_ == PrintPaper::screen && meterStock_ && DigitalReferenceReceiver::keysTone(*meterStock_);
}

bool FilmEngineInvocation::labScanMeters() const { return meterMedium_ == PrintPaper::labScan && meterStock_.has_value(); }

void FilmEngineInvocation::copyMeteredLevels(const FilmEngineInvocation& measured) {
    if (!meterStock_) return;
    applyMeteredLevels(measured.meterLevels_);
}

void FilmEngineInvocation::applyMeteredLevels(MeteredLevels levels) {
    Float3 ratio = levels.scale / meterLevels_.scale;
    for (int c = 0; c < 3; ++c) configuration[FOTUFILM_CONFIG_MASKING + c] *= ratio[c];
    const int offsets[3] = {paperMidpointRedOffset, FOTUFILM_CONFIG_PAPER_MIDPOINT, paperMidpointBlueOffset};
    for (int c = 0; c < 3; ++c) configuration[offsets[c]] += levels.shift[c] - meterLevels_.shift[c];
    configuration[exposureGainOffset] *= std::exp2(levels.filmBoost - meterLevels_.filmBoost);
    if ((levels.film.shift != meterLevels_.film.shift || levels.film.keys != meterLevels_.film.keys) && meterStock_) {
        packFilmCurves(*meterStock_, levels.film);
    }
    if (configuration[toneGridSizeOffset] == 1 && configuration[toneGridSizeOffset + 1] == 1) {
        configuration[toneGridBOffset] = -(levels.toneKey.value_or(0.0f));
    }
    const int shadows = sceneAdjustOffset + 1;
    float userShadows = configuration[shadows] - meterLevels_.shadowLift;
    levels.shadowLift = swiftMin(levels.shadowLift, swiftMax(1 - userShadows, 0.0f));
    configuration[shadows] = userShadows + levels.shadowLift;
    const int highlights = sceneAdjustOffset;
    float userHighlights = configuration[highlights] + meterLevels_.highlightHold;
    levels.highlightHold = swiftMin(levels.highlightHold, swiftMax(userHighlights + 1, 0.0f));
    configuration[highlights] = userHighlights - levels.highlightHold;
    meterLevels_ = levels;
}

ToneBaseMeasurement FilmEngineInvocation::toneBaseMeasurement() const {
    return toneBaseMeasurement(static_cast<int>(configuration[frameSizeOffset]),
                               static_cast<int>(configuration[frameSizeOffset + 1]));
}

ToneBaseMeasurement FilmEngineInvocation::toneBaseMeasurement(int frameWidth, int frameHeight) const {
    Float3 balance(configuration[whiteBalanceOffset], configuration[whiteBalanceOffset + 1],
                   configuration[whiteBalanceOffset + 2]);
    std::optional<FilmStock> screenStock = screenMeterStock();
    return ToneBaseMeasurement(frameWidth, frameHeight, balance, configuration[exposureGainOffset],
                               screenStock ? DigitalReferenceReceiver::metersColour(*screenStock) : false);
}

std::optional<float> FilmEngineInvocation::sceneHighlightStops(const ToneBaseMeasurement& measurement) const {
    if (meterStock_ && meterMedium_ == PrintPaper::labScan) {
        std::optional<AutoAdjustment::SceneStops> scene =
            AutoAdjustment::SceneStops::fromRegionStops(measurement.regionStops());
        if (!scene) return std::nullopt;
        return LabScanTiming::highlight(*scene);
    }
    float exposureEV = std::log2(configuration[exposureGainOffset]) - meterLevels_.filmBoost;
    std::optional<AutoAdjustment::SceneStops> scene = screenScene(measurement);
    if (!scene) return std::nullopt;
    return DigitalReferenceReceiver::retimeHighlight(*scene, exposureEV);
}

std::optional<Float3> FilmEngineInvocation::sceneToneStops(const ToneBaseMeasurement& measurement) const {
    std::optional<AutoAdjustment::SceneStops> scene =
        meterMedium_ == PrintPaper::labScan && meterStock_
            ? AutoAdjustment::SceneStops::fromRegionStops(measurement.regionStops())
            : screenScene(measurement);
    if (!scene) return std::nullopt;
    return Float3(scene->median, scene->dark, scene->bright);
}

std::optional<FilmStock> FilmEngineInvocation::screenMeterStock() const {
    return meterMedium_ == PrintPaper::screen ? meterStock_ : std::nullopt;
}

std::optional<AutoAdjustment::SceneStops> FilmEngineInvocation::screenScene(
    const ToneBaseMeasurement& measurement) const {
    if (!screenMeterStock()) return std::nullopt;
    return AutoAdjustment::SceneStops::fromRegionStops(measurement.regionStops());
}

std::optional<Float3> FilmEngineInvocation::sceneChannelMedians(const ToneBaseMeasurement& measurement) const {
    if (!screenMeterStock()) return std::nullopt;
    return measurement.channelMedians();
}

void FilmEngineInvocation::setToneBase(const ToneBaseMeasurement& measurement) {
    if (meterStock_ && meterMedium_ == PrintPaper::labScan) {
        std::optional<AutoAdjustment::SceneStops> scene =
            AutoAdjustment::SceneStops::fromRegionStops(measurement.regionStops());
        if (scene) {
            LabScanTiming::Dodge dodge = LabScanTiming::dodge(*scene, meterDodging_);
            LabScanTiming::Setup setup =
                LabScanTiming::setup(*meterStock_, LabScanTiming::highlight(*scene), scene->median, meterExposureEV_,
                                     meterScanExposure_, meterKeys_);
            MeteredLevels levels;
            levels.scale = setup.scale;
            levels.shift = setup.print;
            levels.film = setup;
            levels.toneKey = dodge.key;
            levels.shadowLift = dodge.lift;
            levels.highlightHold = dodge.hold;
            applyMeteredLevels(levels);
        }
    }
    bool keyedLocally = toneKeyedLocally();
    std::optional<FilmStock> stock = screenMeterStock();
    std::optional<AutoAdjustment::SceneStops> scene = screenScene(measurement);
    if (stock && scene) {
        float exposureEV = std::log2(configuration[exposureGainOffset]) - meterLevels_.filmBoost;
        float white = DigitalReferenceReceiver::retimeHighlight(*scene, exposureEV);
        float boost = DigitalReferenceReceiver::filmBoost(*stock, white, exposureEV);
        ReceiverLevels screen =
            DigitalReferenceReceiver::levels(*stock, DigitalReferenceStyle::autoLevels, white + boost, exposureEV);
        std::optional<Float3> medians = sceneChannelMedians(measurement);
        if (medians) medians = *medians + boost;
        Float3 colour = DigitalReferenceReceiver::autoColourShift(*stock, medians, exposureEV);
        DigitalReferenceReceiver::ToneCompression compression =
            DigitalReferenceReceiver::toneCompression(*stock, *scene, white);
        MeteredLevels levels;
        levels.scale = Float3::repeating(screen.scale);
        levels.shift = Float3::repeating(screen.shift) + colour;
        levels.filmBoost = boost;
        if (DigitalReferenceReceiver::keysTone(*stock)) levels.toneKey = DigitalReferenceReceiver::toneKey(white) + boost;
        levels.shadowLift = compression.lift;
        levels.highlightHold = compression.hold;
        applyMeteredLevels(levels);
    }
    // Levels meter the same regions even when local tone is disabled; keep the whole-frame key.
    if (!keyedLocally) return;
    ToneBaseMeasurement::Coefficients coefficients = measurement.solvedCoefficients();
    float key = meterMedium_ == PrintPaper::labScan ? meterLevels_.toneKey.value_or(0.0f) : 0.0f;
    configuration[toneGridSizeOffset] = static_cast<float>(measurement.gridWidth());
    configuration[toneGridSizeOffset + 1] = static_cast<float>(measurement.gridHeight());
    for (size_t i = 0; i < coefficients.a.size(); ++i) {
        configuration[toneGridAOffset + i] = coefficients.a[i];
        configuration[toneGridBOffset + i] = coefficients.b[i] - key;
    }
}

void FilmEngineInvocation::measureToneBasePlanar(const float* red, const float* green, const float* blue, int width,
                                                 int height) {
    (void)width;
    ToneBaseMeasurement measurement = toneBaseMeasurement();
    measurement.addPlanar(red, green, blue, 0, height);
    setToneBase(measurement);
}

void FilmEngineInvocation::measureToneBase(const float* linearRGBA, int width, int height) {
    (void)width;
    ToneBaseMeasurement measurement = toneBaseMeasurement();
    measurement.addLinearRGBA(linearRGBA, 0, height);
    setToneBase(measurement);
}

void FilmEngineInvocation::measureToneBaseSRGB(const uint8_t* bytes, int width, int height) {
    (void)width;
    ToneBaseMeasurement measurement = toneBaseMeasurement();
    measurement.addSRGBRGBA(bytes, 0, height);
    setToneBase(measurement);
}

void FilmEngineInvocation::measureToneBaseEncodedDisplayP3(const uint8_t* bytes, int width, int height) {
    (void)width;
    ToneBaseMeasurement measurement = toneBaseMeasurement();
    measurement.addEncodedDisplayP3RGBA(bytes, 0, height);
    setToneBase(measurement);
}

}  // namespace fotufilm
