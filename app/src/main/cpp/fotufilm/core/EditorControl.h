// Port of Sources/FotufilmEditModel/EditorControl.swift: control fields, groups, sections, units,
// scales, kinds, curves, availability and the control description. A control's plugin-host,
// web and command-line descriptions and its documentation text are not ported.
#ifndef FOTUFILM_CORE_EDITOR_CONTROL_H
#define FOTUFILM_CORE_EDITOR_CONTROL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "EditorControlSurface.h"

namespace fotufilm {

struct FilmStock;

#define FOTUFILM_EDITOR_CONTROL_FIELDS(X)                                                                  \
    X(exposure) X(warmth) X(tint) X(highlights) X(shadows) X(localTone) X(autoAdjustment) X(saturation)    \
    X(vibrance) X(sceneLight) X(sceneLightKelvin) X(cameraPreflash) X(hdrRange) X(hdrRollOff) X(stock)     \
    X(gauge) X(frameCoverage) X(grain) X(grainMottle) X(mottleOverride) X(mottleShare) X(grainModel)       \
    X(grainAnimation) X(seed) X(filmGrainSize) X(filmColourGrain) X(filmRedLayer) X(filmGreenLayer)        \
    X(filmBlueLayer) X(filmScanSoftness) X(halation) X(halationReturn) X(halationColour)                   \
    X(halationSpectrum) X(halationModel) X(estimatedHalation) X(halationHaze) X(antiHalation)              \
    X(baseThickness) X(pressurePlate) X(couplers) X(couplerReach) X(couplerSelf) X(couplerRedGreen)        \
    X(couplerGreenBlue) X(chromaticFringeAmount) X(chromaticFringeRadius) X(push) X(bleach) X(expired)     \
    X(shutter) X(lensFilterStack) X(lensFilter1) X(lensFilter2) X(lensFilter3) X(metering) X(diffusion)    \
    X(diffusionGrade) X(focalLength) X(flare) X(filterCoating) X(lensCorrection) X(lensProfile)            \
    X(lensAmount) X(lensDistortion) X(lensVignetting) X(lensRedCyan) X(lensBlueYellow) X(screenCyan)       \
    X(screenMagenta) X(screenYellow) X(screenRedBand) X(screenGreenBand) X(screenBlueBand)                 \
    X(labScanDodging) X(labScanLook) X(paper) X(digitalReference) X(screenGrade) X(screenExposure)         \
    X(printFrame) X(printFrameMargin) X(printLight) X(enlarger) X(printCorrection) X(negativeViewing)      \
    X(gradeSpace) X(printerEnabled) X(printerLamp) X(printerExposure) X(printerMagenta) X(printerYellow)   \
    X(printerPreflash) X(printerRed) X(printerGreen) X(printerBlue) X(gradeShadowsWarmth)                  \
    X(gradeShadowsTint) X(gradeShadowsLevel) X(gradeMidtonesWarmth) X(gradeMidtonesTint)                   \
    X(gradeMidtonesLevel) X(gradeHighlightsWarmth) X(gradeHighlightsTint) X(gradeHighlightsLevel) X(crop)  \
    X(straighten) X(perspectiveVertical) X(perspectiveHorizontal) X(rotation) X(flip) X(selective)         \
    X(colorSpace) X(colorManagement) X(inputColorSpace) X(outputColorSpace) X(stage) X(textureStages)      \
    X(renderMode)

enum class EditorControlField {
#define FOTUFILM_FIELD_CASE(name) name,
    FOTUFILM_EDITOR_CONTROL_FIELDS(FOTUFILM_FIELD_CASE)
#undef FOTUFILM_FIELD_CASE
};

const char* rawValue(EditorControlField field);
std::optional<EditorControlField> editorControlFieldFromRawValue(const std::string& raw);
/// A control only the film grain model reads.
bool needsFilmGrainModel(EditorControlField field);

enum class EditorControlGroup { film, lens, light, print, frame, pipeline };
const char* rawValue(EditorControlGroup group);
const char* title(EditorControlGroup group);

enum class EditorControlSection {
    filmStock,
    filmGrain,
    filmHalation,
    filmEmulsion,
    filmLab,
    lensGlass,
    lensCorrection,
    lightExposure,
    lightBalance,
    lightColor,
    lightGrade,
    hdrHighlights,
    printPaper,
    printLamp,
    frameGeometry,
    frameLocal,
    pipeline,
};
const char* rawValue(EditorControlSection section);
const char* title(EditorControlSection section);
EditorControlGroup group(EditorControlSection section);
/// Whether Copy Settings and Save Preset tick this section to begin with.
bool transfersByDefault(EditorControlSection section);

enum class EditorControlUnit {
    multiplier,
    stops,
    stopsFromOff,
    signedValue,
    percent,
    years,
    seconds,
    degrees,
    kelvin,
    opticalDensity,
    micrometers,
    millimetres,
    nanometres,
    /// A release printer's light setting: 0.025 log exposure a point.
    printerPoints,
    none,
};
const char* rawValue(EditorControlUnit unit);
/// `EditorControlUnit.format`, the upstream English rendering of a displayed value.
std::string format(EditorControlUnit unit, double value);

struct EditorControlScale {
    double rangeLower = 0;
    double rangeUpper = 1;
    double neutral = 0;
    EditorControlUnit unit = EditorControlUnit::none;
    std::vector<double> stops;
    double admittedLower = 0;
    double admittedUpper = 1;

    EditorControlScale() = default;
    EditorControlScale(double lower, double upper, double neutral_, EditorControlUnit unit_,
                       std::vector<double> stops_ = {}, std::optional<std::pair<double, double>> admitted = std::nullopt)
        : rangeLower(lower),
          rangeUpper(upper),
          neutral(neutral_),
          unit(unit_),
          stops(std::move(stops_)),
          admittedLower(admitted ? admitted->first : lower),
          admittedUpper(admitted ? admitted->second : upper) {}

    bool admits(double value) const { return value >= admittedLower && value <= admittedUpper; }
    bool isMoved(double value) const;
};

struct EditorControlChoice {
    double value;
    std::string label;
};

struct EditorControlCurve {
    std::vector<double> handles;
    double domainLower = 0, domainUpper = 0;
    double rangeLower = 0, rangeUpper = 0;
    double neutral = 0;
    EditorControlUnit unit = EditorControlUnit::none;

    std::vector<double> restingValues() const { return std::vector<double>(handles.size(), neutral); }
    bool isMoved(const std::vector<double>& values) const;
};

struct EditorControlKind {
    enum class Type { slider, chips, toggle, menu, takeover, curve };
    Type type = Type::takeover;
    EditorControlScale scaleValue;
    std::vector<EditorControlChoice> chips;
    bool restingOn = false;
    EditorMenuChoices menuValue;
    EditorControlCurve curveValue;

    static EditorControlKind slider(EditorControlScale scale) {
        EditorControlKind k;
        k.type = Type::slider;
        k.scaleValue = std::move(scale);
        return k;
    }
    static EditorControlKind makeChips(EditorControlScale scale, std::vector<EditorControlChoice> choices) {
        EditorControlKind k;
        k.type = Type::chips;
        k.scaleValue = std::move(scale);
        k.chips = std::move(choices);
        return k;
    }
    static EditorControlKind toggle(bool restingOn) {
        EditorControlKind k;
        k.type = Type::toggle;
        k.restingOn = restingOn;
        return k;
    }
    static EditorControlKind menu(EditorMenuChoices choices) {
        EditorControlKind k;
        k.type = Type::menu;
        k.menuValue = std::move(choices);
        return k;
    }
    static EditorControlKind takeover() { return EditorControlKind(); }
    static EditorControlKind curve(EditorControlCurve curve) {
        EditorControlKind k;
        k.type = Type::curve;
        k.curveValue = std::move(curve);
        return k;
    }

    const EditorControlScale* scale() const {
        return type == Type::slider || type == Type::chips ? &scaleValue : nullptr;
    }
    const EditorControlCurve* curveOrNull() const { return type == Type::curve ? &curveValue : nullptr; }
    const EditorMenuChoices* menuOrNull() const { return type == Type::menu ? &menuValue : nullptr; }
    const char* typeName() const;
};

enum class EditorControlAvailability {
    always,
    film,
    colourNegative,
    /// Any colour film, negative or reversal.
    colourFilm,
    /// Film developed as a transparency rather than as an integral print.
    transparentFilm,
    printStage,
    statedReciprocity,
    couplerGeometry,
    interlayerInhibition,
    developmentConditions,
};
const char* rawValue(EditorControlAvailability availability);
bool admits(EditorControlAvailability availability, const FilmStock* stock);
std::optional<int32_t> capabilityBit(EditorControlAvailability availability);

struct EditorControl {
    EditorControlField field;
    std::string title;
    std::string detail;
    EditorControlSection section;
    EditorControlKind kind;
    EditorControlAvailability availability = EditorControlAvailability::always;
    std::optional<EditorControlField> foldsUnder;
    EditorControlScope scope = EditorControlScope::edit;
    EditorControlPersistence persistence;
    std::optional<EngineBinding> binding;
    std::vector<std::string> drives;
    EditorSurfaces surfaces = 0;

    /// `EditorControl.init`'s defaults: surfaces `[.app, .desktop, .android]`, persistence from the
    /// field, kind and scope.
    static EditorSurfaces defaultSurfaces() {
        return surfaceBit(EditorSurface::app) | surfaceBit(EditorSurface::desktop) | surfaceBit(EditorSurface::android);
    }
    static EditorControlPersistence defaultPersistence(EditorControlField field, const EditorControlKind& kind,
                                                       EditorControlScope scope);

    EditorControlGroup group() const { return fotufilm::group(section); }
    bool offered(EditorSurface surface) const { return (surfaces & surfaceBit(surface)) != 0; }
    std::optional<EditorControlValue> restingValue() const;
    std::optional<double> storedNeutral() const;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_EDITOR_CONTROL_H
