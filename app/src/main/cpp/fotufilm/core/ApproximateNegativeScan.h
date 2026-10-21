// Port of Sources/FotufilmCore/ApproximateNegativeScanReading.swift for the case this engine uses:
// a print span handed densities, never a scanned negative. Scanned-negative import
// (ApproximateNegativeScan.swift, NegativeScanCrosstalk.swift) is not ported, so the reading is
// always the nil one.
#ifndef FOTUFILM_CORE_APPROXIMATE_NEGATIVE_SCAN_H
#define FOTUFILM_CORE_APPROXIMATE_NEGATIVE_SCAN_H

#include <vector>

namespace fotufilm {
namespace ApproximateNegativeScan {

/// FOTUFILM_CONFIG_SCAN_READING followed by FOTUFILM_CONFIG_SCAN_CROSSTALK for a span handed
/// densities: every slot zero (mode 0).
std::vector<float> packedReadingNone();

}  // namespace ApproximateNegativeScan
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_APPROXIMATE_NEGATIVE_SCAN_H
