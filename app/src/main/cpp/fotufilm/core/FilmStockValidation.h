// Port of Sources/FotufilmCore/FilmStockValidation.swift: the failure type. `validate()` is a
// member of FilmStockDefinition (FilmStockPack.h) and is defined in FilmStockValidation.cpp.
#ifndef FOTUFILM_CORE_FILM_STOCK_VALIDATION_H
#define FOTUFILM_CORE_FILM_STOCK_VALIDATION_H

#include <stdexcept>
#include <string>

namespace fotufilm {

/// Bounds-check failure for a stock that arrived from outside; `what()` is "field: reason".
class ValidationFailure : public std::runtime_error {
public:
    ValidationFailure(std::string field, std::string reason)
        : std::runtime_error(field + ": " + reason), field_(std::move(field)), reason_(std::move(reason)) {}
    const std::string& field() const { return field_; }
    const std::string& reason() const { return reason_; }

private:
    std::string field_;
    std::string reason_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_STOCK_VALIDATION_H
