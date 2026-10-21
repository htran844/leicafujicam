// Port of Sources/FotufilmCore/Halation.swift.
#include "Halation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <unordered_map>

#include "Simd.h"

namespace fotufilm {

FilmBase::FilmBase(float thicknessMM_, float refractiveIndex_, std::optional<std::vector<float>> indices)
    : thicknessMM(thicknessMM_), refractiveIndex(refractiveIndex_), recordRefractiveIndices(std::move(indices)) {
    preconditionCheck(!recordRefractiveIndices || recordRefractiveIndices->size() == 3,
                      "recordRefractiveIndices has three entries");
}

FilmBase FilmBase::acetate(float thicknessMM) {
    return FilmBase(thicknessMM, 1.48f, std::vector<float>{1.474f, 1.480f, 1.489f});
}

FilmBase FilmBase::estar(float thicknessMM) {
    return FilmBase(thicknessMM, 1.64f, std::vector<float>{1.635f, 1.643f, 1.657f});
}

float FilmBase::criticalAngle() const { return std::asin(1 / refractiveIndex); }

float FilmBase::haloRadiusMM() const { return 2 * thicknessMM * std::tan(criticalAngle()); }

float FilmBase::refractiveIndexForRecord(int record) const {
    if (!(record >= 0 && record < 3)) return refractiveIndex;
    if (!recordRefractiveIndices) return refractiveIndex;
    preconditionCheck(recordRefractiveIndices->size() == 3, "recordRefractiveIndices has three entries");
    return (*recordRefractiveIndices)[record];
}

HalationProfile HalationProfile::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& o = json::object(value, path);
    HalationProfile profile;
    profile.roundTripOpticalDepth = json::requiredFloatArray(o, "roundTripOpticalDepth", path);
    profile.angularExponent = json::requiredFloatArray(o, "angularExponent", path);
    profile.diffuseShare = json::requiredFloatArray(o, "diffuseShare", path);
    profile.diffuseSigmaMM = json::requiredFloatArray(o, "diffuseSigmaMM", path);
    profile.bounceRetention = json::requiredFloatArray(o, "bounceRetention", path);
    profile.recordDepthMM = json::optionalFloatArray(o, "recordDepthMM", path);
    return profile;
}

namespace Halation {

std::vector<std::vector<float>> mixMatrix(const std::vector<float>& mix,
                                          const std::optional<std::vector<std::vector<float>>>& returnMatrix) {
    std::vector<std::vector<float>> result;
    for (int receiver = 0; receiver < 3; ++receiver) {
        std::optional<std::vector<float>> row;
        if (returnMatrix && returnMatrix->size() == 3 && (*returnMatrix)[receiver].size() == 3) {
            std::vector<float> values;
            for (float v : (*returnMatrix)[receiver]) values.push_back(swiftMax(v, 0.0f));
            row = values;
        }
        float sum = 0;
        if (row) {
            for (float v : *row) sum = sum + v;
        }
        if (!row || !(sum > 0)) {
            std::vector<float> diagonal(3, 0.0f);
            diagonal[receiver] = mix[receiver];
            result.push_back(diagonal);
            continue;
        }
        std::vector<float> shares;
        for (float v : *row) shares.push_back(mix[receiver] * v / sum);
        result.push_back(shares);
    }
    return result;
}

float mix(float returning) {
    float s = swiftMax(returning, 0.0f);
    return s / (1 + s);
}

std::vector<float> hazed(const std::vector<float>& sigmas, float hazeMM) {
    if (!(hazeMM > 0)) return sigmas;
    std::vector<float> result;
    for (float sigma : sigmas) result.push_back(std::sqrt(sigma * sigma + hazeMM * hazeMM));
    return result;
}

Optics::Optics(float index_) {
    float n = swiftMax(index_, 1.001f);
    index = n;
    const int count = angleSamples;
    step = kFloatPi / 2 / static_cast<float>(count);
    cosines.assign(count, 0);
    sines.assign(count, 0);
    reflectancesS.assign(count, 0);
    reflectancesP.assign(count, 0);
    lambertian.assign(count, 0);
    for (int i = 0; i < count; ++i) {
        float theta = (static_cast<float>(i) + 0.5f) * step;
        float c = std::cos(theta), s = std::sin(theta);
        cosines[i] = c;
        sines[i] = s;
        PolarizedReflectance r = polarizedReflectance(c, n);
        reflectancesS[i] = r.s;
        reflectancesP[i] = r.p;
        lambertian[i] = 2 * c * s;
    }
}

std::shared_ptr<const Optics> Optics::shared(float index) {
    static std::mutex& lock = *new std::mutex;
    static auto& cache = *new std::map<float, std::shared_ptr<const Optics>>;
    float key = swiftMax(index, 1.001f);
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
    }
    auto built = std::make_shared<const Optics>(key);
    std::lock_guard<std::mutex> guard(lock);
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    cache[key] = built;
    return built;
}

Profile::Profile(float fraction, const FilmBase& base, float recordDepthMM, const Optics& optics) : base_(base) {
    float t = transmittance(fraction, optics);
    const int count = angleSamples;
    radii_.reserve(bounceCount * count);
    weights_.reserve(bounceCount * count);
    blocks_.push_back(0);
    float layerThickness = base.thicknessMM + swiftMax(recordDepthMM, 0.0f);
    std::vector<float> perTrip(count, 0.0f), surviving(count, 0.0f);
    for (int i = 0; i < count; ++i) {
        perTrip[i] = survival(t, optics.cosines[i]);
        surviving[i] = perTrip[i];
    }
    float total = 0;
    for (int m = 1; m <= bounceCount; ++m) {
        float reach = 2 * static_cast<float>(m) * layerThickness;
        float bounceTotal = 0;
        for (int i = 0; i < count; ++i) {
            float reflectionCount = static_cast<float>(2 * m - 1);
            float rsM = std::pow(optics.reflectancesS[i], reflectionCount);
            float rpM = std::pow(optics.reflectancesP[i], reflectionCount);
            float rear = 0.5f * (rsM + rpM);
            float weight = rear * surviving[i] * optics.lambertian[i];
            bounceTotal += weight;
            radii_.push_back(reach * optics.sines[i] / optics.cosines[i]);
            weights_.push_back(weight);
            surviving[i] *= perTrip[i];
        }
        blocks_.push_back(static_cast<int>(radii_.size()));
        total += bounceTotal;
        if (bounceTotal < total * 1e-3f) break;
    }
    if (total > 0) {
        for (float& w : weights_) w /= total;
    }
}

float Profile::edgeSpread(float distance) const {
    float beyond = 0;
    for (size_t bounce = 0; bounce + 1 < blocks_.size(); ++bounce) {
        int start = blocks_[bounce], end = blocks_[bounce + 1];
        int low = start, high = end;
        while (low < high) {
            int middle = (low + high) / 2;
            if (radii_[middle] > distance) high = middle; else low = middle + 1;
        }
        for (int slot = low; slot < end; ++slot) {
            float weight = weights_[slot];
            if (!(weight > 0)) continue;
            beyond += weight * std::acos(swiftMin(distance / radii_[slot], 1.0f));
        }
    }
    return beyond / kFloatPi;
}

float Profile::anchorSigmaMM() const {
    bool any = false;
    for (float w : weights_) {
        if (w > 0) {
            any = true;
            break;
        }
    }
    if (!any) return 0;
    const float halfWidthToSigma = 1.0f / 0.6744897f;
    float low = 0.1f * base_.haloRadiusMM();
    float high = 30 * base_.haloRadiusMM();
    for (int i = 0; i < 14; ++i) {
        float mid = std::sqrt(low * high);
        if (edgeSpread(mid) > 0.25f) low = mid; else high = mid;
    }
    return std::sqrt(low * high) * halfWidthToSigma;
}

PhysicalProfile::PhysicalProfile(const FilmBase& base, float recordDepthMM, const Optics& optics, float opticalDepth,
                                 float angularExponent, float diffuseShare_, float diffuseSigmaMM_,
                                 float bounceRetention)
    : diffuseShare(diffuseShare_), diffuseSigmaMM(diffuseSigmaMM_) {
    radii_.reserve(bounceCount * angleSamples);
    weights_.reserve(bounceCount * angleSamples);
    float layerThickness = base.thicknessMM + swiftMax(recordDepthMM, 0.0f);
    float total = 0;
    for (int bounce = 1; bounce <= bounceCount; ++bounce) {
        float m = static_cast<float>(bounce);
        float bounceTotal = 0;
        for (int index = 0; index < angleSamples; ++index) {
            float cosine = optics.cosines[index];
            float angular = (angularExponent + 1) * std::pow(cosine, angularExponent) * optics.sines[index];
            float pathExponent = -m * opticalDepth / swiftMax(cosine, 1e-6f);
            float path = pathExponent < -80 ? 0 : std::exp(pathExponent);
            float rsM = std::pow(optics.reflectancesS[index], m);
            float rpM = std::pow(optics.reflectancesP[index], m);
            float rear = 0.5f * (rsM + rpM);
            float repeats = bounce == 1 ? 1.0f : std::pow(bounceRetention, static_cast<float>(bounce - 1));
            float weight = angular * path * rear * repeats;
            bounceTotal += weight;
            total += weight;
            radii_.push_back(2 * m * layerThickness * optics.sines[index] / cosine);
            weights_.push_back(weight);
        }
        if (bounce > 1 && bounceTotal < total * 1e-3f) break;
    }
    if (total > 0) {
        for (float& w : weights_) w /= total;
    }
}

float PhysicalProfile::edgeSpread(float distance) const {
    float reflex = 0;
    for (size_t i = 0; i < radii_.size(); ++i) {
        float radius = radii_[i];
        if (!(radius > distance)) continue;
        reflex += weights_[i] * std::acos(swiftMin(distance / radius, 1.0f)) / kFloatPi;
    }
    float diffuse = gaussianEdgeSpread(distance, diffuseSigmaMM);
    return diffuseShare * diffuse + (1 - diffuseShare) * reflex;
}

PolarizedReflectance polarizedReflectance(float cosTheta, float n) {
    float cosIn = swiftMin(swiftMax(cosTheta, 0.0f), 1.0f);
    float sinIn = std::sqrt(1 - cosIn * cosIn);
    float sinOut = n * sinIn;
    if (!(sinOut < 1)) return PolarizedReflectance{1, 1};
    float cosOut = std::sqrt(1 - sinOut * sinOut);
    float s = (n * cosIn - cosOut) / (n * cosIn + cosOut);
    float p = (n * cosOut - cosIn) / (n * cosOut + cosIn);
    return PolarizedReflectance{s * s, p * p};
}

float reflectance(float cosTheta, float n) {
    PolarizedReflectance r = polarizedReflectance(cosTheta, n);
    return 0.5f * (r.s + r.p);
}

float survival(float t, float cosTheta) {
    float exponent = std::log(swiftMax(t, 1e-30f)) / swiftMax(cosTheta, 1e-6f);
    return exponent < -80 ? 0 : std::exp(exponent);
}

float returnedFraction(float t, const Optics& optics) {
    float total = 0;
    for (int i = 0; i < angleSamples; ++i) {
        float perTrip = survival(t, optics.cosines[i]);
        if (!(perTrip > 0)) continue;
        float survivalSoFar = perTrip;
        float sum = 0;
        for (int m = 1; m <= bounceCount; ++m) {
            float reflectionCount = static_cast<float>(2 * m - 1);
            float rsM = std::pow(optics.reflectancesS[i], reflectionCount);
            float rpM = std::pow(optics.reflectancesP[i], reflectionCount);
            float rear = 0.5f * (rsM + rpM);
            sum += rear * survivalSoFar;
            survivalSoFar *= perTrip;
        }
        total += sum * optics.lambertian[i];
    }
    return total * optics.step;
}

float returnedFraction(float t, float n) { return returnedFraction(t, *Optics::shared(n)); }

float transmittance(float fraction, const Optics& optics) {
    if (!(fraction > 0)) return 0;
    float low = 1e-6f, high = 0.999f;
    if (returnedFraction(high, optics) <= fraction) return high;
    for (int i = 0; i < 28; ++i) {
        float mid = std::sqrt(low * high);
        if (returnedFraction(mid, optics) < fraction) low = mid; else high = mid;
    }
    return std::sqrt(low * high);
}

float transmittance(float fraction, float n) { return transmittance(fraction, *Optics::shared(n)); }

float gaussianEdgeSpread(float distance, float sigma) {
    return static_cast<float>(0.5 * std::erfc(static_cast<double>(distance) /
                                              (static_cast<double>(sigma) * std::sqrt(2.0))));
}

namespace {

std::vector<float> fitDistances(const std::vector<float>& sigmas) {
    const int count = 40;
    double low = static_cast<double>(0.02f * sigmas[0]), high = static_cast<double>(8 * sigmas[2]);
    double step = (std::log(high) - std::log(low)) / static_cast<double>(count - 1);
    std::vector<float> result{0.0f};
    for (int i = 0; i < count; ++i) {
        result.push_back(static_cast<float>(std::exp(std::log(low) + static_cast<double>(i) * step)));
    }
    return result;
}

std::vector<float> physicalFitDistances(float radius) {
    std::vector<float> result;
    for (int i = 0; i <= 160; ++i) result.push_back(static_cast<float>(i) / 160 * 4 * radius);
    for (int step = 1; step <= 48; ++step) {
        float t = static_cast<float>(step) / 48;
        result.push_back(4 * radius * std::exp(t * std::log(4.0f)));
    }
    return result;
}

std::optional<std::vector<float>> solveSymmetric(std::vector<float> a, std::vector<float> b, int size) {
    for (int pivot = 0; pivot < size; ++pivot) {
        int row = pivot;
        for (int candidate = pivot + 1; candidate < size; ++candidate) {
            if (std::fabs(a[candidate * size + pivot]) > std::fabs(a[row * size + pivot])) row = candidate;
        }
        if (!(std::fabs(a[row * size + pivot]) > 1e-20f)) return std::nullopt;
        if (row != pivot) {
            for (int column = 0; column < size; ++column) {
                std::swap(a[row * size + column], a[pivot * size + column]);
            }
            std::swap(b[row], b[pivot]);
        }
        for (int below = pivot + 1; below < size; ++below) {
            float factor = a[below * size + pivot] / a[pivot * size + pivot];
            if (factor == 0) continue;
            for (int column = pivot; column < size; ++column) {
                a[below * size + column] -= factor * a[pivot * size + column];
            }
            b[below] -= factor * b[pivot];
        }
    }
    std::vector<float> solution(size, 0.0f);
    for (int row = size - 1; row >= 0; --row) {
        float value = b[row];
        for (int column = row + 1; column < size; ++column) value -= a[row * size + column] * solution[column];
        solution[row] = value / a[row * size + row];
    }
    return solution;
}

std::optional<std::vector<float>> solveOnFace(const std::vector<std::vector<float>>& basis,
                                              const std::vector<float>& target, const std::vector<int>& active,
                                              int count) {
    std::vector<float> weights(count, 0.0f);
    int last = active.back();
    std::vector<int> free(active.begin(), active.end() - 1);
    if (free.empty()) {
        weights[last] = 1;
        return weights;
    }
    int size = static_cast<int>(free.size());
    std::vector<float> normal(size * size, 0.0f);
    std::vector<float> rhs(size, 0.0f);
    for (int row = 0; row < size; ++row) {
        int j = free[row];
        for (int column = 0; column < size; ++column) {
            int k = free[column];
            float sum = 0;
            for (size_t index = 0; index < target.size(); ++index) {
                sum += (basis[j][index] - basis[last][index]) * (basis[k][index] - basis[last][index]);
            }
            normal[row * size + column] = sum;
        }
        float sum = 0;
        for (size_t index = 0; index < target.size(); ++index) {
            sum += (basis[j][index] - basis[last][index]) * (target[index] - basis[last][index]);
        }
        rhs[row] = sum;
    }
    std::optional<std::vector<float>> solution = solveSymmetric(normal, rhs, size);
    if (!solution) return std::nullopt;
    float used = 0;
    for (int row = 0; row < size; ++row) {
        int j = free[row];
        if (!((*solution)[row] >= -1e-6f)) return std::nullopt;
        weights[j] = swiftMax((*solution)[row], 0.0f);
        used += weights[j];
    }
    if (!(used <= 1 + 1e-6f)) return std::nullopt;
    weights[last] = 1 - used;
    return weights;
}

struct GaussianFit {
    std::vector<float> sigmas;
    std::vector<std::vector<float>> weights;
};

GaussianFit continuousGaussianFit(const std::vector<PhysicalProfile>& profiles, float radius) {
    std::vector<float> distances = physicalFitDistances(radius);
    std::vector<std::vector<float>> targets;
    for (const PhysicalProfile& profile : profiles) {
        std::vector<float> row;
        for (float d : distances) row.push_back(profile.edgeSpread(d));
        targets.push_back(row);
    }
    const int logarithmicCandidateCount = 14;
    float low = std::log(0.02f), high = std::log(4.0f);
    std::vector<float> candidates;
    for (int index = 0; index < logarithmicCandidateCount; ++index) {
        float t = static_cast<float>(index) / static_cast<float>(logarithmicCandidateCount - 1);
        candidates.push_back(radius * std::exp(low + t * (high - low)));
    }
    for (const PhysicalProfile& profile : profiles) {
        if (profile.diffuseShare > 0) candidates.push_back(profile.diffuseSigmaMM);
    }
    std::sort(candidates.begin(), candidates.end());
    std::vector<float> unique;
    for (float candidate : candidates) {
        if (unique.empty() || std::fabs(unique.back() - candidate) > 1e-6f * radius) unique.push_back(candidate);
    }
    candidates = unique;
    int candidateCount = static_cast<int>(candidates.size());
    std::vector<std::vector<float>> candidateBasis;
    for (float sigma : candidates) {
        std::vector<float> row;
        for (float d : distances) row.push_back(gaussianEdgeSpread(d, sigma));
        candidateBasis.push_back(row);
    }
    float bestResidual = std::numeric_limits<float>::infinity();
    std::vector<float> bestSigmas(candidates.begin(), candidates.begin() + std::min(scaleCount, candidateCount));
    std::vector<std::vector<float>> bestWeights(profiles.size(), std::vector<float>(scaleCount, 0.0f));
    for (int first = 0; first < candidateCount - 2; ++first) {
        for (int second = first + 1; second < candidateCount - 1; ++second) {
            for (int third = second + 1; third < candidateCount; ++third) {
                std::vector<std::vector<float>> basis = {candidateBasis[first], candidateBasis[second],
                                                         candidateBasis[third]};
                std::vector<std::vector<float>> weights;
                for (const auto& target : targets) weights.push_back(simplexFit(basis, target));
                float residual = 0;
                for (size_t p = 0; p < targets.size(); ++p) {
                    const auto& target = targets[p];
                    const auto& row = weights[p];
                    for (size_t sample = 0; sample < target.size(); ++sample) {
                        float fitted = 0;
                        for (int scale = 0; scale < scaleCount; ++scale) fitted += row[scale] * basis[scale][sample];
                        float error = fitted - target[sample];
                        residual += error * error;
                    }
                }
                if (residual < bestResidual) {
                    bestResidual = residual;
                    bestSigmas = {candidates[first], candidates[second], candidates[third]};
                    bestWeights = weights;
                }
            }
        }
    }
    return GaussianFit{bestSigmas, bestWeights};
}

}  // namespace

std::vector<float> simplexFit(const std::vector<std::vector<float>>& basis, const std::vector<float>& target) {
    int count = static_cast<int>(basis.size());
    std::vector<float> best(count, 0.0f);
    float bestResidual = std::numeric_limits<float>::infinity();
    for (int mask = 1; mask < (1 << count); ++mask) {
        std::vector<int> active;
        for (int i = 0; i < count; ++i) {
            if (mask & (1 << i)) active.push_back(i);
        }
        std::optional<std::vector<float>> candidate = solveOnFace(basis, target, active, count);
        if (!candidate) continue;
        float residual = 0;
        for (size_t index = 0; index < target.size(); ++index) {
            float value = 0;
            for (int column = 0; column < count; ++column) value += (*candidate)[column] * basis[column][index];
            residual += (value - target[index]) * (value - target[index]);
        }
        if (residual < bestResidual) {
            bestResidual = residual;
            best = *candidate;
        }
    }
    return best;
}

Kernel kernel(const std::vector<float>& returned, const FilmBase& base, float hazeMM,
              const std::optional<std::vector<std::vector<float>>>& returnMatrix) {
    preconditionCheck(returned.size() == 3, "halation kernel takes three returned fractions");
    std::vector<Profile> profiles;
    for (int layer = 0; layer < 3; ++layer) {
        std::shared_ptr<const Optics> optics = Optics::shared(base.refractiveIndexForRecord(layer));
        profiles.emplace_back(returned[layer], base, 0.0f, *optics);
    }
    std::vector<float> anchors;
    for (const Profile& profile : profiles) anchors.push_back(profile.anchorSigmaMM());
    float anchor = swiftMax(maxOr(anchors, 0.0f), 1e-6f);
    std::vector<float> sigmas = {anchor / scaleRatio, anchor, anchor * scaleRatio};
    std::vector<float> distances = fitDistances(sigmas);
    std::vector<std::vector<float>> basis;
    for (float sigma : sigmas) {
        std::vector<float> row;
        for (float d : distances) row.push_back(gaussianEdgeSpread(d, sigma));
        basis.push_back(row);
    }
    std::vector<std::vector<float>> weights;
    for (const Profile& profile : profiles) {
        std::vector<float> target;
        for (float d : distances) target.push_back(profile.edgeSpread(d));
        weights.push_back(simplexFit(basis, target));
    }
    std::vector<float> mixes;
    for (float r : returned) mixes.push_back(mix(r));
    return Kernel{hazed(sigmas, hazeMM), {0, 0, 0}, weights, mixes, mixMatrix(mixes, returnMatrix)};
}

Kernel kernel(const std::vector<float>& returned, const FilmBase& base, const std::optional<HalationProfile>& profile,
              float hazeMM, const std::optional<std::vector<std::vector<float>>>& returnMatrix) {
    if (!profile) return kernel(returned, base, hazeMM, returnMatrix);
    preconditionCheck(returned.size() == 3, "halation kernel takes three returned fractions");
    preconditionCheck(profile->roundTripOpticalDepth.size() == 3, "roundTripOpticalDepth has three entries");
    preconditionCheck(profile->angularExponent.size() == 3, "angularExponent has three entries");
    preconditionCheck(profile->diffuseShare.size() == 3, "diffuseShare has three entries");
    preconditionCheck(profile->diffuseSigmaMM.size() == 3, "diffuseSigmaMM has three entries");
    preconditionCheck(profile->bounceRetention.size() == 3, "bounceRetention has three entries");
    preconditionCheck(!profile->recordDepthMM || profile->recordDepthMM->size() == 3,
                      "recordDepthMM has three entries");
    std::vector<PhysicalProfile> profiles;
    for (int layer = 0; layer < 3; ++layer) {
        std::shared_ptr<const Optics> optics = Optics::shared(base.refractiveIndexForRecord(layer));
        profiles.emplace_back(base, profile->recordDepthMM ? (*profile->recordDepthMM)[layer] : 0.0f, *optics,
                              swiftMax(profile->roundTripOpticalDepth[layer], 0.0f),
                              swiftMax(profile->angularExponent[layer], 0.0f),
                              swiftMin(swiftMax(profile->diffuseShare[layer], 0.0f), 1.0f),
                              swiftMax(profile->diffuseSigmaMM[layer], 1e-6f),
                              swiftMin(swiftMax(profile->bounceRetention[layer], 0.0f), 1.0f));
    }
    std::vector<float> critical;
    for (int layer = 0; layer < 3; ++layer) {
        float index = swiftMax(base.refractiveIndexForRecord(layer), 1.001f);
        float angle = std::asin(1 / index);
        float depth = swiftMax(profile->recordDepthMM ? (*profile->recordDepthMM)[layer] : 0.0f, 0.0f);
        critical.push_back(2 * (base.thicknessMM + depth) * std::tan(angle));
    }
    float criticalRadius = maxOr(critical, base.haloRadiusMM());
    GaussianFit fit = continuousGaussianFit(profiles, swiftMax(criticalRadius, 1e-6f));
    std::vector<float> mixes;
    for (float r : returned) mixes.push_back(mix(r));
    return Kernel{hazed(fit.sigmas, hazeMM), {0, 0, 0}, fit.weights, mixes, mixMatrix(mixes, returnMatrix)};
}

}  // namespace Halation

namespace HalationRuntime {

Halation::Kernel kernel(const std::vector<float>& returned, const FilmBase& base,
                        const std::optional<HalationProfile>& profile, float hazeMM,
                        const std::optional<std::vector<std::vector<float>>>& returnMatrix) {
    uint64_t key = 0xcbf29ce484222325ull;
    auto add = [&key](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        key = (key ^ static_cast<uint64_t>(bits)) * 0x100000001b3ull;
    };
    for (float v : returned) add(v);
    add(base.thicknessMM);
    add(base.refractiveIndex);
    if (base.recordRefractiveIndices) {
        add(1);
        for (float v : *base.recordRefractiveIndices) add(v);
    } else {
        add(0);
    }
    add(hazeMM);
    if (profile) {
        add(1);
        for (float v : profile->roundTripOpticalDepth) add(v);
        for (float v : profile->angularExponent) add(v);
        for (float v : profile->diffuseShare) add(v);
        for (float v : profile->diffuseSigmaMM) add(v);
        for (float v : profile->bounceRetention) add(v);
        if (profile->recordDepthMM) {
            add(1);
            for (float v : *profile->recordDepthMM) add(v);
        } else {
            add(0);
        }
    } else {
        add(0);
    }
    if (returnMatrix) {
        add(1);
        for (const auto& row : *returnMatrix) {
            for (float v : row) add(v);
        }
    } else {
        add(0);
    }

    static std::mutex& lock = *new std::mutex;
    static auto& cache = *new std::unordered_map<uint64_t, Halation::Kernel>;
    const size_t cacheLimit = 64;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
    }
    Halation::Kernel built = Halation::kernel(returned, base, profile, hazeMM, returnMatrix);
    std::lock_guard<std::mutex> guard(lock);
    auto found = cache.find(key);
    Halation::Kernel result = found != cache.end() ? found->second : built;
    if (cache.size() >= cacheLimit) cache.clear();
    cache[key] = result;
    return result;
}

}  // namespace HalationRuntime

}  // namespace fotufilm
