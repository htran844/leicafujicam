// Port of Sources/FotufilmCore/CharacteristicCurve.swift.
#include "CharacteristicCurve.h"

#include "CoreMath.h"
#include "Simd.h"

namespace fotufilm {

CharacteristicCurveComponent::CharacteristicCurveComponent(float gamma_, float toe_, float toeWidth_,
                                                           float shoulder_, float shoulderWidth_)
    : gamma(gamma_), toe(toe_), toeWidth(toeWidth_), shoulder(shoulder_), shoulderWidth(shoulderWidth_) {
    preconditionCheck(shoulder_ > toe_, "secondary shoulder must sit above the toe on the log-exposure axis");
}

CharacteristicCurveComponent CharacteristicCurveComponent::reexposed(float offset) const {
    return CharacteristicCurveComponent(gamma, toe - offset, toeWidth, shoulder - offset, shoulderWidth);
}

float CharacteristicCurveComponent::density(float x) const {
    float t = toeWidth * softplus((x - toe) / toeWidth);
    float s = shoulderWidth * softplus((x - shoulder) / shoulderWidth);
    return gamma * swiftMin(swiftMax(t - s, 0.0f), shoulder - toe);
}

CharacteristicCurve::CharacteristicCurve(float dMin_, float gamma_, float toe_, float toeWidth_, float shoulder_,
                                         float shoulderWidth_, std::optional<CharacteristicCurveComponent> secondary_,
                                         std::optional<SampledCharacteristicCurve> sampled_)
    : dMin(dMin_),
      gamma(gamma_),
      toe(toe_),
      toeWidth(toeWidth_),
      shoulder(shoulder_),
      shoulderWidth(shoulderWidth_),
      secondary(std::move(secondary_)),
      sampled(std::move(sampled_)) {
    preconditionCheck(shoulder_ > toe_, "shoulder must sit above the toe on the log-exposure axis");
}

float CharacteristicCurve::dMax() const {
    if (sampled) {
        float out;
        sequenceMax(sampled->density, out);
        return out;
    }
    return dMin + gamma * (shoulder - toe) + (secondary ? secondary->densityRange() : 0.0f);
}

float CharacteristicCurve::density(float x) const {
    if (sampled) return sampled->value(x);
    float t = toeWidth * softplus((x - toe) / toeWidth);
    float s = shoulderWidth * softplus((x - shoulder) / shoulderWidth);
    return dMin + gamma * swiftMin(swiftMax(t - s, 0.0f), shoulder - toe) +
           (secondary ? secondary->density(x) : 0.0f);
}

CharacteristicCurve CharacteristicCurve::reexposed(float offset) const {
    if (offset == 0) return *this;
    std::optional<CharacteristicCurveComponent> shiftedSecondary;
    if (secondary) shiftedSecondary = secondary->reexposed(offset);
    std::optional<SampledCharacteristicCurve> shiftedSampled;
    if (sampled) shiftedSampled = sampled->reexposed(offset);
    return CharacteristicCurve(dMin, gamma, toe - offset, toeWidth, shoulder - offset, shoulderWidth,
                               shiftedSecondary, shiftedSampled);
}

float CharacteristicCurve::logExposure(float target) const {
    float lo = sampled ? sampled->logExposure.front() : (toe - 6);
    float hi = sampled ? sampled->logExposure.back() : (shoulder + 6);
    if (sampled && target > sampled->density.back()) {
        // A lower final sample does not bracket densities near an interior peak. Find the first
        // rising crossing, or clamp to the actual peak when the density is unattainable.
        const auto& d = sampled->density;
        size_t upper = d.size();
        for (size_t i = 0; i < d.size(); ++i) {
            if (d[i] >= target) {
                upper = i;
                break;
            }
        }
        if (upper == d.size()) {
            size_t peak = 0;
            for (size_t i = 1; i < d.size(); ++i) {
                if (d[peak] < d[i]) peak = i;
            }
            return sampled->logExposure[peak];
        }
        if (!(upper > 0)) return sampled->logExposure[0];
        lo = sampled->logExposure[upper - 1];
        hi = sampled->logExposure[upper];
    }
    for (int i = 0; i < 60; ++i) {
        float mid = (lo + hi) / 2;
        if (density(mid) < target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (lo + hi) / 2;
}

}  // namespace fotufilm
