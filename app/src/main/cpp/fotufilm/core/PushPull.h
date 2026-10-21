// Port of Sources/FotufilmCore/PushPull.swift. The `FilmStock` extension (supportedDevelopmentStops,
// hasDevelopmentConditions, supportsDevelopment, pushed) is declared in FilmStock.h and defined in
// PushPull.cpp.
#ifndef FOTUFILM_CORE_PUSH_PULL_H
#define FOTUFILM_CORE_PUSH_PULL_H

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "CharacteristicCurve.h"

namespace fotufilm {

/// What stands behind a development condition's curves.
struct FilmDevelopmentBasis {
    enum class Kind { measured, transferred, estimated };
    Kind kind = Kind::measured;
    /// The stock whose published push or pull a `transferred` condition carries.
    std::string from;

    static FilmDevelopmentBasis measured() { return {Kind::measured, {}}; }
    static FilmDevelopmentBasis transferred(std::string from) { return {Kind::transferred, std::move(from)}; }
    static FilmDevelopmentBasis estimated() { return {Kind::estimated, {}}; }

    bool operator==(const FilmDevelopmentBasis& o) const {
        return kind == o.kind && (kind != Kind::transferred || from == o.from);
    }
    bool operator!=(const FilmDevelopmentBasis& o) const { return !(*this == o); }
};

/// One result of developing a stock away from its pack's reference process.
struct FilmDevelopmentCondition {
    /// Exposure-rating difference from the pack's reference process, in stops.
    float stops;
    /// Label printed in editors and validation errors.
    std::string label;
    /// Development time the condition describes.
    float timeMinutes;
    /// Meter setting associated with this condition, where the source states one.
    std::optional<float> exposureIndex;
    /// Where the curves come from.
    FilmDevelopmentBasis basis;
    /// Complete dye-forming layer curves at this condition.
    std::vector<CharacteristicCurve> curves;
    /// Complete donor-layer curves, where the stock coats donor records.
    std::vector<CharacteristicCurve> donorCurves;

    std::optional<float> grainStrength;
    std::optional<float> grainSizeMM;
    std::optional<std::vector<float>> grainLayerWeights;
    std::optional<float> grainFogDensity;
    std::optional<float> adjacencyStrength;
};

/// The fixed process shared by a stock's measured development conditions.
struct FilmDevelopmentProfile {
    std::string developer;
    std::optional<std::string> dilution;
    float temperatureC;
    std::string agitation;
    std::string source;
    int64_t sourcePage;
    std::vector<FilmDevelopmentCondition> conditions;
};

/// An invalid development request.
class FilmDevelopmentError : public std::runtime_error {
public:
    enum class Kind { unavailable, unlistedCondition, invalidProfile };

    static FilmDevelopmentError unavailable(const std::string& stock, float requestedStops);
    static FilmDevelopmentError unlistedCondition(const std::string& stock, float requestedStops,
                                                  const std::vector<float>& availableStops);
    static FilmDevelopmentError invalidProfile(const std::string& stock, const std::string& condition,
                                               const std::string& reason);

    Kind kind() const { return kind_; }
    /// `description`, without the "invalid development request: " prefix `what()` carries.
    const std::string& description() const { return description_; }

    /// Swift `String(format:)` of a stop count as the error messages print it.
    static std::string stops(float value);

private:
    FilmDevelopmentError(Kind kind, std::string description)
        : std::runtime_error("invalid development request: " + description),
          kind_(kind),
          description_(std::move(description)) {}
    Kind kind_;
    std::string description_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PUSH_PULL_H
