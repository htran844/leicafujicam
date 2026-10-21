// Port of Sources/FotufilmCore/Math.swift.
#include "CoreMath.h"

#include <cfloat>
#include <cmath>

namespace fotufilm {

float softplus(float x) {
    if (x > 20) return x;
    if (x < -20) return std::exp(x);
    return std::log1p(std::exp(x));
}

uint32_t pcgHash(uint32_t v) {
    uint32_t state = v * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28) + 4u)) ^ state) * 277803737u;
    return (word >> 22) ^ word;
}

float triangularDither(uint32_t index, uint32_t channel, uint32_t seed) {
    uint32_t h1 = pcgHash(index ^ pcgHash(channel + (seed * 0x9E3779B9u)));
    uint32_t h2 = pcgHash(h1);
    float u1 = static_cast<float>(h1 >> 8) * (1.0f / 16777216.0f);
    float u2 = static_cast<float>(h2 >> 8) * (1.0f / 16777216.0f);
    return u1 + u2 - 1;
}

float scaledBesselI0(float x) {
    double ax = static_cast<double>(std::fabs(x));
    if (ax < 3.75) {
        double t = (ax / 3.75) * (ax / 3.75);
        double series = 1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492
            + t * (0.2659732 + t * (0.0360768 + t * 0.0045813)))));
        return static_cast<float>(series * std::exp(-ax));
    }
    double t = 3.75 / ax;
    double series = 0.39894228 + t * (0.01328592 + t * (0.00225319
        + t * (-0.00157565 + t * (0.00916281 + t * (-0.02057706
        + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    return static_cast<float>(series / std::sqrt(ax));
}

float scaledBesselI1(float x) {
    double ax = static_cast<double>(std::fabs(x));
    if (ax < 3.75) {
        double t = (ax / 3.75) * (ax / 3.75);
        double series = ax * (0.5 + t * (0.87890594 + t * (0.51498869
            + t * (0.15084934 + t * (0.02658733 + t * (0.00301532
            + t * 0.00032411))))));
        return static_cast<float>(series * std::exp(-ax));
    }
    double t = 3.75 / ax;
    double tail = 0.02282967 + t * (-0.02895312 + t * (0.01787654 - t * 0.00420059));
    double series = 0.39894228 + t * (-0.03988024 + t * (-0.00362018
        + t * (0.00163801 + t * (-0.01031555 + t * tail))));
    return static_cast<float>(series / std::sqrt(ax));
}

uint64_t SplitMix64::next() {
    state_ += 0x9E3779B97F4A7C15ull;
    uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float GaussianGenerator::next() {
    if (cached_) {
        float value = *cached_;
        cached_.reset();
        return value;
    }
    float u1 = static_cast<float>(rng_.next() >> 40) * (1.0f / 16777216.0f) + FLT_MIN;
    float u2 = static_cast<float>(rng_.next() >> 40) * (1.0f / 16777216.0f);
    float r = std::sqrt(-2 * std::log(u1));
    float theta = 2 * kFloatPi * u2;
    cached_ = r * std::sin(theta);
    return r * std::cos(theta);
}

PoissonFieldGenerator::PoissonFieldGenerator(uint64_t seed, float lambda)
    : rng_(seed), gaussian_(seed ^ 0xA5A55A5A12348765ull) {
    lambda_ = swiftMax(lambda, 1e-4f);
    expNegLambda_ = std::exp(-static_cast<double>(lambda_));
    invSqrtLambda_ = 1 / std::sqrt(lambda_);
}

float PoissonFieldGenerator::next() {
    if (lambda_ >= gaussianLimit) {
        return gaussian_.next();
    }
    int k = -1;
    double p = 1.0;
    do {
        k += 1;
        p *= static_cast<double>(rng_.next() >> 11) * 0x1p-53;
    } while (p > expNegLambda_);
    return (static_cast<float>(k) - lambda_) * invSqrtLambda_;
}

}  // namespace fotufilm
