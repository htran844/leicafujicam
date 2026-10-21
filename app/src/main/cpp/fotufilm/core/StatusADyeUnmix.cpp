// Port of Sources/FotufilmCore/StatusADyeUnmix.swift.
#include "StatusADyeUnmix.h"

#include <cmath>

#include "Densitometry.h"
#include "SpectralGrid.h"

namespace fotufilm {

StatusADyeUnmix::StatusADyeUnmix(const std::vector<std::vector<float>>& dyes) {
    for (int i = 0; i < SpectralGrid::count; ++i) dye_.push_back(Float3(dyes[0][i], dyes[1][i], dyes[2][i]));
    const auto& statusA = Densitometry::statusA();
    float integrals[3];
    for (int b = 0; b < 3; ++b) {
        float sum = 0;
        for (float x : statusA[b]) sum += x;
        integrals[b] = sum;
    }
    for (int i = 0; i < SpectralGrid::count; ++i) {
        band_.push_back(Float3(statusA[0][i] / integrals[0], statusA[1][i] / integrals[1],
                               statusA[2][i] / integrals[2]));
    }
}

void StatusADyeUnmix::measurement(Float3 amounts, Float3& density, Float3 moments[3]) const {
    static const float log2Of10 = static_cast<float>(std::log2(10.0));
    Float3 sum = Float3::repeating(0);
    for (int c = 0; c < 3; ++c) moments[c] = Float3::repeating(0);
    for (size_t i = 0; i < dye_.size(); ++i) {
        float transmission = std::exp2(-dot(amounts, dye_[i]) * log2Of10);
        Float3 weight = band_[i] * transmission;
        sum += weight;
        for (int channel = 0; channel < 3; ++channel) moments[channel] += dye_[i] * weight[channel];
    }
    density = Float3::repeating(0);
    for (int channel = 0; channel < 3; ++channel) {
        float energy = swiftMax(sum[channel], 1e-20f);
        density[channel] = -std::log10(energy);
        moments[channel] /= energy;
    }
}

Float3 StatusADyeUnmix::amounts(Float3 target) const {
    Float3 amounts = Float3::repeating(0);
    for (int iteration = 0; iteration < 24; ++iteration) {
        Float3 density;
        Float3 jacobian[3];
        measurement(amounts, density, jacobian);
        Float3 residual = density - target;
        if (swiftMax(std::fabs(residual.x), std::fabs(residual.y), std::fabs(residual.z)) < 1e-6f) break;
        // Linearized nonnegative least squares over the eight active sets of three dyes.
        Float3 b;
        for (int i = 0; i < 3; ++i) b[i] = dot(jacobian[i], amounts) - residual[i];
        const float regularization = 1e-7f;
        Float3 normal[3] = {Float3(), Float3(), Float3()};
        Float3 rhs = Float3::repeating(0);
        for (int j = 0; j < 3; ++j) {
            rhs[j] = regularization * amounts[j];
            for (int k = 0; k < 3; ++k) {
                normal[j][k] = (j == k ? regularization : 0);
                for (int channel = 0; channel < 3; ++channel) {
                    normal[j][k] += jacobian[channel][j] * jacobian[channel][k];
                }
            }
            for (int channel = 0; channel < 3; ++channel) rhs[j] += jacobian[channel][j] * b[channel];
        }
        Float3 best = Float3::repeating(0);
        float bestError = dot(b, b) + regularization * dot(amounts, amounts);
        for (int mask = 1; mask < 8; ++mask) {
            std::vector<int> active;
            for (int i = 0; i < 3; ++i) {
                if ((mask & (1 << i)) != 0) active.push_back(i);
            }
            std::optional<Float3> solved = solve(normal, rhs, active);
            if (!solved) continue;
            Float3 candidate = *solved;
            if (!(candidate.x >= 0 && candidate.y >= 0 && candidate.z >= 0)) continue;
            Float3 error;
            for (int i = 0; i < 3; ++i) error[i] = dot(jacobian[i], candidate) - b[i];
            Float3 distance = candidate - amounts;
            float score = dot(error, error) + regularization * dot(distance, distance);
            if (score < bestError) {
                bestError = score;
                best = candidate;
            }
        }
        Float3 step = best - amounts;
        if (swiftMax(std::fabs(step.x), std::fabs(step.y), std::fabs(step.z)) < 2e-7f) break;
        float scale = 1;
        float error = dot(residual, residual);
        bool accepted = false;
        for (int attempt = 0; attempt < 12; ++attempt) {
            Float3 candidate = amounts + scale * step;
            Float3 nextDensity;
            Float3 unused[3];
            measurement(candidate, nextDensity, unused);
            Float3 next = nextDensity - target;
            if (dot(next, next) < error) {
                amounts = candidate;
                accepted = true;
                break;
            }
            scale *= 0.5f;
        }
        if (!accepted) break;
    }
    return amounts;
}

std::optional<Float3> StatusADyeUnmix::solve(const Float3 matrix[3], Float3 rhs, const std::vector<int>& active) {
    Float3 a[3] = {matrix[0], matrix[1], matrix[2]};
    Float3 b = rhs;
    for (size_t index = 0; index < active.size(); ++index) {
        int column = active[index];
        if (!(a[column][column] > 1e-10f)) return std::nullopt;
        for (size_t r = index + 1; r < active.size(); ++r) {
            int row = active[r];
            float factor = a[row][column] / a[column][column];
            for (size_t kk = index; kk < active.size(); ++kk) {
                int k = active[kk];
                a[row][k] -= factor * a[column][k];
            }
            b[row] -= factor * b[column];
        }
    }
    Float3 result = Float3::repeating(0);
    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        int row = *it;
        float value = b[row];
        for (int k : active) {
            if (k > row) value -= a[row][k] * result[k];
        }
        result[row] = value / a[row][row];
    }
    return result;
}

}  // namespace fotufilm
