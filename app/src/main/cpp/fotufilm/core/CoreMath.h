// Port of Sources/FotufilmCore/Math.swift (named CoreMath so it cannot shadow <math.h> on
// case-insensitive file systems). AndroidMath.swift needs no counterpart: the C++ <cmath> float
// overloads are the single-precision functions it wraps.
#ifndef FOTUFILM_CORE_CORE_MATH_H
#define FOTUFILM_CORE_CORE_MATH_H

#include <cstdint>
#include <optional>

#include "Simd.h"

namespace fotufilm {

float softplus(float x);

uint32_t pcgHash(uint32_t v);

/// Triangular-PDF dither spanning ±1 quantizer step.
float triangularDither(uint32_t index, uint32_t channel, uint32_t seed);

/// Modified Bessel function I0, scaled by exp(-x). Abramowitz & Stegun 9.8.1 and 9.8.2.
float scaledBesselI0(float x);
/// Modified Bessel function I1, scaled by exp(-x). Abramowitz & Stegun 9.8.3 and 9.8.4.
float scaledBesselI1(float x);

/// SplitMix64 generator so grain is reproducible for a given seed.
class SplitMix64 {
public:
    explicit SplitMix64(uint64_t seed) : state_(seed) {}
    uint64_t next();

private:
    uint64_t state_;
};

/// Box–Muller transform producing standard-normal samples.
class GaussianGenerator {
public:
    explicit GaussianGenerator(uint64_t seed) : rng_(seed) {}
    float next();

private:
    SplitMix64 rng_;
    std::optional<float> cached_;
};

/// Samples of the centered, unit-variance Poisson grain field: (N - lambda) / sqrt(lambda).
class PoissonFieldGenerator {
public:
    /// Above this, skewness is negligible and the Gaussian limit is used.
    static constexpr float gaussianLimit = 32;

    PoissonFieldGenerator(uint64_t seed, float lambda);
    float next();
    float lambda() const { return lambda_; }

private:
    SplitMix64 rng_;
    GaussianGenerator gaussian_;
    float lambda_;
    double expNegLambda_;
    float invSqrtLambda_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_CORE_MATH_H
