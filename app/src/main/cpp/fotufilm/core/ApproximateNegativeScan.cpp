// Port of Sources/FotufilmCore/ApproximateNegativeScanReading.swift (`packedReading(nil)`).
#include "ApproximateNegativeScan.h"

#include "FotufilmConfigLayout.h"

namespace fotufilm {
namespace ApproximateNegativeScan {

std::vector<float> packedReadingNone() {
    return std::vector<float>(FOTUFILM_CONFIG_SCAN_READING_COUNT + FOTUFILM_CONFIG_SCAN_CROSSTALK_COUNT, 0.0f);
}

}  // namespace ApproximateNegativeScan
}  // namespace fotufilm
