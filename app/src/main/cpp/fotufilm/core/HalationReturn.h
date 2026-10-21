// Port of Sources/FotufilmCore/HalationReturn.swift.
#ifndef FOTUFILM_CORE_HALATION_RETURN_H
#define FOTUFILM_CORE_HALATION_RETURN_H

#include <optional>
#include <vector>

#include "LayeredTransport.h"

namespace fotufilm {

struct FilmStock;

/// A returned/direct exposure ratio, before the amount and spectrum controls.
namespace HalationReturn {
constexpr float rangeLower = 0;
constexpr float rangeUpper = 1;

/// Throws TransportError for a non-finite ratio or one outside 0...1.
void validate(float ratio);
/// Preserve the stock's record balance while setting its red reference ratio.
std::vector<float> ratios(const FilmStock& stock, std::optional<float> ratio);
/// A spectral construction uses the arithmetic mean of its red ratios as its reference.
LayeredTransport applying(std::optional<float> ratio, const LayeredTransport& supplied);
}  // namespace HalationReturn

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HALATION_RETURN_H
