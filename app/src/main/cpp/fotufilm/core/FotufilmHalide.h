// Port of Sources/FotufilmHalide/include/FotufilmHalide.h: the frame feature bits and the host-side
// stride/reach helpers the core reads (`fotufilm_halation_box_sigma`, `fotufilm_halation_stride`,
// `fotufilm_gaussian_grid_reach`, `fotufilm_diffusion_stride`, `fotufilm_halation_strided_radius`).
// The Halide entry points are not ported; the GLES graph replaces them.
#ifndef FOTUFILM_CORE_FOTUFILM_HALIDE_H
#define FOTUFILM_CORE_FOTUFILM_HALIDE_H

#include <cmath>
#include <cstdint>

#include "FotufilmConfigLayout.h"

/// Feature mask selecting which physical stages a pipeline variant includes.
enum {
    FOTUFILM_FRAME_FLARE = 1 << 0,
    FOTUFILM_FRAME_MTF = 1 << 1,
    FOTUFILM_FRAME_HALATION = 1 << 2,
    FOTUFILM_FRAME_COUPLERS = 1 << 3,
    FOTUFILM_FRAME_ADJACENCY = 1 << 4,
    FOTUFILM_FRAME_GRAIN = 1 << 5,
    FOTUFILM_FRAME_REVERSAL = 1 << 6,
    FOTUFILM_FRAME_MONOCHROME = 1 << 7,
    FOTUFILM_FRAME_FLOAT_IO = 1 << 8,
    FOTUFILM_FRAME_MTF_LUMA = 1 << 9,
    FOTUFILM_FRAME_COUPLER_DIFFUSION = 1 << 10,
    FOTUFILM_FRAME_REALTIME = 1 << 11,
    FOTUFILM_FRAME_EXACT_MATH = 1 << 12,
    FOTUFILM_FRAME_DENSITY_OUT = 1 << 13,
    FOTUFILM_FRAME_DENSITY_IN = 1 << 14,
    FOTUFILM_FRAME_GRAIN_MOTTLE = 1 << 16,
    FOTUFILM_FRAME_PRINT_MTF = 1 << 17,
    FOTUFILM_FRAME_FLARE_MEASURE = 1 << 18,
    FOTUFILM_FRAME_ENCODE_OUT = 1 << 19,
    FOTUFILM_FRAME_LIGHT_OUT = 1 << 20,
    FOTUFILM_FRAME_FIELDS_IN = 1 << 21,
    FOTUFILM_FRAME_OUTPUT_LINEAR = 1 << 22,
    FOTUFILM_FRAME_OUTPUT_POWER = 1 << 23,
    FOTUFILM_FRAME_OUTPUT_LOG = 1 << 24,
    FOTUFILM_FRAME_TEXTURE = 1 << 25,
    FOTUFILM_FRAME_DIFFUSION = 1 << 26,
    FOTUFILM_FRAME_DONOR_LAYER = 1 << 27,
    FOTUFILM_FRAME_HALATION_ANNULAR = 1 << 28,
    FOTUFILM_FRAME_RECORD_EXPOSURE_IN = 1 << 30,
    FOTUFILM_FRAME_NO_FILM = 1 << 29,
};

/// Sigma of the Gaussian that three iterated box blurs of `radius` approximate.
static inline float fotufilm_halation_box_sigma(int32_t radius) {
    const float width = (float)(2 * radius + 1);
    return sqrtf((width * width - 1.0f) * 0.25f);
}

/// Decimation stride for one halation scale.
static inline int32_t fotufilm_halation_stride(int32_t radius) {
    const float sigma = fotufilm_halation_box_sigma(radius);
    int32_t stride = 1;
    while (stride < 8 && (float)(2 * stride) * 2.5f <= sigma) stride *= 2;
    return stride;
}

/// How far a decimated Gaussian of `sigma` (`radius` taps at full resolution) really reads.
static inline int32_t fotufilm_gaussian_grid_reach(float sigma, int32_t radius) {
    if (radius <= 0) return 0;
    const int32_t stride = sigma >= 8.0f ? 8 : sigma >= 4.0f ? 4 : sigma >= 2.0f ? 2 : 1;
    int32_t grid_radius = (radius + stride - 1) / stride;
    if (grid_radius < 1) grid_radius = 1;
    return stride == 1 ? radius + 1 : stride * (grid_radius + 2);
}

/// Diffusion decimation with a stride ceiling of 64.
static inline int32_t fotufilm_diffusion_stride(int32_t radius) {
    const float sigma = fotufilm_halation_box_sigma(radius);
    int32_t stride = 1;
    while (stride < 64 && (float)(2 * stride) * 2.5f <= sigma) stride *= 2;
    return stride;
}

/// Box radius on the decimated grid whose three-pass chain matches the full-resolution sigma.
static inline int32_t fotufilm_halation_strided_radius(int32_t radius, int32_t stride) {
    if (stride <= 1) return radius;
    const float sigma = fotufilm_halation_box_sigma(radius);
    float variance = sigma * sigma / (float)(stride * stride) - 0.25f;
    if (variance < 0.25f) variance = 0.25f;
    const float width = sqrtf(4.0f * variance + 1.0f);
    const int32_t scaled = (int32_t)((width - 1.0f) * 0.5f + 0.5f);
    return scaled < 1 ? 1 : scaled;
}

#endif  // FOTUFILM_CORE_FOTUFILM_HALIDE_H
