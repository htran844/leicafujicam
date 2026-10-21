// Port of Sources/FotufilmCore/DigitalReferenceReceiver.swift.
#include "DigitalReferenceReceiver.h"

#include <cmath>
#include <mutex>

#include "BoundedCache.h"
#include "FilmStock.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {

const std::vector<DigitalReferenceStyle>& allDigitalReferenceStyles() {
    static const std::vector<DigitalReferenceStyle> all = {
        DigitalReferenceStyle::referenceExposure, DigitalReferenceStyle::gradedPrint, DigitalReferenceStyle::autoLevels};
    return all;
}

const char* rawValue(DigitalReferenceStyle style) {
    switch (style) {
        case DigitalReferenceStyle::referenceExposure: return "reference-exposure";
        case DigitalReferenceStyle::gradedPrint: return "graded-print";
        case DigitalReferenceStyle::autoLevels: return "auto-levels";
    }
    return "";
}

std::optional<DigitalReferenceStyle> digitalReferenceStyleFromRawValue(const std::string& raw) {
    for (DigitalReferenceStyle style : allDigitalReferenceStyles()) {
        if (raw == rawValue(style)) return style;
    }
    return std::nullopt;
}

const char* name(DigitalReferenceStyle style) {
    switch (style) {
        case DigitalReferenceStyle::referenceExposure: return "Reference Exposure";
        case DigitalReferenceStyle::gradedPrint: return "Graded Print";
        case DigitalReferenceStyle::autoLevels: return "Auto Levels";
    }
    return "";
}

const char* detail(DigitalReferenceStyle style) {
    switch (style) {
        case DigitalReferenceStyle::referenceExposure:
            return "Fixed calibrated exposure; mid-grey and contrast never depend on the frame.";
        case DigitalReferenceStyle::gradedPrint:
            return "Fixed exposure through a graded paper curve with a soft highlight shoulder; "
                   "a positive's clear base sets white.";
        case DigitalReferenceStyle::autoLevels:
            return "Re-times each frame like a lab scan: its brightest content sets white.";
    }
    return "";
}

ReceiverLevels receiverLevels(DigitalReferenceStyle style, const FilmStock& stock,
                              std::optional<float> sceneHighlightStops, float exposureEV) {
    return DigitalReferenceReceiver::levels(stock, style, sceneHighlightStops, exposureEV);
}

std::optional<float> autoLevelsColourRead(const FilmStock& stock, float stops) {
    if (!DigitalReferenceReceiver::metersColour(stock)) return std::nullopt;
    return DigitalReferenceReceiver::gradedBaseRead() / DigitalReferenceReceiver::baseRead(stock) *
           DigitalReferenceReceiver::read(stock, stops);
}

namespace AutoLevelsRule {
float whiteSpanLower() { return DigitalReferenceReceiver::retimeWhiteSpanLower; }
float whiteSpanUpper() { return DigitalReferenceReceiver::retimeWhiteSpanUpper; }
float liftLimit() { return DigitalReferenceReceiver::retimeLiftLimit; }
float printWhiteOverGrey() { return DigitalReferenceReceiver::printWhiteOverGrey; }
float printRange() { return DigitalReferenceReceiver::printRange; }
float shadowLiftShare() { return DigitalReferenceReceiver::shadowLiftShare; }
float colourShare() { return DigitalReferenceReceiver::autoColourShare; }
float colourReach() { return DigitalReferenceReceiver::autoColourReach; }
bool placesFilm(const FilmStock& stock) { return DigitalReferenceReceiver::keysTone(stock); }
bool metersColour(const FilmStock& stock) { return DigitalReferenceReceiver::metersColour(stock); }
}  // namespace AutoLevelsRule

// ---- ReceiverBands ----

const ReceiverBands& ReceiverBands::paper() {
    static const ReceiverBands bands = [] {
        float peaks[3];
        const auto& sensitivity = SpectralGrid::paperSensitivity();
        for (int b = 0; b < 3; ++b) {
            const std::vector<float>& band = sensitivity[b];
            size_t best = 0;
            for (size_t i = 1; i < band.size(); ++i) {
                if (band[best] < band[i]) best = i;
            }
            peaks[b] = SpectralGrid::wavelengths()[best];
        }
        return ReceiverBands(peaks[0], peaks[1], peaks[2]);
    }();
    return bands;
}

const std::vector<Float2>& ReceiverBands::ranges() {
    static const std::vector<Float2> values = {Float2(600, 740), Float2(500, 600), Float2(400, 500)};
    return values;
}

ReceiverBands ReceiverBands::clamped() const {
    ReceiverBands result = *this;
    for (int band = 0; band < 3; ++band) {
        Float2 range = ranges()[band];
        result[band] = std::isfinite((*this)[band]) ? swiftMin(swiftMax((*this)[band], range.x), range.y)
                                                    : paper()[band];
    }
    return result;
}

std::vector<std::vector<float>> ReceiverBands::sensitivity() const {
    if (*this == paper()) return SpectralGrid::paperSensitivity();
    std::vector<std::vector<float>> bands;
    const auto& wavelengths = SpectralGrid::wavelengths();
    for (int band = 0; band < 3; ++band) {
        const std::vector<float>& measured = SpectralGrid::paperSensitivity()[band];
        float shift = (*this)[band] - paper()[band];
        std::vector<float> row(wavelengths.size());
        for (size_t i = 0; i < wavelengths.size(); ++i) {
            // Read the measured band `shift` nanometres back; nothing past the grid.
            float position = (wavelengths[i] - shift - wavelengths[0]) / SpectralGrid::stepNM;
            if (!(position >= 0 && position <= static_cast<float>(SpectralGrid::count - 1))) {
                row[i] = 0;
                continue;
            }
            int low = std::min(static_cast<int>(position), SpectralGrid::count - 2);
            float fraction = position - static_cast<float>(low);
            row[i] = measured[low] * (1 - fraction) + measured[low + 1] * fraction;
        }
        bands.push_back(std::move(row));
    }
    return SpectralGrid::normalizeSensitivities(bands);
}

// ---- DigitalReferenceReceiver ----

namespace DigitalReferenceReceiver {

Float3 colourShift(Float3 cmy, const FilmStock& stock, PrintPaper paper) {
    if (!(paper == PrintPaper::screen && !stock.isReversal && !stock.isMonochrome && !stock.isReflectionPrint)) {
        return Float3();
    }
    Float3 result;
    for (int channel = 0; channel < 3; ++channel) {
        float value = cmy[channel];
        result[channel] = std::isfinite(value) ? swiftMin(swiftMax(value, -1.0f), 1.0f) * 0.30f : 0;
    }
    return result;
}

const std::vector<float>& illuminant() { return SpectralGrid::equalEnergy(); }

const CharacteristicCurve& referenceCurve() {
    static const CharacteristicCurve curve(0.07f, 2.60f, -0.52f, 0.16f, 0.42f, 0.14f);
    return curve;
}

const CharacteristicCurve& straightCurve() {
    static const CharacteristicCurve curve(0, 1, -0.8f, 0.01f, 0.9f, 0.01f);
    return curve;
}

const CharacteristicCurve& positiveCurve() {
    static const CharacteristicCurve curve(0, 1, -anchorDensity, 0.01f, 3.5f - anchorDensity, 0.01f);
    return curve;
}

const CharacteristicCurve& referenceCurve(const FilmStock& stock) {
    return stock.isMonochrome ? stock.paperCurve : referenceCurve();
}

CharacteristicCurve curve(DigitalReferenceStyle style, const FilmStock& stock) {
    if (stock.isReversal) return positiveCurve();
    return usesGradedCurve(style) ? straightCurve() : referenceCurve(stock);
}

float referenceBaseTarget(const FilmStock& stock) {
    const CharacteristicCurve& curve = referenceCurve(stock);
    float anchor = curve.logExposure(curve.dMin + anchorDensity);
    return curve.logExposure(curve.dMax() - 0.03f) - anchor;
}

float stretchShadows(float v, float scale) {
    if (!(v > 0)) return v;
    return v + (scale - 1) * v * (1 - std::exp(-v / 0.08f));
}

float baseRead(const FilmStock& stock) {
    if (stock.isMonochrome) {
        float dMin = (((0.0f + stock.curves[0].dMin) + stock.curves[1].dMin) + stock.curves[2].dMin) / 3;
        return swiftMax(SpectralRuntime::neutralDensity(stock, 0) - dMin, 0.05f);
    }
    const CharacteristicCurve& green = stock.curves[1];
    return swiftMax(green.density(0) - green.dMin, 0.05f);
}

float read(const FilmStock& stock, float stops) {
    if (stock.isMonochrome) {
        return SpectralRuntime::neutralDensity(stock, 0) - SpectralRuntime::neutralDensity(stock, stops * std::log10(2.0f));
    }
    const CharacteristicCurve& green = stock.curves[1];
    return green.density(0) - green.density(stops * std::log10(2.0f));
}

float gradedReadPerVal() { return 0.73f / 0.54f; }
float gradedBaseRead() { return (gradedBaseVal - gradedAnchorVal) * gradedReadPerVal(); }
float gradedWhiteRead() { return (gradedWhiteVal - gradedAnchorVal) * gradedReadPerVal(); }

float gradedSlope(float grade) {
    float held = swiftMin(swiftMax(grade, gradeRangeLower), gradeRangeUpper);
    return 2.894f * std::exp(0.225f * (held - referenceGrade));
}

float gradedTransmittance(float val, float grade) {
    auto softplus = [](float x) { return x > 0 ? x + std::log1p(std::exp(-x)) : std::log1p(std::exp(x)); };
    float k = gradedSlope(grade), vStar = 0.6955f;
    float pivot = gradedAnchorVal - vStar / k;
    const float dMinEff = 0.004f, dMaxEff = 2.295f, aHl = 3.0f, aSh = 6.0f;
    float v = k * (val - pivot);
    v += 0.05f * 0.6f * std::tanh((v - vStar) / 0.6f);
    float v1 = dMinEff + softplus(aHl * (v - dMinEff)) / aHl;
    float d = dMaxEff - softplus(aSh * (dMaxEff - v1)) / aSh;
    float black = std::pow(10.0f, -2.3f);
    return swiftMin(swiftMax((std::pow(10.0f, -d) - black) / (1 - black), 0.0f), 1.0f);
}

ReceiverLevels levels(const FilmStock& stock, DigitalReferenceStyle style, std::optional<float> sceneHighlightStops,
                      float exposureEV) {
    if (!usesGradedCurve(style) || stock.isReflectionPrint) return {1, 0};
    std::optional<float> measuredStops;
    if (sceneHighlightStops) measuredStops = *sceneHighlightStops - exposureEV;
    if (stock.isReversal) return {1, positiveShift(stock, style, measuredStops)};
    float base = baseRead(stock);
    switch (style) {
        case DigitalReferenceStyle::referenceExposure: return {1, 0};
        case DigitalReferenceStyle::gradedPrint: return {gradedBaseRead() / base, 0};
        case DigitalReferenceStyle::autoLevels: {
            float scale = gradedBaseRead() / base;
            if (!measuredStops || !std::isfinite(*measuredStops)) return {scale, 0};
            return {scale, retimeShift(stock, *measuredStops)};
        }
    }
    return {1, 0};
}

float retimeShift(const FilmStock& stock, float highlightStops) {
    float scale = gradedBaseRead() / baseRead(stock);
    return gradedWhiteRead() - scale * read(stock, swiftMin(swiftMax(highlightStops, -12.0f), 12.0f));
}

float retimeHighlight(const AutoAdjustment::SceneStops& scene, float exposureEV) {
    float white = swiftMin(swiftMax(scene.bright, scene.median + retimeWhiteSpanLower),
                           scene.median + retimeWhiteSpanUpper);
    return swiftMax(white, exposureEV + printWhiteOverGrey - retimeLiftLimit);
}

float filmBoost(const FilmStock& stock, float white, float exposureEV) {
    if (!keysTone(stock)) return 0;
    return exposureEV - toneKey(white);
}

bool keysTone(const FilmStock& stock) { return !stock.isReversal && !stock.isReflectionPrint; }

ToneCompression toneCompression(const FilmStock& stock, const AutoAdjustment::SceneStops& scene, float white) {
    if (!keysTone(stock)) return {0, 0};
    // The tone masks move a pixel `reach` stops from the key by 3 · amount · smoothstep(reach / 6).
    auto amount = [](float overflow, float room) {
        if (!(overflow > 0)) return 0.0f;
        float t = swiftMin((room + overflow) / 6, 1.0f);
        return swiftMin(overflow / (3 * t * t * (3 - 2 * t)), 1.0f);
    };
    return {amount(scene.bright - white, printWhiteOverGrey),
            shadowLiftShare * amount(white - printRange - scene.dark, printRange - printWhiteOverGrey)};
}

Float3 autoColourShift(const FilmStock& stock, std::optional<Float3> channelMedians, float exposureEV) {
    if (!metersColour(stock) || !channelMedians) return Float3();
    Float3 medians = *channelMedians;
    if (!(std::isfinite(medians.x) && std::isfinite(medians.y) && std::isfinite(medians.z))) return Float3();
    auto readAt = [&](float stops) { return autoLevelsColourRead(stock, stops).value_or(0.0f); };
    float green = medians.y - exposureEV;
    auto shift = [&](float median) {
        float cast = swiftMin(swiftMax(median - exposureEV - green, -autoColourReach), autoColourReach);
        return readAt(green) - readAt(green + cast);
    };
    return autoColourShare * Float3(shift(medians.x), 0, shift(medians.z));
}

bool metersColour(const FilmStock& stock) {
    return !stock.isReversal && !stock.isMonochrome && !stock.isReflectionPrint;
}

float positiveShift(const FilmStock& stock, DigitalReferenceStyle style, std::optional<float> sceneHighlightStops) {
    std::shared_ptr<const SpectralRuntime::PositiveScreenRead> positive = SpectralRuntime::positiveScreenRead(stock);
    float baseWhite = -positive->base;
    switch (style) {
        case DigitalReferenceStyle::referenceExposure: return 0;
        case DigitalReferenceStyle::gradedPrint: return baseWhite;
        case DigitalReferenceStyle::autoLevels: {
            if (!sceneHighlightStops || !std::isfinite(*sceneHighlightStops)) return baseWhite;
            float stops = swiftMin(swiftMax(*sceneHighlightStops, 0.5f), 12.0f);
            float highlight = -positive->luminance(stops);
            return swiftMin(swiftMax(highlight, baseWhite - positiveLiftLimit), baseWhite);
        }
    }
    return 0;
}

Float3 positiveRGB(Float3 density) {
    return Float3(std::pow(10.0f, -density.x), std::pow(10.0f, -density.y), std::pow(10.0f, -density.z));
}

DensityUnmix::DensityUnmix(const ReceiverBands& bands) {
    std::vector<std::vector<float>> dyes = SpectralGrid::dyes(FilmDyeFamily::kodakNegative);
    std::vector<std::vector<float>> sensitivity = bands.sensitivity();
    const std::vector<float>& light = illuminant();
    Float3 response[3];
    for (int band = 0; band < 3; ++band) {
        Float3 r = Float3::repeating(0);
        float total = 0;
        for (int i = 0; i < SpectralGrid::count; ++i) {
            float weight = light[i] * sensitivity[band][i];
            total += weight;
            for (int dye = 0; dye < 3; ++dye) r[dye] += weight * dyes[dye][i];
        }
        response[band] = r / total;
    }
    auto cross = [](Float3 a, Float3 b) {
        return Float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    };
    Float3 a = cross(response[1], response[2]);
    Float3 b = cross(response[2], response[0]);
    Float3 c = cross(response[0], response[1]);
    // Transpose the cofactors into inverse rows; normalising their sums holds an equal-channel
    // exposure exactly on the neutral axis.
    for (int i = 0; i < 3; ++i) {
        Float3 row(a[i], b[i], c[i]);
        rows_.push_back(row / (row.x + row.y + row.z));
    }
}

Float3 DensityUnmix::read(Float3 e) const {
    Float3 out;
    for (int i = 0; i < 3; ++i) {
        const Float3& row = rows_[i];
        out[i] = row.x * e.x + row.y * e.y + row.z * e.z;
    }
    return out;
}

const DensityUnmix& unmixPaper() {
    static const DensityUnmix value(ReceiverBands::paper());
    return value;
}

DensityUnmix unmix(const ReceiverBands& bands) {
    return bands == ReceiverBands::paper() ? unmixPaper() : DensityUnmix(bands);
}

float exposureShift(float stops, const FilmStock& stock, DigitalReferenceStyle style, float grade) {
    if (stops == 0) return 0;
    if (stock.isReversal) return -stops * std::log10(2.0f);
    CharacteristicCurve c = curve(style, stock);
    float xMid = c.logExposure(c.dMin + anchorDensity);
    // Mid-grey's green output at a read shift, exactly as the kernel and output table place it.
    auto output = [&](float shift) {
        float density = c.density(xMid + shift) - c.dMin;
        return rgb(Float3::repeating(density), style, stock, grade).y;
    };
    float target = output(0) * std::exp2(stops);
    // Output falls as the read rises; bisect the read for the target.
    float low = -3, high = 3;
    for (int i = 0; i < 40; ++i) {
        float mid = (low + high) / 2;
        if (output(mid) > target) low = mid; else high = mid;
    }
    return (low + high) / 2;
}

Float3 rgb(Float3 density, DigitalReferenceStyle style, const FilmStock& stock, float grade) {
    if (stock.isReversal) return positiveRGB(density);
    const CharacteristicCurve& reference = referenceCurve(stock);
    if (usesGradedCurve(style)) {
        Float3 val = (density - anchorDensity) / gradedReadPerVal() + gradedAnchorVal;
        float baseTarget = referenceBaseTarget(stock);
        auto mixed = [&](float relative, float value) {
            float highlight = gradedTransmittance(value, grade);
            float anchor = reference.logExposure(reference.dMin + anchorDensity);
            float shadowRead = stretchShadows(relative, baseTarget / gradedBaseRead());
            float shadowDensity = reference.density(anchor + shadowRead) - reference.dMin;
            float floor = std::pow(10.0f, -(reference.dMax() - reference.dMin));
            float shadow = swiftMax((std::pow(10.0f, -shadowDensity) - floor) / (1 - floor), 0.0f);
            // C1 blend through mid-grey: the graded highlight shoulder over film-base blacks.
            float t = swiftMin(swiftMax((relative + 0.04f) / 0.08f, 0.0f), 1.0f);
            float blend = t * t * (3 - 2 * t);
            return highlight * (1 - blend) + shadow * blend;
        };
        Float3 relative = density - anchorDensity;
        if (style == DigitalReferenceStyle::autoLevels) {
            std::shared_ptr<const ScreenShadowToe> toe = ScreenShadowToe::shared(stock);
            auto logged = [&](float rel, float value) {
                float highlight = gradedTransmittance(value, grade);
                float shadow = toe->transmittance(rel);
                float t = swiftMin(swiftMax((rel + 0.04f) / 0.08f, 0.0f), 1.0f);
                float blend = t * t * (3 - 2 * t);
                return highlight * (1 - blend) + shadow * blend;
            };
            return Float3(logged(relative.x, val.x), logged(relative.y, val.y), logged(relative.z, val.z));
        }
        return Float3(mixed(relative.x, val.x), mixed(relative.y, val.y), mixed(relative.z, val.z));
    }
    float floor = std::pow(10.0f, -(reference.dMax() - reference.dMin));
    Float3 t(std::pow(10.0f, -density.x), std::pow(10.0f, -density.y), std::pow(10.0f, -density.z));
    Float3 c = (t - floor) / (1 - floor);
    return Float3(swiftMax(c.x, 0.0f), swiftMax(c.y, 0.0f), swiftMax(c.z, 0.0f));
}

}  // namespace DigitalReferenceReceiver

// ---- ScreenShadowToe ----

namespace {
std::mutex& screenShadowToeLock() {
    static std::mutex lock;
    return lock;
}
}  // namespace

std::shared_ptr<const ScreenShadowToe> ScreenShadowToe::shared(const FilmStock& stock) {
    static BoundedCache<uint64_t, std::shared_ptr<const ScreenShadowToe>> cache(16);
    uint64_t key = SpectralRuntime::cacheIdentifier(stock, PrintPaper::screen);
    {
        std::lock_guard<std::mutex> guard(screenShadowToeLock());
        if (auto found = cache.value(key)) return *found;
    }
    std::shared_ptr<const ScreenShadowToe> toe(new ScreenShadowToe(stock));
    {
        std::lock_guard<std::mutex> guard(screenShadowToeLock());
        cache.insert(toe, key);
    }
    return toe;
}

ScreenShadowToe::ScreenShadowToe(const FilmStock& stock) {
    using namespace DigitalReferenceReceiver;
    float base = baseRead(stock);
    float scale = gradedBaseRead() / base;
    float shift = retimeShift(stock, printWhiteOverGrey);
    int count = static_cast<int>((6 - lowest) / stride) + 1;
    std::vector<float> reads(count);
    for (int i = 0; i < count; ++i) {
        reads[i] = scale * DigitalReferenceReceiver::read(stock, lowest + static_cast<float>(i) * stride) + shift;
    }
    reads_ = reads;
    anchorStops_ = stops(0, reads);
    depth_ = anchorStops_ - stops(scale * recordedShare * base + shift, reads);
    float y = gradedTransmittance(gradedAnchorVal);
    float grey = 116 * std::cbrt(y) - 16;
    // The width that gives the toe `greySlope` at mid-grey.
    float low = 1e-3f, high = 20;
    for (int i = 0; i < 60; ++i) {
        float mid = (low + high) / 2;
        if (grey / (mid * std::log(1 + depth_ / mid)) > greySlope) low = mid; else high = mid;
    }
    float width = (low + high) / 2;
    width_ = width;
    grey_ = grey;
    float span = std::log(1 + depth_ / width);
    float under = width * (std::exp((1 - finishLightness / grey) * span) - 1);
    float stop = anchorStops_ - under;
    auto readAt = [&](float s) {
        float position = (s - lowest) / stride;
        int index = std::min(std::max(static_cast<int>(position), 0), static_cast<int>(reads.size()) - 2);
        return reads[index] + (position - static_cast<float>(index)) * (reads[index + 1] - reads[index]);
    };
    finishRead_ = readAt(stop);
    blackRead_ = scale * blackShare * base + shift;
    float lightnessPerStop = grey / ((width + under) * span);
    float readPerStop = (readAt(stop - 0.01f) - readAt(stop + 0.01f)) / 0.02f;
    finishPower_ = swiftMax(lightnessPerStop / readPerStop * (blackRead_ - finishRead_) / finishLightness, 1.0f);
}

float ScreenShadowToe::stops(float relative, const std::vector<float>& reads) {
    if (!(relative < reads[0])) return lowest;
    if (!(relative > reads[reads.size() - 1])) return lowest + static_cast<float>(reads.size() - 1) * stride;
    int low = 0, high = static_cast<int>(reads.size()) - 1;
    while (high - low > 1) {
        int mid = (low + high) / 2;
        if (reads[mid] > relative) low = mid; else high = mid;
    }
    float span = reads[low] - reads[high];
    float fraction = span > 0 ? (reads[low] - relative) / span : 0;
    return lowest + (static_cast<float>(low) + fraction) * stride;
}

float ScreenShadowToe::transmittance(float relative) const {
    float lightness;
    if (relative >= finishRead_) {
        float toBlack = swiftMax(blackRead_ - relative, 0.0f) / swiftMax(blackRead_ - finishRead_, 1e-6f);
        lightness = finishLightness * std::pow(swiftMin(toBlack, 1.0f), finishPower_);
    } else {
        float under = swiftMax(anchorStops_ - stops(relative, reads_), 0.0f);
        lightness = grey_ * (1 - std::log(1 + under / width_) / std::log(1 + depth_ / width_));
    }
    return lightness > 8 ? std::pow((lightness + 16) / 116, 3.0f) : lightness / (24389.0f / 27.0f);
}

}  // namespace fotufilm
