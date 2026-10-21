// Port of Sources/FotufilmCore/Densitometry.swift: Densitometry and PrintDyeUnmix.
#ifndef FOTUFILM_CORE_DENSITOMETRY_H
#define FOTUFILM_CORE_DENSITOMETRY_H

#include <vector>

#include "Simd.h"

namespace fotufilm {

namespace Densitometry {

/// ISO Status A bands on the engine's grid, carried from Hunt's 10 nm table linearly and zero outside.
const std::vector<std::vector<float>>& statusA();
Float3 statusADensity(const std::vector<float>& transmittance);
std::vector<float> transmittance(Float3 amounts, const std::vector<std::vector<float>>& dyes);
Float3 statusADensity(Float3 amounts, const std::vector<std::vector<float>>& dyes);

}  // namespace Densitometry

/// Carries a sheet's Status A integral densities back to the dye amounts behind them.
class PrintDyeUnmix {
public:
    explicit PrintDyeUnmix(const std::vector<std::vector<float>>& dyes);

    const std::vector<std::vector<float>>& dyes() const { return dyes_; }
    Float3 read(Float3 amounts) const;
    Float3 amounts(Float3 statusA) const;

private:
    Float3 apply(const std::vector<Float3>& m, Float3 v) const;
    static bool invert(const std::vector<Float3>& columns, std::vector<Float3>& out);

    /// log2(10), for 10^-d as exp2 in the table-build inner loop.
    static constexpr float log2Of10 = 3.321928f;

    std::vector<std::vector<float>> dyes_;
    std::vector<Float3> dye_;
    std::vector<Float3> band_;
    std::vector<Float3> matrix_;
    std::vector<Float3> inverse_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_DENSITOMETRY_H
