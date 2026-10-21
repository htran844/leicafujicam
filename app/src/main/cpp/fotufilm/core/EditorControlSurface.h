// Port of Sources/FotufilmEditModel/EditorControlSurface.swift: surfaces, scopes, stored encodings,
// the warmth axis, persistence, control values, the engine bindings and menu choices. Plugin-host
// parameter descriptions (HostGroup, HostParameter, HostAuxiliary, BridgeEncoding), the web and
// command-line bindings are host-specific and not ported.
#ifndef FOTUFILM_CORE_EDITOR_CONTROL_SURFACE_H
#define FOTUFILM_CORE_EDITOR_CONTROL_SURFACE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Pipeline.h"

namespace fotufilm {

enum class EditorSurface : uint32_t { app, desktop, android, resolve, finalcut, web, cli };

const char* rawValue(EditorSurface surface);
/// A set of surfaces as a bit mask, bit `1 << surface`.
using EditorSurfaces = uint32_t;
constexpr EditorSurfaces surfaceBit(EditorSurface surface) { return 1u << static_cast<uint32_t>(surface); }

enum class EditorControlScope { edit, global, hostOnly };

enum class StoredEncoding { same, miredFromWarmth, duvFromPadTint, scaleFromStops };

const char* rawValue(StoredEncoding encoding);
double stored(StoredEncoding encoding, double displayed);
double displayed(StoredEncoding encoding, double stored);

namespace WarmthAxis {
double neutralMired();
constexpr double coolMired = 1e6 / 12000.0;
constexpr double warmMired = 1e6 / 2500.0;
double warmthFromMired(double mired);
double miredFromWarmth(double warmth);
double kelvinFromWarmth(double warmth);
double warmthFromKelvin(double kelvin);
double padTintFromDuv(double tint);
double duvFromPadTint(double value);
}  // namespace WarmthAxis

struct EditorControlPersistence {
    enum class Type { key, bespoke, none };
    Type type = Type::none;
    std::string key;
    StoredEncoding encoding = StoredEncoding::same;

    static EditorControlPersistence keyed(std::string key, StoredEncoding encoding) {
        return EditorControlPersistence{Type::key, std::move(key), encoding};
    }
    static EditorControlPersistence bespoke() { return EditorControlPersistence{Type::bespoke, {}, StoredEncoding::same}; }
    static EditorControlPersistence none() { return EditorControlPersistence{Type::none, {}, StoredEncoding::same}; }
    std::optional<StoredEncoding> storedEncoding() const {
        if (type == Type::key) return encoding;
        return std::nullopt;
    }
};

struct EditorControlValue {
    enum class Type { number, optionalNumber, flag, curve, choice };
    Type type = Type::number;
    double numberValue = 0;
    /// `optionalNumber(nil)` when false.
    bool hasNumber = true;
    bool flagValue = false;
    std::vector<double> curveValue;
    int choiceValue = 0;

    static EditorControlValue makeNumber(double value) {
        EditorControlValue v;
        v.type = Type::number;
        v.numberValue = value;
        return v;
    }
    static EditorControlValue makeOptionalNumber(std::optional<double> value) {
        EditorControlValue v;
        v.type = Type::optionalNumber;
        v.hasNumber = value.has_value();
        v.numberValue = value.value_or(0);
        return v;
    }
    static EditorControlValue makeFlag(bool on) {
        EditorControlValue v;
        v.type = Type::flag;
        v.flagValue = on;
        return v;
    }
    static EditorControlValue makeCurve(std::vector<double> values) {
        EditorControlValue v;
        v.type = Type::curve;
        v.curveValue = std::move(values);
        return v;
    }
    static EditorControlValue makeChoice(int index) {
        EditorControlValue v;
        v.type = Type::choice;
        v.choiceValue = index;
        return v;
    }

    std::optional<double> number() const;
    std::optional<bool> flag() const;
    std::optional<int> choice() const;
};

enum class GradeBand { shadows, midtones, highlights };
enum class GradeAxis { warmth, tint, level };
const char* rawValue(GradeBand band);
/// `GradeBand.title`.
std::string title(GradeBand band);

/// Swift `EngineBinding`; the cases with an associated value carry it in `index` (channel, band
/// or record) or in `band`/`axis`.
struct EngineBinding {
    enum class Kind {
        exposureEV,
        warmth,
        tint,
        highlights,
        shadows,
        localTone,
        saturation,
        vibrance,
        grainScale,
        grainMottleShare,
        grainModel,
        halationStops,
        halationSourceColour,
        halationReturnRatio,
        halationReturnGain,
        halationHazeMicrometers,
        antiHalationScale,
        baseThicknessScale,
        pressurePlateReflectance,
        couplerScale,
        couplerGapReach,
        couplerSelf,
        chromaticFringeAmount,
        chromaticFringeRadiusMicrometers,
        developmentEV,
        bleachBypass,
        expiredYears,
        shutterSeconds,
        printViewingKelvin,
        printCorrection,
        grade,
        gradeSpaceEncoded,
        flareScale,
        estimatedHalationProfile,
        halationModelIndex,
        negativeViewingIndex,
        frameCoverage,
        focalLengthMM,
        sceneIlluminantKelvin,
        stageIndex,
        textureStagesMask,
        seed,
        digitalReferenceIndex,
        screenGrade,
        screenExposureStops,
        screenCMY,
        printerLight,
        receiverBand,
        labScanDodging,
        labScanLook,
        enlargerIndex,
        whiteBalanceKelvin,
        whiteBalanceDuv,
        halationScale,
        cameraPreflash,
        printerPreflash,
        filmGrainSize,
        filmColourGrain,
        filmGrainLayer,
        filmScanSoftness,
        hdrRange,
        hdrRollOff,
    };

    Kind kind = Kind::exposureEV;
    int index = 0;
    GradeBand band = GradeBand::shadows;
    GradeAxis axis = GradeAxis::warmth;

    EngineBinding() = default;
    EngineBinding(Kind kind_) : kind(kind_) {}  // NOLINT: Swift's bare case
    static EngineBinding indexed(Kind kind, int index) {
        EngineBinding b(kind);
        b.index = index;
        return b;
    }
    static EngineBinding makeGrade(GradeBand band, GradeAxis axis) {
        EngineBinding b(Kind::grade);
        b.band = band;
        b.axis = axis;
        return b;
    }

    std::vector<std::string> optionNames() const;
    void apply(const EditorControlValue& value, FotufilmEngine::Options& options) const;
};

struct EditorMenuChoice {
    std::optional<double> value;
    std::string label;
    std::string detail;
    std::string id;

    EditorMenuChoice(std::optional<double> value_, std::string label_, std::string detail_ = "",
                     std::optional<std::string> id_ = std::nullopt);

    static std::string slug(const std::string& label);
};

enum class EditorDynamicMenu {
    stocks,
    gauges,
    papers,
    lensProfiles,
    lensFilters,
    meterings,
    diffusionFamilies,
    diffusionGrades,
    negativeViewings,
    stages,
    textureStages,
    shutterTimes,
    viewingLights,
    colourSpaces,
    pushConditions,
};
const char* rawValue(EditorDynamicMenu menu);

struct EditorMenuChoices {
    bool isDynamic = false;
    std::vector<EditorMenuChoice> fixed;
    EditorDynamicMenu dynamic = EditorDynamicMenu::stocks;

    static EditorMenuChoices makeFixed(std::vector<EditorMenuChoice> choices) {
        EditorMenuChoices c;
        c.fixed = std::move(choices);
        return c;
    }
    static EditorMenuChoices makeDynamic(EditorDynamicMenu menu) {
        EditorMenuChoices c;
        c.isDynamic = true;
        c.dynamic = menu;
        return c;
    }
    const std::vector<EditorMenuChoice>* fixedChoices() const { return isDynamic ? nullptr : &fixed; }
    std::optional<EditorDynamicMenu> dynamicMenu() const {
        if (isDynamic) return dynamic;
        return std::nullopt;
    }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_EDITOR_CONTROL_SURFACE_H
