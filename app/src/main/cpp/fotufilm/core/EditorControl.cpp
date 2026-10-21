// Port of Sources/FotufilmEditModel/EditorControl.swift.
#include "EditorControl.h"

#include <cmath>
#include <cstdio>

#include "FilmStock.h"

namespace fotufilm {

const char* rawValue(EditorControlField field) {
    switch (field) {
#define FOTUFILM_FIELD_NAME(name) \
    case EditorControlField::name: return #name;
        FOTUFILM_EDITOR_CONTROL_FIELDS(FOTUFILM_FIELD_NAME)
#undef FOTUFILM_FIELD_NAME
    }
    return "";
}

std::optional<EditorControlField> editorControlFieldFromRawValue(const std::string& raw) {
#define FOTUFILM_FIELD_MATCH(name) \
    if (raw == #name) return EditorControlField::name;
    FOTUFILM_EDITOR_CONTROL_FIELDS(FOTUFILM_FIELD_MATCH)
#undef FOTUFILM_FIELD_MATCH
    return std::nullopt;
}

bool needsFilmGrainModel(EditorControlField field) {
    switch (field) {
        case EditorControlField::filmGrainSize:
        case EditorControlField::filmColourGrain:
        case EditorControlField::filmRedLayer:
        case EditorControlField::filmGreenLayer:
        case EditorControlField::filmBlueLayer:
        case EditorControlField::filmScanSoftness: return true;
        default: return false;
    }
}

const char* rawValue(EditorControlGroup group) {
    switch (group) {
        case EditorControlGroup::film: return "film";
        case EditorControlGroup::lens: return "lens";
        case EditorControlGroup::light: return "light";
        case EditorControlGroup::print: return "print";
        case EditorControlGroup::frame: return "frame";
        case EditorControlGroup::pipeline: return "pipeline";
    }
    return "film";
}

const char* title(EditorControlGroup group) {
    switch (group) {
        case EditorControlGroup::film: return "Film";
        case EditorControlGroup::lens: return "Lens";
        case EditorControlGroup::light: return "Light & Color";
        case EditorControlGroup::print: return "Print";
        case EditorControlGroup::frame: return "Frame";
        case EditorControlGroup::pipeline: return "Pipeline";
    }
    return "";
}

const char* rawValue(EditorControlSection section) {
    switch (section) {
        case EditorControlSection::filmStock: return "filmStock";
        case EditorControlSection::filmGrain: return "filmGrain";
        case EditorControlSection::filmHalation: return "filmHalation";
        case EditorControlSection::filmEmulsion: return "filmEmulsion";
        case EditorControlSection::filmLab: return "filmLab";
        case EditorControlSection::lensGlass: return "lensGlass";
        case EditorControlSection::lensCorrection: return "lensCorrection";
        case EditorControlSection::lightExposure: return "lightExposure";
        case EditorControlSection::lightBalance: return "lightBalance";
        case EditorControlSection::lightColor: return "lightColor";
        case EditorControlSection::lightGrade: return "lightGrade";
        case EditorControlSection::hdrHighlights: return "hdrHighlights";
        case EditorControlSection::printPaper: return "printPaper";
        case EditorControlSection::printLamp: return "printLamp";
        case EditorControlSection::frameGeometry: return "frameGeometry";
        case EditorControlSection::frameLocal: return "frameLocal";
        case EditorControlSection::pipeline: return "pipeline";
    }
    return "";
}

const char* title(EditorControlSection section) {
    switch (section) {
        case EditorControlSection::filmStock: return "Stock";
        case EditorControlSection::filmGrain: return "Grain";
        case EditorControlSection::filmHalation: return "Halation";
        case EditorControlSection::filmEmulsion: return "Emulsion";
        case EditorControlSection::filmLab: return "Lab";
        case EditorControlSection::lensGlass: return "Filters";
        case EditorControlSection::lensCorrection: return "Correction";
        case EditorControlSection::lightExposure: return "Exposure";
        case EditorControlSection::hdrHighlights: return "HDR Highlights";
        case EditorControlSection::lightBalance: return "Balance";
        case EditorControlSection::lightColor: return "Color";
        case EditorControlSection::lightGrade: return "Grade";
        case EditorControlSection::printPaper: return "Output";
        case EditorControlSection::printLamp: return "Lamp";
        case EditorControlSection::frameGeometry: return "Geometry";
        case EditorControlSection::frameLocal: return "Local";
        case EditorControlSection::pipeline: return "Pipeline";
    }
    return "";
}

EditorControlGroup group(EditorControlSection section) {
    switch (section) {
        case EditorControlSection::filmStock:
        case EditorControlSection::filmGrain:
        case EditorControlSection::filmHalation:
        case EditorControlSection::filmEmulsion:
        case EditorControlSection::filmLab: return EditorControlGroup::film;
        case EditorControlSection::lensGlass:
        case EditorControlSection::lensCorrection: return EditorControlGroup::lens;
        case EditorControlSection::lightExposure:
        case EditorControlSection::lightBalance:
        case EditorControlSection::lightColor:
        case EditorControlSection::lightGrade:
        case EditorControlSection::hdrHighlights: return EditorControlGroup::light;
        case EditorControlSection::printPaper:
        case EditorControlSection::printLamp: return EditorControlGroup::print;
        case EditorControlSection::frameGeometry:
        case EditorControlSection::frameLocal: return EditorControlGroup::frame;
        case EditorControlSection::pipeline: return EditorControlGroup::pipeline;
    }
    return EditorControlGroup::film;
}

bool transfersByDefault(EditorControlSection section) {
    switch (section) {
        case EditorControlSection::hdrHighlights:
        case EditorControlSection::frameGeometry:
        case EditorControlSection::frameLocal:
        case EditorControlSection::pipeline: return false;
        default: return true;
    }
}

const char* rawValue(EditorControlUnit unit) {
    switch (unit) {
        case EditorControlUnit::multiplier: return "multiplier";
        case EditorControlUnit::stops: return "stops";
        case EditorControlUnit::stopsFromOff: return "stopsFromOff";
        case EditorControlUnit::signedValue: return "signed";
        case EditorControlUnit::percent: return "percent";
        case EditorControlUnit::years: return "years";
        case EditorControlUnit::seconds: return "seconds";
        case EditorControlUnit::degrees: return "degrees";
        case EditorControlUnit::kelvin: return "kelvin";
        case EditorControlUnit::opticalDensity: return "opticalDensity";
        case EditorControlUnit::micrometers: return "micrometers";
        case EditorControlUnit::millimetres: return "millimetres";
        case EditorControlUnit::nanometres: return "nanometres";
        case EditorControlUnit::printerPoints: return "printerPoints";
        case EditorControlUnit::none: return "none";
    }
    return "none";
}

namespace {

double shown(double value, int places) {
    const double scale = std::pow(10.0, static_cast<double>(places));
    const double rounded = std::round(value * scale) / scale;
    return rounded == 0 ? 0 : rounded;
}

std::string printf1(const char* pattern, double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, pattern, value);
    return buffer;
}

}  // namespace

std::string format(EditorControlUnit unit, double value) {
    switch (unit) {
        case EditorControlUnit::multiplier: return printf1("%.2f\xC3\x97", shown(value, 2));
        case EditorControlUnit::stops: return printf1("%+.1f EV", shown(value, 1));
        case EditorControlUnit::stopsFromOff:
            return value <= -6.0 ? std::string("Off") : format(EditorControlUnit::stops, value);
        case EditorControlUnit::signedValue: return printf1("%+.2f", shown(value, 2));
        case EditorControlUnit::percent: return printf1("%.0f%%", shown(value * 100, 0));
        case EditorControlUnit::years: return value == 0 ? std::string("Fresh") : printf1("%.0f yr", value);
        case EditorControlUnit::seconds:
            if (value >= 1) return printf1("%.0f s", value);
            return printf1("%.2f s", shown(value, 2));
        case EditorControlUnit::degrees: return printf1("%+.1f\xC2\xB0", shown(value, 1));
        case EditorControlUnit::kelvin: return printf1("%.0f K", shown(value, 0));
        case EditorControlUnit::opticalDensity: return printf1("%.2f OD", shown(value, 2));
        case EditorControlUnit::micrometers: return printf1("%.0f \xC2\xB5m", shown(value, 0));
        case EditorControlUnit::millimetres: return printf1("%.0f mm", shown(value, 0));
        case EditorControlUnit::nanometres: return printf1("%.0f nm", shown(value, 0));
        case EditorControlUnit::printerPoints: return printf1("%.0f", shown(value, 0));
        case EditorControlUnit::none: return "";
    }
    return "";
}

bool EditorControlScale::isMoved(double value) const {
    return std::abs(value - neutral) > (rangeUpper - rangeLower) * 1e-4;
}

bool EditorControlCurve::isMoved(const std::vector<double>& values) const {
    if (values.size() != handles.size()) return false;
    const double slack = (rangeUpper - rangeLower) * 1e-4;
    for (double value : values) {
        if (std::abs(value - neutral) > slack) return true;
    }
    return false;
}

const char* EditorControlKind::typeName() const {
    switch (type) {
        case Type::slider: return "slider";
        case Type::chips: return "chips";
        case Type::toggle: return "toggle";
        case Type::menu: return "menu";
        case Type::takeover: return "takeover";
        case Type::curve: return "curve";
    }
    return "takeover";
}

const char* rawValue(EditorControlAvailability availability) {
    switch (availability) {
        case EditorControlAvailability::always: return "always";
        case EditorControlAvailability::film: return "film";
        case EditorControlAvailability::colourNegative: return "colourNegative";
        case EditorControlAvailability::colourFilm: return "colourFilm";
        case EditorControlAvailability::transparentFilm: return "transparentFilm";
        case EditorControlAvailability::printStage: return "printStage";
        case EditorControlAvailability::statedReciprocity: return "statedReciprocity";
        case EditorControlAvailability::couplerGeometry: return "couplerGeometry";
        case EditorControlAvailability::interlayerInhibition: return "interlayerInhibition";
        case EditorControlAvailability::developmentConditions: return "developmentConditions";
    }
    return "always";
}

bool admits(EditorControlAvailability availability, const FilmStock* stock) {
    switch (availability) {
        case EditorControlAvailability::always: return true;
        case EditorControlAvailability::film: return stock != nullptr;
        case EditorControlAvailability::colourNegative:
            if (!stock) return false;
            return !stock->isMonochrome && !stock->isReversal;
        case EditorControlAvailability::colourFilm:
            if (!stock) return false;
            return !stock->isMonochrome;
        case EditorControlAvailability::transparentFilm:
            if (!stock) return false;
            return !stock->isReflectionPrint;
        case EditorControlAvailability::printStage: return stock != nullptr;
        case EditorControlAvailability::statedReciprocity:
            if (!stock || !stock->reciprocityFailure) return false;
            return stock->reciprocityFailure->lostStopsPerDecade > 0;
        case EditorControlAvailability::couplerGeometry: return stock && stock->couplerGeometry().has_value();
        case EditorControlAvailability::interlayerInhibition: {
            if (!stock || stock->isMonochrome) return false;
            const auto& matrix = stock->couplerInhibition;
            for (size_t receiver = 0; receiver < matrix.size(); ++receiver) {
                for (size_t donor = 0; donor < matrix[receiver].size(); ++donor) {
                    if (receiver != donor && matrix[receiver][donor] != 0) return true;
                }
            }
            return false;
        }
        case EditorControlAvailability::developmentConditions: return stock && stock->hasDevelopmentConditions();
    }
    return false;
}

std::optional<int32_t> capabilityBit(EditorControlAvailability availability) {
    switch (availability) {
        case EditorControlAvailability::colourNegative: return 1;
        case EditorControlAvailability::couplerGeometry: return 2;
        case EditorControlAvailability::statedReciprocity: return 4;
        default: return std::nullopt;
    }
}

EditorControlPersistence EditorControl::defaultPersistence(EditorControlField field, const EditorControlKind& kind,
                                                           EditorControlScope scope) {
    if (scope != EditorControlScope::edit) return EditorControlPersistence::none();
    switch (kind.type) {
        case EditorControlKind::Type::slider:
        case EditorControlKind::Type::chips:
        case EditorControlKind::Type::toggle:
        case EditorControlKind::Type::curve: return EditorControlPersistence::keyed(rawValue(field), StoredEncoding::same);
        case EditorControlKind::Type::menu:
        case EditorControlKind::Type::takeover: return EditorControlPersistence::bespoke();
    }
    return EditorControlPersistence::bespoke();
}

std::optional<EditorControlValue> EditorControl::restingValue() const {
    switch (kind.type) {
        case EditorControlKind::Type::slider:
        case EditorControlKind::Type::chips: return EditorControlValue::makeNumber(kind.scaleValue.neutral);
        case EditorControlKind::Type::toggle: return EditorControlValue::makeFlag(kind.restingOn);
        case EditorControlKind::Type::curve: return EditorControlValue::makeCurve(kind.curveValue.restingValues());
        case EditorControlKind::Type::menu:
        case EditorControlKind::Type::takeover: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> EditorControl::storedNeutral() const {
    const EditorControlScale* scale = kind.scale();
    auto encoding = persistence.storedEncoding();
    if (!scale || !encoding) return std::nullopt;
    return stored(*encoding, scale->neutral);
}

}  // namespace fotufilm
