// Port of Sources/FotufilmCore/CouplerGeometry.swift.
#include "CouplerGeometry.h"

#include <cmath>
#include <utility>

#include "Simd.h"

namespace fotufilm {

const std::vector<float>& CouplerGeometry::colorNegativeDepthUM() {
    static const std::vector<float> depths = {14, 8, 3};
    return depths;
}

CouplerGeometry::CouplerGeometry(std::vector<float> layerDepthUM_, std::vector<float> interlayerTransmission_,
                                 std::vector<float> release_, float selfRetention_) {
    preconditionCheck(layerDepthUM_.size() == 3, "layerDepthUM must have 3 entries");
    preconditionCheck(release_.size() == 3, "release must have 3 entries");
    preconditionCheck(interlayerTransmission_.size() == layerDepthUM_.size() - 1,
                      "interlayerTransmission needs one entry per gap");
    layerDepthUM = std::move(layerDepthUM_);
    interlayerTransmission = std::move(interlayerTransmission_);
    release = std::move(release_);
    selfRetention = selfRetention_;
}

CouplerGeometry CouplerGeometry::onColorNegativeStack(std::vector<float> interlayerTransmission,
                                                      std::vector<float> release, float selfRetention) {
    return CouplerGeometry(colorNegativeDepthUM(), std::move(interlayerTransmission), std::move(release),
                           selfRetention);
}

std::vector<float> CouplerGeometry::transmission(float range, const std::vector<float>& layerDepthUM) {
    std::vector<float> out;
    for (size_t gap = 0; gap + 1 < layerDepthUM.size(); ++gap) {
        if (!(range > 0)) {
            out.push_back(0);
            continue;
        }
        float width = std::fabs(layerDepthUM[gap] - layerDepthUM[gap + 1]);
        out.push_back(std::exp(-width / range));
    }
    return out;
}

std::vector<std::vector<float>> CouplerGeometry::matrix(const std::optional<std::vector<float>>& gapReachScales,
                                                        float selfScale) const {
    const int layers = static_cast<int>(layerDepthUM.size());
    const std::vector<float> scales =
        gapReachScales ? *gapReachScales : std::vector<float>(static_cast<size_t>(layers - 1), 1.0f);

    // Transmission of one gap at its reach setting. Identity short-circuits before `pow`.
    auto transmitted = [&](int gap) -> float {
        float scale = gap < static_cast<int>(scales.size()) ? scales[gap] : 1.0f;
        if (!(scale > 0)) return 0;
        float base = interlayerTransmission[gap];
        if (scale == 1) return base;
        return std::pow(base, 1 / scale);
    };

    std::vector<std::vector<float>> out(layers, std::vector<float>(layers, 0.0f));
    for (int receiver = 0; receiver < layers; ++receiver) {
        for (int donor = 0; donor < layers; ++donor) {
            if (receiver == donor) {
                out[receiver][donor] = release[receiver] * selfRetention * selfScale;
                continue;
            }
            // The inhibitor crosses every interlayer between the two, so the survivals multiply.
            float crossing = 1;
            for (int gap = swiftMin(receiver, donor); gap < swiftMax(receiver, donor); ++gap) {
                crossing = crossing * transmitted(gap);
            }
            out[receiver][donor] = release[donor] * crossing;
        }
    }
    return out;
}

bool CouplerGeometry::isActive() const {
    for (float r : release) {
        if (r != 0) return true;
    }
    return false;
}

CouplerGeometry CouplerGeometry::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& c = json::object(value, path);
    std::optional<std::vector<float>> decodedDepths = json::optionalFloatArray(c, "layerDepthUM", path);
    std::vector<float> depths = decodedDepths ? *decodedDepths : colorNegativeDepthUM();
    std::vector<float> release = json::requiredFloatArray(c, "release", path);
    if (depths.size() != 3) {
        throw DecodingError("dataCorrupted at " + json::join(path, "layerDepthUM") +
                            ": layerDepthUM needs one depth per layer (3), got " + std::to_string(depths.size()));
    }
    if (release.size() != 3) {
        throw DecodingError("dataCorrupted at " + json::join(path, "release") +
                            ": release needs one strength per layer (3), got " + std::to_string(release.size()));
    }
    std::vector<float> transmission = json::requiredFloatArray(c, "interlayerTransmission", path);
    if (transmission.size() != depths.size() - 1) {
        throw DecodingError("dataCorrupted at " + json::join(path, "interlayerTransmission") +
                            ": interlayerTransmission needs two entries, got " +
                            std::to_string(transmission.size()));
    }
    std::optional<float> selfRetention = json::optionalFloat(c, "selfRetention", path);
    return CouplerGeometry(std::move(depths), std::move(transmission), std::move(release),
                           selfRetention ? *selfRetention : houseSelfRetention);
}

CouplerGeometry CouplerGeometry::fit(const std::vector<std::vector<float>>& matrix,
                                     const std::vector<float>& layerDepthUM) {
    bool shaped = matrix.size() == 3;
    for (const auto& row : matrix) shaped = shaped && row.size() == 3;
    preconditionCheck(shaped, "inhibition matrix must be 3x3");

    std::optional<std::pair<float, CouplerGeometry>> best;
    const int steps = 2000;
    const float low = 0.5f, high = 60;
    for (int step = 0; step < steps; ++step) {
        float t = static_cast<float>(step) / static_cast<float>(steps - 1);
        float range = low * std::exp(t * std::log(high / low));

        std::vector<float> release(3, 0.0f);
        for (int donor = 0; donor < 3; ++donor) {
            float numerator = 0, denominator = 0;
            for (int receiver = 0; receiver < 3; ++receiver) {
                if (receiver == donor) continue;
                float w = std::exp(-std::fabs(layerDepthUM[receiver] - layerDepthUM[donor]) / range);
                numerator += w * matrix[receiver][donor];
                denominator += w * w;
            }
            release[donor] = denominator > 0 ? numerator / denominator : 0;
        }

        float numerator = 0, denominator = 0;
        for (int layer = 0; layer < 3; ++layer) {
            numerator += release[layer] * matrix[layer][layer];
            denominator += release[layer] * release[layer];
        }
        float selfRetention = denominator > 0 ? numerator / denominator : 0;

        CouplerGeometry candidate(layerDepthUM, transmission(range, layerDepthUM), release, selfRetention);
        auto produced = candidate.matrix();
        float error = 0;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                float d = produced[i][j] - matrix[i][j];
                error += d * d;
            }
        }
        if (!best || error < best->first) best.emplace(error, candidate);
    }
    return best->second;
}

float CouplerGeometry::residual(const std::vector<std::vector<float>>& matrix) const {
    auto produced = this->matrix();
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float d = produced[i][j] - matrix[i][j];
            sum += d * d;
        }
    }
    return std::sqrt(sum / 9);
}

namespace {

/// Gaussian elimination with partial pivoting on a 3x3.
std::optional<std::vector<float>> solve3x3(std::vector<std::vector<float>> a, std::vector<float> b) {
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row) {
            if (std::fabs(a[row][column]) > std::fabs(a[pivot][column])) pivot = row;
        }
        if (!(std::fabs(a[pivot][column]) > 1e-9f)) return std::nullopt;
        if (pivot != column) {
            std::swap(a[pivot], a[column]);
            std::swap(b[pivot], b[column]);
        }
        for (int row = column + 1; row < 3; ++row) {
            float factor = a[row][column] / a[column][column];
            if (factor == 0) continue;
            for (int k = column; k < 3; ++k) a[row][k] -= factor * a[column][k];
            b[row] -= factor * b[column];
        }
    }
    std::vector<float> x(3, 0.0f);
    for (int row = 2; row >= 0; --row) {
        float sum = b[row];
        for (int k = row + 1; k < 3; ++k) sum -= a[row][k] * x[k];
        x[row] = sum / a[row][row];
    }
    return x;
}

}  // namespace

std::optional<std::vector<float>> CouplerGeometry::releaseSolvedFromInterImage(
    const std::vector<float>& ratios, const std::vector<float>& slopes,
    const std::vector<float>& interlayerTransmission, float selfRetention,
    const std::vector<float>& layerDepthUM) {
    if (!(ratios.size() == 3 && slopes.size() == 3 &&
          interlayerTransmission.size() == layerDepthUM.size() - 1)) {
        return std::nullopt;
    }
    for (float t : interlayerTransmission) {
        if (!(t > 0)) return std::nullopt;
    }
    for (float r : ratios) {
        if (!(r > 1)) return std::nullopt;
    }

    std::vector<std::vector<float>> a(3, std::vector<float>(3, 0.0f));
    std::vector<float> b(3, 0.0f);
    for (int c = 0; c < 3; ++c) {
        for (int j = 0; j < 3; ++j) {
            float w;
            if (c == j) {
                w = selfRetention;
            } else {
                w = 1;
                for (int gap = swiftMin(c, j); gap < swiftMax(c, j); ++gap) w = w * interlayerTransmission[gap];
            }
            a[c][j] = w * slopes[j] * ratios[c];
        }
        a[c][c] -= selfRetention * slopes[c];
        b[c] = ratios[c] - 1;
    }

    auto release = solve3x3(a, b);
    if (!release) return std::nullopt;
    for (float r : *release) {
        if (!(r >= 0)) return std::nullopt;
    }
    return release;
}

}  // namespace fotufilm
