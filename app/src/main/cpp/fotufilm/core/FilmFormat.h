// Port of Sources/FotufilmCore/FilmFormat.swift.
#ifndef FOTUFILM_CORE_FILM_FORMAT_H
#define FOTUFILM_CORE_FILM_FORMAT_H

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Halation.h"

namespace fotufilm {

/// A film gauge.
struct FilmFormat {
    std::string name;
    /// Height of the exposed frame in millimeters — the frame's short side.
    float frameHeightMM;
    /// Long side divided by short side for the exposed image area.
    float frameAspectRatio;
    /// The support this gauge is coated on.
    FilmBase base;

    FilmFormat(std::string name, float frameHeightMM, float frameAspectRatio = 3.0f / 2.0f,
               FilmBase base = FilmBase::acetate(0.13f));

    /// Frame diagonal in millimetres, the default focal length when metadata is absent.
    float normalFocalLengthMM() const;
    /// Whether this gauge is a motion-picture camera negative rather than a still frame.
    bool isMotionPicture() const;

    static const FilmFormat& super8();
    static const FilmFormat& sixteenMM();
    static const FilmFormat& super35();
    static const FilmFormat& still35();
    static const FilmFormat& mediumFormat120();
    static const FilmFormat& largeFormat4x5();
    static const FilmFormat& instaxMini();
    static const FilmFormat& instaxSquare();
    static const FilmFormat& instaxWide();

    /// Ordered list (smallest gauge first) keyed by CLI/UI identifier.
    static const std::vector<std::pair<std::string, FilmFormat>>& presets();
    static std::optional<FilmFormat> preset(const std::string& id);

    /// Default gauge when neither the user, source metadata, nor stock specifies one.
    static const char* const houseDefaultID;

    /// The stock's declared gauge (from the registered pack definition), else the 35 mm default.
    static std::string nativeID(const std::string& stockID);
    static FilmFormat native(const std::string& stockID);

    /// Camera-negative gauges eligible for automatic sensor matching, ordered by size.
    static const std::vector<std::string>& sensorMatchIDs();
    /// The gauge with the nearest frame-height ratio.
    static std::pair<std::string, FilmFormat> nearest(float frameHeightMM);

    /// The gauge a develop runs on: the chosen one, else the gauge nearest the camera's measured
    /// frame (`SensorFrame.gauge`, i.e. nearest(shortSideMM)), else the film's own.
    static FilmFormat resolved(const std::optional<std::string>& chosenID,
                               const std::optional<float>& sensorShortSideMM, const std::string& stockID);

    bool operator==(const FilmFormat& o) const {
        return name == o.name && frameHeightMM == o.frameHeightMM && frameAspectRatio == o.frameAspectRatio &&
               base == o.base;
    }
    bool operator!=(const FilmFormat& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_FORMAT_H
