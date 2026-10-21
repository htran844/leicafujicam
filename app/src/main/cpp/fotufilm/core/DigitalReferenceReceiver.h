// Port of Sources/FotufilmCore/DigitalReferenceReceiver.swift: DigitalReferenceStyle,
// ReceiverBands, DigitalReferenceReceiver and ScreenShadowToe.
#ifndef FOTUFILM_CORE_DIGITAL_REFERENCE_RECEIVER_H
#define FOTUFILM_CORE_DIGITAL_REFERENCE_RECEIVER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AutoAdjustment.h"
#include "CharacteristicCurve.h"
#include "Simd.h"

namespace fotufilm {

struct FilmStock;

/// How a developed film's tone is placed on Digital Reference.
enum class DigitalReferenceStyle : int {
    /// A print made at a standard, calibrated exposure through one fixed receiver curve.
    referenceExposure,
    /// The same fixed anchoring with a graded paper's characteristic curve.
    gradedPrint,
    /// Per-frame timing on the graded curve, the way a minilab scanner re-times each frame.
    autoLevels,
};

constexpr DigitalReferenceStyle kDefaultDigitalReferenceStyle = DigitalReferenceStyle::autoLevels;
const std::vector<DigitalReferenceStyle>& allDigitalReferenceStyles();
const char* rawValue(DigitalReferenceStyle style);
std::optional<DigitalReferenceStyle> digitalReferenceStyleFromRawValue(const std::string& raw);
inline std::string id(DigitalReferenceStyle style) { return rawValue(style); }
const char* name(DigitalReferenceStyle style);
const char* detail(DigitalReferenceStyle style);
/// Whether the tone curve lives in the output table rather than the kernel's paper curve.
inline bool usesGradedCurve(DigitalReferenceStyle style) { return style != DigitalReferenceStyle::referenceExposure; }

/// The affine `read' = scale * read + shift` the kernel applies for the graded styles.
struct ReceiverLevels {
    float scale;
    float shift;
};

/// `DigitalReferenceStyle.receiverLevels(for:sceneHighlightStops:exposureEV:)`.
ReceiverLevels receiverLevels(DigitalReferenceStyle style, const FilmStock& stock,
                              std::optional<float> sceneHighlightStops = std::nullopt, float exposureEV = 0);
/// The read Auto Levels balances a colour negative's records on; nullopt where it leaves colour alone.
std::optional<float> autoLevelsColourRead(const FilmStock& stock, float stops);

/// `DigitalReferenceStyle.AutoLevelsRule`: Auto Levels' metering rule for hosts.
namespace AutoLevelsRule {
float whiteSpanLower();
float whiteSpanUpper();
float liftLimit();
float printWhiteOverGrey();
float printRange();
float shadowLiftShare();
float colourShare();
float colourReach();
/// ToneBaseMeasurement.litFloorStops.
constexpr float litFloorStops = -12;
bool placesFilm(const FilmStock& stock);
bool metersColour(const FilmStock& stock);
}  // namespace AutoLevelsRule

/// The three bands Digital Reference reads a colour negative's dyes through, each named by the
/// wavelength it peaks at.
struct ReceiverBands {
    float red;
    float green;
    float blue;

    ReceiverBands(float red_, float green_, float blue_) : red(red_), green(green_), blue(blue_) {}

    /// Where the measured paper bands peak on the spectral grid.
    static const ReceiverBands& paper();
    /// The peaks each band can be placed at, in nanometres: 600...740, 500...600, 400...500.
    static const std::vector<Float2>& ranges();

    float operator[](int band) const { return band == 0 ? red : (band == 1 ? green : blue); }
    float& operator[](int band) { return band == 0 ? red : (band == 1 ? green : blue); }

    /// Each peak held inside its range; a non-finite one keeps the paper's.
    ReceiverBands clamped() const;
    /// The bands on the spectral grid, each normalised under D65 as the paper's are.
    std::vector<std::vector<float>> sensitivity() const;

    bool operator==(const ReceiverBands& o) const { return red == o.red && green == o.green && blue == o.blue; }
    bool operator!=(const ReceiverBands& o) const { return !(*this == o); }
};

/// Fixed spectral receiver for color negatives on Digital Reference.
namespace DigitalReferenceReceiver {

/// Independent receiver offsets after automatic levels, on Digital Reference.
Float3 colourShift(Float3 cmy, const FilmStock& stock, PrintPaper paper);

/// Equal energy.
const std::vector<float>& illuminant();

/// The receiver's mid-grey density above base.
constexpr float anchorDensity = 0.744f;

/// Common digital density scale for `.referenceExposure`.
const CharacteristicCurve& referenceCurve();
/// A straight line for the graded styles.
const CharacteristicCurve& straightCurve();
/// A straight line for a levelled positive.
const CharacteristicCurve& positiveCurve();
/// The curve `.referenceExposure` develops a record along.
const CharacteristicCurve& referenceCurve(const FilmStock& stock);
CharacteristicCurve curve(DigitalReferenceStyle style, const FilmStock& stock);

float referenceBaseTarget(const FilmStock& stock);
float stretchShadows(float v, float scale);
float baseRead(const FilmStock& stock);
float read(const FilmStock& stock, float stops);

constexpr float gradedAnchorVal = 0.46f;
constexpr float gradedBaseVal = 1.0f;
constexpr float gradedWhiteVal = 0.02f;
float gradedReadPerVal();
float gradedBaseRead();
float gradedWhiteRead();

constexpr float referenceGrade = 2;
constexpr float gradeRangeLower = 0;
constexpr float gradeRangeUpper = 5;

float gradedSlope(float grade);
float gradedTransmittance(float val, float grade = referenceGrade);

ReceiverLevels levels(const FilmStock& stock, DigitalReferenceStyle style, std::optional<float> sceneHighlightStops,
                      float exposureEV = 0);
float retimeShift(const FilmStock& stock, float highlightStops);

constexpr float printWhiteOverGrey = 3;
constexpr float printRange = 6.3f;
constexpr float retimeWhiteSpanLower = 2.5f;
constexpr float retimeWhiteSpanUpper = 3.5f;
constexpr float retimeLiftLimit = 2.5f;

float retimeHighlight(const AutoAdjustment::SceneStops& scene, float exposureEV = 0);
inline float toneKey(float white) { return white - printWhiteOverGrey; }
float filmBoost(const FilmStock& stock, float white, float exposureEV = 0);
bool keysTone(const FilmStock& stock);

constexpr float shadowLiftShare = 0.75f;

struct ToneCompression {
    float hold;
    float lift;
};
ToneCompression toneCompression(const FilmStock& stock, const AutoAdjustment::SceneStops& scene, float white);

constexpr float autoColourShare = 0.5f;
constexpr float autoColourReach = 2;

Float3 autoColourShift(const FilmStock& stock, std::optional<Float3> channelMedians, float exposureEV = 0);
bool metersColour(const FilmStock& stock);

constexpr float positiveLiftLimit = 0.9f;
float positiveShift(const FilmStock& stock, DigitalReferenceStyle style, std::optional<float> sceneHighlightStops);
Float3 positiveRGB(Float3 density);

/// The receiver's small-signal density response at a neutral synthetic negative.
class DensityUnmix {
public:
    explicit DensityUnmix(const ReceiverBands& bands);
    Float3 read(Float3 relativeLogEnergy) const;

private:
    std::vector<Float3> rows_;
};

const DensityUnmix& unmixPaper();
DensityUnmix unmix(const ReceiverBands& bands);

float exposureShift(float stops, const FilmStock& stock, DigitalReferenceStyle style, float grade);
Float3 rgb(Float3 density, DigitalReferenceStyle style, const FilmStock& stock, float grade = referenceGrade);

}  // namespace DigitalReferenceReceiver

/// Auto Levels' shadow half on the screen: a log curve in the stops of scene light under mid-grey.
class ScreenShadowToe {
public:
    static constexpr float greySlope = 30;
    static constexpr float recordedShare = 0.9f;
    static constexpr float finishLightness = 6;
    static constexpr float blackShare = 0.92f;

    static std::shared_ptr<const ScreenShadowToe> shared(const FilmStock& stock);

    /// Display-linear output for a levelled read on the shadow side of mid-grey.
    float transmittance(float relative) const;

private:
    explicit ScreenShadowToe(const FilmStock& stock);
    static float stops(float relative, const std::vector<float>& reads);

    static constexpr float stride = 0.02f;
    static constexpr float lowest = -16;
    std::vector<float> reads_;
    float anchorStops_;
    float depth_;
    float width_;
    float grey_;
    float finishRead_;
    float blackRead_;
    float finishPower_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_DIGITAL_REFERENCE_RECEIVER_H
