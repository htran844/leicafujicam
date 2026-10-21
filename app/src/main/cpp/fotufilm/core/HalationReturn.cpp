// Port of Sources/FotufilmCore/HalationReturn.swift.
#include "HalationReturn.h"

#include <cmath>

#include "FilmStock.h"
#include "Simd.h"

namespace fotufilm {
namespace HalationReturn {

void validate(float ratio) {
    if (!(std::isfinite(ratio) && inClosedRange(ratio, rangeLower, rangeUpper))) {
        throw TransportError("halation return must be a finite ratio from 0 through 1");
    }
}

std::vector<float> ratios(const FilmStock& stock, std::optional<float> ratio) {
    if (!ratio) return stock.halationStrength;
    validate(*ratio);
    if (*ratio == 0) return std::vector<float>(stock.halationStrength.size(), 0.0f);
    if (stock.halationStrength.empty() || !std::isfinite(stock.halationStrength[0]) ||
        !(stock.halationStrength[0] > 0)) {
        throw TransportError("a halation return override requires a positive red reference");
    }
    float red = stock.halationStrength[0];
    std::vector<float> result;
    for (float v : stock.halationStrength) result.push_back((v / red) * *ratio);
    return result;
}

LayeredTransport applying(std::optional<float> ratio, const LayeredTransport& supplied) {
    if (!ratio) return supplied;
    validate(*ratio);
    supplied.validate();
    LayeredTransport model = supplied;
    if (*ratio == 0) {
        for (auto& row : model.returnedToDirect) {
            for (double& v : row) v = 0;
        }
        return model;
    }
    const std::vector<double>& red = model.returnedToDirect[0];
    double sum = 0;
    for (double v : red) sum = sum + v;
    double reference = sum / static_cast<double>(red.size());
    if (!(reference > 0)) throw TransportError("a halation return override requires a positive red reference");
    for (auto& row : model.returnedToDirect) {
        for (double& v : row) v = (v / reference) * static_cast<double>(*ratio);
    }
    return model;
}

}  // namespace HalationReturn
}  // namespace fotufilm
