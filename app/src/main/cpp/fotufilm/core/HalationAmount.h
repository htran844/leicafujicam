// Port of Sources/FotufilmEditModel/HalationAmount.swift.
#ifndef FOTUFILM_CORE_HALATION_AMOUNT_H
#define FOTUFILM_CORE_HALATION_AMOUNT_H

#include <algorithm>
#include <cmath>

namespace fotufilm {

/// Converts between the halation multiplier stored by the engine and the stop scale displayed by
/// the editor. Zero stops preserves the stock value; the lower endpoint disables halation.
namespace HalationAmount {

/// `EditorControlUnit.offStops`.
constexpr double offStops = -6.0;

/// The drawn travel, in stops about the stock's authored look: six stops down is 1/64 and six
/// stops up is 64×. The lower endpoint represents off.
constexpr double travelLower = offStops;
constexpr double travelUpper = 6;

/// What the row admits without destroying it: multiples up to 100.
constexpr double admittedLower = offStops;
inline double admittedUpper() { return std::log2(100.0); }

/// The stored multiple, as the row draws it. A multiple at or under a sixty-fourth appears as off.
inline double stops(double scale) {
    if (!(scale > 0)) return offStops;
    return std::max(std::log2(scale), offStops);
}

/// The drawn stops, as the edit stores them.
inline double scale(double stops) { return stops <= offStops ? 0 : std::exp2(stops); }

}  // namespace HalationAmount
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HALATION_AMOUNT_H
