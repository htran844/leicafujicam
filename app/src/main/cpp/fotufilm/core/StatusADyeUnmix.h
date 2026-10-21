// Port of Sources/FotufilmCore/StatusADyeUnmix.swift.
#ifndef FOTUFILM_CORE_STATUS_A_DYE_UNMIX_H
#define FOTUFILM_CORE_STATUS_A_DYE_UNMIX_H

#include <optional>
#include <vector>

#include "Simd.h"

namespace fotufilm {

/// Converts integral Status A record densities to nonnegative dye amounts. Each iteration
/// integrates the actual transmitted spectrum and its Jacobian; unreachable triples minimize
/// squared density error on the dye gamut boundary.
class StatusADyeUnmix {
public:
    explicit StatusADyeUnmix(const std::vector<std::vector<float>>& dyes);

    Float3 amounts(Float3 statusA) const;

private:
    void measurement(Float3 amounts, Float3& density, Float3 moments[3]) const;
    static float dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static std::optional<Float3> solve(const Float3 matrix[3], Float3 rhs, const std::vector<int>& active);

    std::vector<Float3> dye_;
    std::vector<Float3> band_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_STATUS_A_DYE_UNMIX_H
