// Port of Sources/FotufilmEditModel/EditorControlSurface.swift.
#include "EditorControlSurface.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include "FilmSpectralProfile.h"
#include "HalationAmount.h"
#include "HalationSpectrum.h"
#include "WhiteBalance.h"

namespace fotufilm {

const char* rawValue(EditorSurface surface) {
    switch (surface) {
        case EditorSurface::app: return "app";
        case EditorSurface::desktop: return "desktop";
        case EditorSurface::android: return "android";
        case EditorSurface::resolve: return "resolve";
        case EditorSurface::finalcut: return "finalcut";
        case EditorSurface::web: return "web";
        case EditorSurface::cli: return "cli";
    }
    return "app";
}

const char* rawValue(StoredEncoding encoding) {
    switch (encoding) {
        case StoredEncoding::same: return "same";
        case StoredEncoding::miredFromWarmth: return "miredFromWarmth";
        case StoredEncoding::duvFromPadTint: return "duvFromPadTint";
        case StoredEncoding::scaleFromStops: return "scaleFromStops";
    }
    return "same";
}

double stored(StoredEncoding encoding, double value) {
    switch (encoding) {
        case StoredEncoding::same: return value;
        case StoredEncoding::miredFromWarmth: return WarmthAxis::miredFromWarmth(value);
        case StoredEncoding::duvFromPadTint: return WarmthAxis::duvFromPadTint(value);
        case StoredEncoding::scaleFromStops: return HalationAmount::scale(value);
    }
    return value;
}

double displayed(StoredEncoding encoding, double value) {
    switch (encoding) {
        case StoredEncoding::same: return value;
        case StoredEncoding::miredFromWarmth: return WarmthAxis::warmthFromMired(value);
        case StoredEncoding::duvFromPadTint: return WarmthAxis::padTintFromDuv(value);
        case StoredEncoding::scaleFromStops: return HalationAmount::stops(value);
    }
    return value;
}

namespace WarmthAxis {

double neutralMired() {
    return static_cast<double>(WhiteBalance::kelvinToMired(WhiteBalance::neutralKelvin));
}

double warmthFromMired(double mired) {
    const double neutral = neutralMired();
    const double warmth = mired >= neutral ? (mired - neutral) / (warmMired - neutral)
                                           : (mired - neutral) / (neutral - coolMired);
    return std::min(std::max(warmth, -1.0), 1.0);
}

double miredFromWarmth(double warmth) {
    const double neutral = neutralMired();
    return warmth > 0 ? neutral + warmth * (warmMired - neutral) : neutral + warmth * (neutral - coolMired);
}

double kelvinFromWarmth(double warmth) {
    return static_cast<double>(WhiteBalance::miredToKelvin(static_cast<float>(miredFromWarmth(warmth))));
}

double warmthFromKelvin(double kelvin) {
    return warmthFromMired(static_cast<double>(WhiteBalance::kelvinToMired(static_cast<float>(kelvin))));
}

double padTintFromDuv(double tint) { return std::min(std::max(tint / 100, -1.0), 1.0); }

double duvFromPadTint(double value) { return value * 100; }

}  // namespace WarmthAxis

std::optional<double> EditorControlValue::number() const {
    switch (type) {
        case Type::number: return numberValue;
        case Type::optionalNumber: return hasNumber ? std::optional<double>(numberValue) : std::nullopt;
        case Type::flag: return flagValue ? 1.0 : 0.0;
        case Type::choice: return static_cast<double>(choiceValue);
        case Type::curve: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<bool> EditorControlValue::flag() const {
    switch (type) {
        case Type::flag: return flagValue;
        case Type::number: return numberValue != 0;
        case Type::optionalNumber: return hasNumber ? std::optional<bool>(numberValue != 0) : std::nullopt;
        case Type::choice: return choiceValue != 0;
        case Type::curve: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> EditorControlValue::choice() const {
    switch (type) {
        case Type::choice: return choiceValue;
        case Type::number: return static_cast<int>(numberValue);
        case Type::optionalNumber:
            return hasNumber ? std::optional<int>(static_cast<int>(numberValue)) : std::nullopt;
        case Type::flag: return flagValue ? 1 : 0;
        case Type::curve: return std::nullopt;
    }
    return std::nullopt;
}

const char* rawValue(GradeBand band) {
    switch (band) {
        case GradeBand::shadows: return "shadows";
        case GradeBand::midtones: return "midtones";
        case GradeBand::highlights: return "highlights";
    }
    return "shadows";
}

std::string title(GradeBand band) {
    std::string raw = rawValue(band);
    raw[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(raw[0])));
    return raw;
}

namespace {

ColorGrade::Band& gradeBand(ColorGrade& grade, GradeBand band) {
    switch (band) {
        case GradeBand::shadows: return grade.shadows;
        case GradeBand::midtones: return grade.midtones;
        case GradeBand::highlights: return grade.highlights;
    }
    return grade.shadows;
}

float& gradeAxis(ColorGrade::Band& band, GradeAxis axis) {
    switch (axis) {
        case GradeAxis::warmth: return band.balanceX;
        case GradeAxis::tint: return band.balanceY;
        case GradeAxis::level: return band.level;
    }
    return band.balanceX;
}

std::optional<float> positiveFloat(std::optional<double> number) {
    if (number && *number > 0) return static_cast<float>(*number);
    return std::nullopt;
}

}  // namespace

std::vector<std::string> EngineBinding::optionNames() const {
    switch (kind) {
        case Kind::exposureEV: return {"exposureEV"};
        case Kind::warmth:
        case Kind::tint: return {"whiteBalance"};
        case Kind::highlights: return {"highlights"};
        case Kind::shadows: return {"shadows"};
        case Kind::localTone: return {"localTone"};
        case Kind::saturation: return {"saturation"};
        case Kind::vibrance: return {"vibrance"};
        case Kind::grainScale: return {"grainScale"};
        case Kind::grainMottleShare: return {"grainMottleShare"};
        case Kind::grainModel: return {"grainModel"};
        case Kind::halationStops: return {"halationScale"};
        case Kind::halationReturnRatio: return {"halationReturnRatio"};
        case Kind::halationSourceColour: return {"halationSourceColour"};
        case Kind::halationReturnGain: return {"halationReturnGain"};
        case Kind::halationHazeMicrometers: return {"halationHazeMM"};
        case Kind::antiHalationScale: return {"antiHalationScale"};
        case Kind::baseThicknessScale: return {"baseThicknessScale"};
        case Kind::pressurePlateReflectance: return {"pressurePlateReflectance"};
        case Kind::couplerScale: return {"couplerScale"};
        case Kind::couplerGapReach: return {"couplerGapReachScales"};
        case Kind::couplerSelf: return {"couplerSelfScale"};
        case Kind::chromaticFringeAmount: return {"chromaticFringeAmount"};
        case Kind::chromaticFringeRadiusMicrometers: return {"chromaticFringeRadiusMM"};
        case Kind::developmentEV: return {"developmentEV"};
        case Kind::bleachBypass: return {"bleachBypass"};
        case Kind::expiredYears: return {"expiredYears"};
        case Kind::shutterSeconds: return {"shutterSeconds"};
        case Kind::printViewingKelvin: return {"printViewingKelvin"};
        case Kind::printCorrection: return {"printCorrection"};
        case Kind::grade: return {"grade"};
        case Kind::gradeSpaceEncoded: return {"gradeSpace"};
        case Kind::flareScale: return {"flareScale"};
        case Kind::estimatedHalationProfile: return {"useEstimatedHalationProfile"};
        case Kind::halationModelIndex: return {"halationModel"};
        case Kind::negativeViewingIndex: return {"negativeViewing"};
        case Kind::frameCoverage: return {"frameCoverage"};
        case Kind::focalLengthMM: return {"focalLengthMM"};
        case Kind::sceneIlluminantKelvin: return {"sceneIlluminantKelvin"};
        case Kind::stageIndex: return {"stage"};
        case Kind::textureStagesMask: return {"textureStages"};
        case Kind::seed: return {"seed"};
        case Kind::digitalReferenceIndex: return {"digitalReference"};
        case Kind::screenGrade: return {"screenGrade"};
        case Kind::screenExposureStops: return {"screenExposureEV"};
        case Kind::screenCMY: return {"screenCMY"};
        case Kind::printerLight: return {"printerLights"};
        case Kind::receiverBand: return {"receiverBands"};
        case Kind::labScanDodging: return {"labScanDodging"};
        case Kind::labScanLook: return {"labScanLook"};
        case Kind::enlargerIndex: return {"enlarger"};
        case Kind::whiteBalanceKelvin:
        case Kind::whiteBalanceDuv: return {"whiteBalance"};
        case Kind::halationScale: return {"halationScale"};
        case Kind::cameraPreflash: return {"cameraPreflash"};
        case Kind::printerPreflash: return {"printerPreflash"};
        case Kind::filmGrainSize:
        case Kind::filmColourGrain:
        case Kind::filmGrainLayer:
        case Kind::filmScanSoftness: return {"filmGrain"};
        case Kind::hdrRange: return {"hdrRange"};
        case Kind::hdrRollOff: return {"hdrRollOff"};
    }
    return {};
}

void EngineBinding::apply(const EditorControlValue& value, FotufilmEngine::Options& options) const {
    const std::optional<double> number = value.number();
    switch (kind) {
        case Kind::exposureEV:
            if (number) options.exposureEV = static_cast<float>(*number);
            break;
        case Kind::warmth:
            if (number) options.whiteBalance.kelvin = static_cast<float>(WarmthAxis::kelvinFromWarmth(*number));
            break;
        case Kind::tint:
            if (number) options.whiteBalance.tint = static_cast<float>(WarmthAxis::duvFromPadTint(*number));
            break;
        case Kind::highlights:
            if (number) options.highlights = static_cast<float>(*number);
            break;
        case Kind::shadows:
            if (number) options.shadows = static_cast<float>(*number);
            break;
        case Kind::localTone:
            if (auto flag = value.flag()) options.localTone = *flag;
            break;
        case Kind::saturation:
            if (number) options.saturation = static_cast<float>(*number);
            break;
        case Kind::vibrance:
            if (number) options.vibrance = static_cast<float>(*number);
            break;
        case Kind::grainScale:
            if (number) options.grainScale = static_cast<float>(*number);
            break;
        case Kind::grainMottleShare:
            options.grainMottleShare =
                number ? std::optional<float>(static_cast<float>(*number)) : std::optional<float>();
            break;
        case Kind::grainModel:
            if (value.type == EditorControlValue::Type::choice) {
                const int index = std::min(std::max(value.choiceValue, 0), 1);
                options.grainModel = index == 0 ? GrainModel::clumpField : GrainModel::film;
            }
            break;
        case Kind::halationStops:
            if (number) options.halationScale = static_cast<float>(HalationAmount::scale(*number));
            break;
        case Kind::halationReturnRatio:
            options.halationReturnRatio =
                number ? std::optional<float>(static_cast<float>(*number)) : std::optional<float>();
            break;
        case Kind::halationSourceColour:
            if (number) options.halationSourceColour = static_cast<float>(*number);
            break;
        case Kind::halationReturnGain:
            if (value.type == EditorControlValue::Type::curve) {
                std::vector<float> handles;
                handles.reserve(value.curveValue.size());
                for (double handle : value.curveValue) handles.push_back(static_cast<float>(handle));
                options.halationReturnGain = HalationSpectrum::resampled(handles);
            }
            break;
        case Kind::halationHazeMicrometers:
            // Zero keeps the film's own figure.
            if (number) {
                options.halationHazeMM =
                    *number > 0 ? std::optional<float>(static_cast<float>(*number / 1000)) : std::nullopt;
            }
            break;
        case Kind::antiHalationScale:
            if (number) options.antiHalationScale = static_cast<float>(*number);
            break;
        case Kind::baseThicknessScale:
            if (number) options.baseThicknessScale = static_cast<float>(*number);
            break;
        case Kind::pressurePlateReflectance:
            if (number) options.pressurePlateReflectance = static_cast<float>(*number);
            break;
        case Kind::couplerScale:
            if (number) options.couplerScale = static_cast<float>(*number);
            break;
        case Kind::couplerGapReach:
            if (number) {
                options.couplerGapReachScales =
                    std::vector<float>{static_cast<float>(*number), static_cast<float>(*number)};
            }
            break;
        case Kind::couplerSelf:
            if (number) options.couplerSelfScale = static_cast<float>(*number);
            break;
        case Kind::chromaticFringeAmount:
            if (number) options.chromaticFringeAmount = static_cast<float>(*number);
            break;
        case Kind::chromaticFringeRadiusMicrometers:
            if (number) options.chromaticFringeRadiusMM = static_cast<float>(*number / 1000);
            break;
        case Kind::developmentEV:
            if (number) options.developmentEV = static_cast<float>(*number);
            break;
        case Kind::bleachBypass:
            if (number) options.bleachBypass = static_cast<float>(*number);
            break;
        case Kind::expiredYears:
            if (number) options.expiredYears = static_cast<float>(*number);
            break;
        case Kind::shutterSeconds:
            options.shutterSeconds = positiveFloat(number);
            break;
        case Kind::printViewingKelvin:
            options.printViewingKelvin = positiveFloat(number);
            break;
        case Kind::printCorrection:
            if (number) options.printCorrection = static_cast<float>(*number);
            break;
        case Kind::grade:
            if (number) gradeAxis(gradeBand(options.grade, band), axis) = static_cast<float>(*number);
            break;
        case Kind::gradeSpaceEncoded:
            if (auto flag = value.flag()) options.gradeSpace = *flag ? ColorGrade::Space::encoded : ColorGrade::Space::linear;
            break;
        case Kind::flareScale:
            if (number) options.flareScale = static_cast<float>(*number);
            break;
        case Kind::estimatedHalationProfile:
            if (auto flag = value.flag()) options.useEstimatedHalationProfile = *flag;
            break;
        case Kind::halationModelIndex:
            if (auto index = value.choice()) options.halationModel = *index == 1 ? HalationModel::layered : HalationModel::legacy;
            break;
        case Kind::negativeViewingIndex:
            if (auto index = value.choice()) {
                const auto& all = allNegativeViewings();
                if (*index >= 0 && *index < static_cast<int>(all.size())) options.negativeViewing = all[*index];
            }
            break;
        case Kind::frameCoverage:
            if (number) options.frameCoverage = static_cast<float>(*number);
            break;
        case Kind::focalLengthMM:
            options.focalLengthMM = positiveFloat(number);
            break;
        case Kind::sceneIlluminantKelvin:
            if (number && std::isfinite(*number) && *number >= 1000 && *number <= 25000) {
                options.sceneIlluminantKelvin = static_cast<float>(*number);
            } else {
                options.sceneIlluminantKelvin = std::nullopt;
            }
            // Selecting a temperature or Stock Native replaces the entire source choice.
            options.sceneIlluminantChromaticity = std::nullopt;
            options.sceneIlluminantSpectrum.clear();
            break;
        case Kind::stageIndex:
            if (auto index = value.choice()) {
                if (auto stage = pipelineStageFromOrdinal(static_cast<int32_t>(*index))) options.stage = *stage;
            }
            break;
        case Kind::textureStagesMask:
            if (auto mask = value.choice()) {
                options.textureStages = TextureStages(static_cast<int32_t>(*mask) & TextureStages::all().rawValue);
            }
            break;
        case Kind::seed:
            if (number) options.seed = static_cast<uint64_t>(std::max(0.0, *number));
            break;
        case Kind::digitalReferenceIndex:
            if (auto index = value.choice()) {
                const auto& all = allDigitalReferenceStyles();
                if (*index >= 0 && *index < static_cast<int>(all.size())) options.digitalReference = all[*index];
            }
            break;
        case Kind::screenGrade:
            if (number) options.screenGrade = static_cast<float>(*number);
            break;
        case Kind::screenExposureStops:
            if (number) options.screenExposureEV = static_cast<float>(*number);
            break;
        case Kind::screenCMY:
            if (number) options.screenCMY[index] = static_cast<float>(*number);
            break;
        case Kind::printerLight:
            if (number) options.printerLights[index] = static_cast<float>(*number);
            break;
        case Kind::receiverBand:
            if (number) options.receiverBands[index] = static_cast<float>(*number);
            break;
        case Kind::labScanDodging:
            if (number) options.labScanDodging = static_cast<float>(*number);
            break;
        case Kind::labScanLook:
            if (number) options.labScanLook = static_cast<float>(*number);
            break;
        case Kind::enlargerIndex:
            if (auto index = value.choice()) {
                const auto& all = allEnlargers();
                if (*index >= 0 && *index < static_cast<int>(all.size())) options.enlarger = all[*index];
            }
            break;
        case Kind::whiteBalanceKelvin:
            if (number) options.whiteBalance.kelvin = static_cast<float>(*number);
            break;
        case Kind::whiteBalanceDuv:
            if (number) options.whiteBalance.tint = static_cast<float>(*number);
            break;
        case Kind::halationScale:
            if (number) options.halationScale = static_cast<float>(*number);
            break;
        case Kind::cameraPreflash:
            if (number) options.cameraPreflash = static_cast<float>(*number);
            break;
        case Kind::printerPreflash:
            if (number) options.printerPreflash = static_cast<float>(*number);
            break;
        case Kind::filmGrainSize:
            if (number) options.filmGrain.size = static_cast<float>(*number);
            break;
        case Kind::filmColourGrain:
            if (number) options.filmGrain.colour = static_cast<float>(*number);
            break;
        case Kind::filmGrainLayer:
            if (number) options.filmGrain.layers[index] = static_cast<float>(*number);
            break;
        case Kind::filmScanSoftness:
            if (number) options.filmGrain.softness = static_cast<float>(*number);
            break;
        case Kind::hdrRange:
            if (number) options.hdrRange = static_cast<float>(*number);
            break;
        case Kind::hdrRollOff:
            if (number) options.hdrRollOff = static_cast<float>(*number);
            break;
    }
}

EditorMenuChoice::EditorMenuChoice(std::optional<double> value_, std::string label_, std::string detail_,
                                   std::optional<std::string> id_)
    : value(value_), label(std::move(label_)), detail(std::move(detail_)) {
    id = id_ ? *id_ : slug(label);
}

std::string EditorMenuChoice::slug(const std::string& label) {
    // Lowercased letters and digits, runs of anything else collapsed to one dash. Every choice the
    // ported catalogue builds names its id, so only ASCII letters count as alphabetic here.
    std::string out;
    bool pendingDash = false;
    for (unsigned char c : label) {
        const unsigned char lowered = static_cast<unsigned char>(std::tolower(c));
        if ((lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9')) {
            if (pendingDash && !out.empty()) out.push_back('-');
            pendingDash = false;
            out.push_back(static_cast<char>(lowered));
        } else {
            pendingDash = true;
        }
    }
    return out;
}

const char* rawValue(EditorDynamicMenu menu) {
    switch (menu) {
        case EditorDynamicMenu::stocks: return "stocks";
        case EditorDynamicMenu::gauges: return "gauges";
        case EditorDynamicMenu::papers: return "papers";
        case EditorDynamicMenu::lensProfiles: return "lensProfiles";
        case EditorDynamicMenu::lensFilters: return "lensFilters";
        case EditorDynamicMenu::meterings: return "meterings";
        case EditorDynamicMenu::diffusionFamilies: return "diffusionFamilies";
        case EditorDynamicMenu::diffusionGrades: return "diffusionGrades";
        case EditorDynamicMenu::negativeViewings: return "negativeViewings";
        case EditorDynamicMenu::stages: return "stages";
        case EditorDynamicMenu::textureStages: return "textureStages";
        case EditorDynamicMenu::shutterTimes: return "shutterTimes";
        case EditorDynamicMenu::viewingLights: return "viewingLights";
        case EditorDynamicMenu::colourSpaces: return "colourSpaces";
        case EditorDynamicMenu::pushConditions: return "pushConditions";
    }
    return "stocks";
}

}  // namespace fotufilm
