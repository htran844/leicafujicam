// Port of Sources/FotufilmCore/FilmFormat.swift.
#include "FilmFormat.h"

#include <cmath>
#include <limits>

#include "FilmStockPack.h"
#include "Simd.h"

namespace fotufilm {

namespace {
/// Motion-picture camera negative is coated on acetate safety base of about 0.0053 inch.
FilmBase cineBase() { return FilmBase::acetate(0.135f); }
/// An integral instant sheet.
FilmBase instaxSheet() { return FilmBase::estar(0.10f); }

/// How far apart two frame heights are, counted in enlargement.
float distance(float measured, float gauge) {
    return gauge > 0 ? swiftMax(measured / gauge, gauge / measured) : std::numeric_limits<float>::infinity();
}
}  // namespace

const char* const FilmFormat::houseDefaultID = "35mm";

FilmFormat::FilmFormat(std::string name_, float frameHeightMM_, float frameAspectRatio_, FilmBase base_)
    : name(std::move(name_)), frameHeightMM(frameHeightMM_), frameAspectRatio(frameAspectRatio_), base(std::move(base_)) {}

float FilmFormat::normalFocalLengthMM() const {
    return frameHeightMM * std::sqrt(1 + frameAspectRatio * frameAspectRatio);
}

bool FilmFormat::isMotionPicture() const { return base == cineBase(); }

const FilmFormat& FilmFormat::super8() {
    static const FilmFormat f("Super 8", 4.0f, 5.79f / 4.01f, cineBase());
    return f;
}
const FilmFormat& FilmFormat::sixteenMM() {
    static const FilmFormat f("16mm", 7.4f, 12.5f / 7.4f, cineBase());
    return f;
}
const FilmFormat& FilmFormat::super35() {
    static const FilmFormat f("35mm motion (Super 35)", 18.7f, 24.9f / 18.7f, cineBase());
    return f;
}
const FilmFormat& FilmFormat::still35() {
    static const FilmFormat f("35mm still", 24, 36.0f / 24.0f, FilmBase::acetate(0.13f));
    return f;
}
const FilmFormat& FilmFormat::mediumFormat120() {
    static const FilmFormat f("120 medium format (6x6)", 56, 1, FilmBase::acetate(0.10f));
    return f;
}
const FilmFormat& FilmFormat::largeFormat4x5() {
    static const FilmFormat f("4x5 large format", 95, 120.0f / 95.0f, FilmBase::estar(0.19f));
    return f;
}
const FilmFormat& FilmFormat::instaxMini() {
    static const FilmFormat f("instax mini", 46, 62.0f / 46.0f, instaxSheet());
    return f;
}
const FilmFormat& FilmFormat::instaxSquare() {
    static const FilmFormat f("instax SQUARE", 62, 1, instaxSheet());
    return f;
}
const FilmFormat& FilmFormat::instaxWide() {
    static const FilmFormat f("instax wide", 62, 99.0f / 62.0f, instaxSheet());
    return f;
}

const std::vector<std::pair<std::string, FilmFormat>>& FilmFormat::presets() {
    static const std::vector<std::pair<std::string, FilmFormat>> list = {
        {"super8", super8()},
        {"16mm", sixteenMM()},
        {"super35", super35()},
        {"35mm", still35()},
        {"instaxmini", instaxMini()},
        {"120", mediumFormat120()},
        {"instaxsquare", instaxSquare()},
        {"instaxwide", instaxWide()},
        {"4x5", largeFormat4x5()},
    };
    return list;
}

std::optional<FilmFormat> FilmFormat::preset(const std::string& id) {
    for (const auto& entry : presets()) {
        if (entry.first == id) return entry.second;
    }
    return std::nullopt;
}

std::string FilmFormat::nativeID(const std::string& stockID) {
    std::optional<std::string> named = FilmStockRegistry::shared().nativeFormatID(stockID);
    if (!named || !preset(*named)) return houseDefaultID;
    return *named;
}

FilmFormat FilmFormat::native(const std::string& stockID) {
    auto found = preset(nativeID(stockID));
    return found ? *found : still35();
}

const std::vector<std::string>& FilmFormat::sensorMatchIDs() {
    static const std::vector<std::string> ids = {"16mm", "super35", "35mm", "120", "4x5"};
    return ids;
}

std::pair<std::string, FilmFormat> FilmFormat::nearest(float height) {
    std::vector<std::pair<std::string, FilmFormat>> candidates;
    for (const auto& id : sensorMatchIDs()) {
        auto format = preset(id);
        if (format) candidates.emplace_back(id, *format);
    }
    if (!(height > 0) || candidates.empty()) return {houseDefaultID, still35()};
    // `min(by:)` keeps the first of equal candidates.
    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); ++i) {
        if (distance(height, candidates[i].second.frameHeightMM) <
            distance(height, candidates[best].second.frameHeightMM)) {
            best = i;
        }
    }
    return candidates[best];
}

FilmFormat FilmFormat::resolved(const std::optional<std::string>& chosenID,
                                const std::optional<float>& sensorShortSideMM, const std::string& stockID) {
    FilmFormat stockGauge = native(stockID);
    if (chosenID) {
        auto chosen = preset(*chosenID);
        return chosen ? *chosen : stockGauge;
    }
    if (!sensorShortSideMM) return stockGauge;
    return nearest(*sensorShortSideMM).second;
}

}  // namespace fotufilm
