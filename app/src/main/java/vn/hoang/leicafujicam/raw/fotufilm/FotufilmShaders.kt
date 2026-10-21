package vn.hoang.leicafujicam.raw.fotufilm

import vn.hoang.leicafujicam.raw.fotufilm.FotufilmConfigLayout as L

/**
 * GLSL ES port of the upstream `Sources/FotufilmHalide/Stages` headers, expression for expression, and the
 * pass kernels `FotufilmRenderer` runs over them.
 *
 * Conventions shared by every source here:
 *  - `cfg(i)` reads configuration slot `i` from an R32F texture 1024 floats wide.
 *  - Every texture is read with `texelFetch` through a `highp` sampler; intermediate stores are
 *    RGBA32F, so a store is the exact float the producing expression computed.
 *  - Halide `select` evaluates both arms; it is written as a GLSL conditional, which yields the
 *    same value (the discarded arm's NaN/Inf never reaches the result either way).
 *  - Integer division/modulo on possibly negative coordinates uses Halide's floor semantics.
 */
internal object FotufilmShaders {
    private val DEFINES: String = buildString {
        val values = listOf(
            "CFG_CURVES" to L.CONFIG_CURVES,
            "CFG_COUPLER" to L.CONFIG_COUPLER,
            "CFG_GRAIN" to L.CONFIG_GRAIN,
            "CFG_PAPER" to L.CONFIG_PAPER,
            "CFG_MASKING" to L.CONFIG_MASKING,
            "CFG_FLARE" to L.CONFIG_FLARE,
            "CFG_COUPLER_SCALE" to L.CONFIG_COUPLER_SCALE,
            "CFG_ADJACENCY_STRENGTH" to L.CONFIG_ADJACENCY_STRENGTH,
            "CFG_EXPOSURE_GAIN" to L.CONFIG_EXPOSURE_GAIN,
            "CFG_PAPER_MIDPOINT" to L.CONFIG_PAPER_MIDPOINT,
            "CFG_FLARE_MEAN" to L.CONFIG_FLARE_MEAN,
            "CFG_COUPLER_WARP" to L.CONFIG_COUPLER_WARP,
            "CFG_WHITE_BALANCE" to L.CONFIG_WHITE_BALANCE,
            "CFG_HIGHLIGHTS" to L.CONFIG_HIGHLIGHTS,
            "CFG_SHADOWS" to L.CONFIG_SHADOWS,
            "CFG_SATURATION" to L.CONFIG_SATURATION,
            "CFG_VIBRANCE" to L.CONFIG_VIBRANCE,
            "CFG_HALATION_KERNEL" to L.CONFIG_HALATION_KERNEL,
            "CFG_GRAIN_CORRELATION" to L.CONFIG_GRAIN_CORRELATION,
            "CFG_MTF_LUMA_SHARE" to L.CONFIG_MTF_LUMA_SHARE,
            "CFG_GRADE_LIFT" to L.CONFIG_GRADE_LIFT,
            "CFG_GRADE_GAIN" to L.CONFIG_GRADE_GAIN,
            "CFG_GRADE_INV_GAMMA" to L.CONFIG_GRADE_INV_GAMMA,
            "CFG_FRAME_WIDTH" to L.CONFIG_FRAME_WIDTH,
            "CFG_FRAME_HEIGHT" to L.CONFIG_FRAME_HEIGHT,
            "CFG_TONE_GRID_WIDTH" to L.CONFIG_TONE_GRID_WIDTH,
            "CFG_TONE_GRID_HEIGHT" to L.CONFIG_TONE_GRID_HEIGHT,
            "CFG_TONE_GRID_A" to L.CONFIG_TONE_GRID_A,
            "CFG_TONE_GRID_B" to L.CONFIG_TONE_GRID_B,
            "CFG_GRADE_SPACE" to L.CONFIG_GRADE_SPACE,
            "CFG_MOTTLE" to L.CONFIG_MOTTLE,
            "CFG_PAPER_RED" to L.CONFIG_PAPER_RED,
            "CFG_PAPER_BLUE" to L.CONFIG_PAPER_BLUE,
            "CFG_PAPER_MIDPOINT_RED" to L.CONFIG_PAPER_MIDPOINT_RED,
            "CFG_PAPER_MIDPOINT_BLUE" to L.CONFIG_PAPER_MIDPOINT_BLUE,
            "CFG_GRAIN_LAW" to L.CONFIG_GRAIN_LAW,
            "CFG_GRAIN_ANCHOR" to L.CONFIG_GRAIN_ANCHOR,
            "CFG_GRAIN_FOG" to L.CONFIG_GRAIN_FOG,
            "CFG_OUTPUT_MATRIX" to L.CONFIG_OUTPUT_MATRIX,
            "CFG_OUTPUT_TRANSFER" to L.CONFIG_OUTPUT_TRANSFER,
            "CFG_OUTPUT_COEFFICIENTS" to L.CONFIG_OUTPUT_COEFFICIENTS,
            "CFG_OUTPUT_PREMULTIPLIED" to L.CONFIG_OUTPUT_PREMULTIPLIED,
            "CFG_PRINT_SHARPEN" to L.CONFIG_PRINT_SHARPEN,
            "CFG_DIFFUSION_DIRECT" to L.CONFIG_DIFFUSION_DIRECT,
            "CFG_DIFFUSION_KERNEL" to L.CONFIG_DIFFUSION_KERNEL,
            "CFG_DONOR_CURVE" to L.CONFIG_DONOR_CURVE,
            "CFG_DONOR_RELEASE" to L.CONFIG_DONOR_RELEASE,
            "CFG_HALATION_RING_RADIUS" to L.CONFIG_HALATION_RING_RADIUS,
            "CFG_HALATION_MATRIX" to L.CONFIG_HALATION_MATRIX,
            "CFG_CURVE_SECONDARY" to L.CONFIG_CURVE_SECONDARY,
            "CFG_MTF_PRIMARY_SHARE" to L.CONFIG_MTF_PRIMARY_SHARE,
            "CFG_COUPLER_RELEASE_GAMMA" to L.CONFIG_COUPLER_RELEASE_GAMMA,
            "CFG_DONOR_RELEASE_GAMMA" to L.CONFIG_DONOR_RELEASE_GAMMA,
            "CFG_DONOR_DIFFUSION_KERNEL" to L.CONFIG_DONOR_DIFFUSION_KERNEL,
            "CFG_DEVELOP_COMPLEMENT" to L.CONFIG_DEVELOP_COMPLEMENT,
            "CFG_OUTPUT_SHOULDER" to L.CONFIG_OUTPUT_SHOULDER,
            "CFG_OUTPUT_GAMUT" to L.CONFIG_OUTPUT_GAMUT,
            "CFG_SAMPLED_CURVES" to L.CONFIG_SAMPLED_CURVES,
            "CFG_ADJACENCY_MODEL" to L.CONFIG_ADJACENCY_MODEL,
            "CFG_CHROMATIC_FRINGE_AMOUNT" to L.CONFIG_CHROMATIC_FRINGE_AMOUNT,
            "CFG_RECORD_INPUT" to L.CONFIG_RECORD_INPUT,
            "CFG_GRAIN_REVERSAL_PROFILE" to L.CONFIG_GRAIN_REVERSAL_PROFILE,
            "CFG_CAMERA_PREFLASH" to L.CONFIG_CAMERA_PREFLASH,
            "CFG_PRINTER_PREFLASH" to L.CONFIG_PRINTER_PREFLASH,
            "CFG_GRAIN_DENSITY_RECORDS" to L.CONFIG_GRAIN_DENSITY_RECORDS,
            "CFG_FILM_TILE" to L.CONFIG_FILM_TILE,
            "CFG_GATE" to L.CONFIG_GATE,
            "CFG_HDR_RANGE" to L.CONFIG_HDR_RANGE,
            "CFG_GATE_CORNER" to L.CONFIG_GATE_CORNER,
            "CFG_CARRIER" to L.CONFIG_CARRIER,
            "CFG_PRINT_FINISH" to L.CONFIG_PRINT_FINISH,
            "COUPLER_WARP_SAMPLES" to L.COUPLER_WARP_SAMPLES,
            "SAMPLED_CURVE_MAX_SAMPLES" to L.SAMPLED_CURVE_MAX_SAMPLES,
            "SAMPLED_CURVE_STRIDE" to L.SAMPLED_CURVE_STRIDE,
            "FILM_TILE_SIDE" to L.FILM_TILE_SIDE,
            "FILM_TILE_LEVELS" to L.FILM_TILE_LEVELS,
            "FILM_TILE_BLOCK" to L.FILM_TILE_BLOCK,
            "FILM_TILE_MARGIN" to L.FILM_TILE_MARGIN,
            "FILM_TILE_SEAM" to L.FILM_TILE_SEAM,
            "FILM_TILE_LAYERS" to L.FILM_TILE_LAYERS,
            "FILM_TILE_STREAM" to L.FILM_TILE_STREAM,
            "FILM_TILE_PITCH" to L.FILM_TILE_PITCH,
            "FILM_TILE_FOOTPRINT" to L.FILM_TILE_FOOTPRINT,
            "FILM_TILE_AMOUNT" to L.FILM_TILE_AMOUNT,
            "FILM_TILE_COLOUR" to L.FILM_TILE_COLOUR,
            "FILM_TILE_DMIN" to L.FILM_TILE_DMIN,
            "FILM_TILE_DMAX" to L.FILM_TILE_DMAX,
            "FILM_TILE_TABLES" to L.FILM_TILE_TABLES,
        )
        for ((name, value) in values) append("#define ").append(name).append(' ').append(value).append('\n')
    }

    private const val COMPUTE_PRECISION = """precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
precision highp sampler2DArray;
precision highp image2D;
"""

    private const val FRAGMENT_PRECISION = """precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
"""

    /** Configuration reads, Halide integer semantics, Math.h and Random.h. */
    private const val CORE = """
uniform highp sampler2D uConfig;

float cfg(int i) { return texelFetch(uConfig, ivec2(i & 1023, i >> 10), 0).r; }

// Halide integer division (floor) for a positive divisor.
int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// Halide integer modulo (non-negative) for a positive divisor.
int floor_mod(int a, int b) { return a - floor_div(a, b) * b; }

// Halide::log(10.0f), folded to float.
const float LN10 = 2.30258512;

// fs_log10
float fs_log10(float value) { return log(value) / LN10; }

// fs_pow10
float fs_pow10(float value) { return pow(10.0, value); }

// softplus
float softplus(float value) {
    return value > 20.0 ? value : (value < -20.0 ? exp(value) : log(1.0 + exp(value)));
}

// display_shoulder
float display_shoulder(float x, float knee) {
    float room = 1.0 - knee;
    float over = x - knee;
    float rolled = knee + room * over / max(over + room, 1.0e-20);
    return x > knee ? rolled : x;
}

// pcg
uint pcg(uint value) {
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// pixel_hash
uint pixel_hash(int x, int y, uint seed, int layer) {
    return pcg(uint(x) ^ pcg(uint(y) ^ pcg(seed ^ (uint(layer) * 0x9E3779B9u))));
}

// gaussian_from_hash
float gaussian_from_hash(uint hash) {
    uint hash2 = pcg(hash);
    float uniform1 = float(hash >> 8u) * (1.0 / 16777216.0) + 1.0e-7;
    float uniform2 = float(hash2 >> 8u) * (1.0 / 16777216.0);
    return sqrt(-2.0 * log(uniform1)) * cos(6.28318548 * uniform2);
}

// normal_sample
float normal_sample(int x, int y, uint seed, int layer) {
    return gaussian_from_hash(pixel_hash(x, y, seed, layer));
}

// poisson_sample
float poisson_sample(int x, int y, uint seed, float lambda, int layer) {
    uint hash = pixel_hash(x, y, seed, layer);
    uint initial_hash = hash;
    float product = 1.0;
    int trials = 0;
    float limit = exp(-lambda);
    for (int i = 0; i < 32; ++i) {
        bool running = product > limit;
        hash = pcg(hash);
        float draw = float(hash >> 8u) * (1.0 / 16777216.0);
        product = running ? product * draw : product;
        trials += running ? 1 : 0;
    }
    int count = max(trials - 1, 0);
    float poisson = (float(count) - lambda) / sqrt(max(lambda, 1.0e-4));
    return lambda >= 16.0 ? gaussian_from_hash(initial_hash) : poisson;
}
"""

    /** Curves.h, Couplers.h, Development.h. */
    private const val FILM = """
// curve_density
float curve_density(int base, float log_exposure) {
    float d_min = cfg(base);
    float gamma = cfg(base + 1);
    float toe = cfg(base + 2);
    float toe_width = max(cfg(base + 3), 1.0e-6);
    float shoulder = cfg(base + 4);
    float shoulder_width = max(cfg(base + 5), 1.0e-6);
    float toe_term = toe_width * softplus((log_exposure - toe) / toe_width);
    float shoulder_term = shoulder_width * softplus((log_exposure - shoulder) / shoulder_width);
    return d_min + gamma * min(max(toe_term - shoulder_term, 0.0), shoulder - toe);
}

// curve_component_density
float curve_component_density(int base, float log_exposure) {
    float gamma = cfg(base);
    float toe = cfg(base + 1);
    float toe_width = max(cfg(base + 2), 1.0e-6);
    float shoulder = cfg(base + 3);
    float shoulder_width = max(cfg(base + 4), 1.0e-6);
    float toe_term = toe_width * softplus((log_exposure - toe) / toe_width);
    float shoulder_term = shoulder_width * softplus((log_exposure - shoulder) / shoulder_width);
    return gamma * min(max(toe_term - shoulder_term, 0.0), shoulder - toe);
}

// sampled_film_density
float sampled_film_density(int channel, float x) {
    int base = CFG_SAMPLED_CURVES + channel * SAMPLED_CURVE_STRIDE;
    int count = clamp(int(cfg(base)), 2, SAMPLED_CURVE_MAX_SAMPLES);
    int low = 0;
    for (int stride = SAMPLED_CURVE_MAX_SAMPLES / 2; stride > 0; stride /= 2) {
        int candidate = min(low + stride, count - 1);
        low = cfg(base + 1 + candidate * 3) <= x ? candidate : low;
    }
    int high = min(low + 1, count - 1);
    int i = base + 1 + low * 3;
    int j = base + 1 + high * 3;
    float h = max(cfg(j) - cfg(i), 1.0e-12);
    float t = clamp((x - cfg(i)) / h, 0.0, 1.0);
    float y0 = cfg(i + 1);
    float y1 = cfg(j + 1);
    float a = h * cfg(i + 2);
    float b = h * cfg(j + 2);
    float delta = y1 - y0;
    float y = y0 + t * (a + t * (3.0 * delta - 2.0 * a - b + t * (-2.0 * delta + a + b)));
    return x <= cfg(base + 1) ? cfg(base + 2)
        : (x >= cfg(base + 1 + (count - 1) * 3) ? cfg(base + 2 + (count - 1) * 3) : y);
}

// has_sampled_film_curve
bool has_sampled_film_curve(int channel) {
    return cfg(CFG_SAMPLED_CURVES + channel * SAMPLED_CURVE_STRIDE) >= 2.0;
}

// film_density (GpuBackend::film_curve with analytic curves)
float film_density(int channel, float log_exposure) {
    if (has_sampled_film_curve(channel)) return sampled_film_density(channel, log_exposure);
    return curve_density(CFG_CURVES + channel * 6, log_exposure)
        + curve_component_density(CFG_CURVE_SECONDARY + channel * 5, log_exposure);
}

// paper_curve_base
int paper_curve_base(int channel) {
    return channel == 0 ? CFG_PAPER_RED : (channel == 1 ? CFG_PAPER : CFG_PAPER_BLUE);
}

// paper_midpoint
float paper_midpoint(int channel) {
    return channel == 0 ? cfg(CFG_PAPER_MIDPOINT_RED)
        : (channel == 1 ? cfg(CFG_PAPER_MIDPOINT) : cfg(CFG_PAPER_MIDPOINT_BLUE));
}

// paper_exposure
float paper_exposure(int channel, float relative) {
    float flash = cfg(CFG_PRINTER_PREFLASH);
    float flashed_rel = fs_log10(max(fs_pow10(relative) + flash, 1.0e-12));
    float effective_rel = flash > 0.0 ? flashed_rel : relative;
    return paper_midpoint(channel) + cfg(CFG_MASKING + channel) * effective_rel;
}

// paper_curve_table entry (kCurveSamples 2048 over kCurveMin -8 .. kCurveMax 8)
float paper_curve_table(int index, int channel) {
    float log_exposure = -8.0 + 16.0 * (float(index) / 2047.0);
    return curve_density(paper_curve_base(channel), log_exposure);
}

// sample_curve over paper_curve_table
float sample_paper_curve(float log_exposure, int channel) {
    float q = clamp((log_exposure - (-8.0)) * (2047.0 / 16.0), 0.0, 2047.0);
    int index = min(int(q), 2046);
    float frac = q - float(index);
    float low = paper_curve_table(index, channel);
    return low + frac * (paper_curve_table(index + 1, channel) - low);
}

// curve_range
float curve_range(int base) {
    return cfg(base + 1) * (cfg(base + 4) - cfg(base + 2));
}

// film_curve_range
float film_curve_range(int channel) {
    int base = CFG_CURVES + channel * 6;
    int secondary = CFG_CURVE_SECONDARY + channel * 5;
    return curve_range(base) + cfg(secondary) * (cfg(secondary + 3) - cfg(secondary + 1));
}

// paper_activation
float paper_activation(int channel, float relative) {
    int base = paper_curve_base(channel);
    float exposure = paper_exposure(channel, relative);
    return (sample_paper_curve(exposure, channel) - cfg(base)) / curve_range(base);
}

// inhibitor_release
float inhibitor_release(float activation, float gamma) {
    float a = clamp(activation, 0.0, 1.0);
    float released = pow(a, gamma);
    float retained = pow(1.0 - a, gamma);
    float nonlinear = released / max(released + retained, 1.0e-8);
    return gamma == 1.0 ? a : nonlinear;
}

// adjacency_transport
float adjacency_transport(float primary, float secondary) {
    const float share = 0.2753401713;
    return cfg(CFG_ADJACENCY_MODEL) > 0.5 ? share * primary + (1.0 - share) * secondary : primary;
}

// adjacency_density
float adjacency_density(int channel, float formed, float residual) {
    float base = cfg(CFG_CURVES + channel * 6);
    float net = max(formed - base, 0.0);
    float corrected = base + clamp(
        net + cfg(CFG_ADJACENCY_STRENGTH) * net * residual, 0.0, film_curve_range(channel));
    return cfg(CFG_ADJACENCY_MODEL) > 0.5 ? corrected : formed;
}

// coupler_release
float coupler_release(int donor, float activation) {
    return inhibitor_release(activation, cfg(CFG_COUPLER_RELEASE_GAMMA + donor));
}

// donor_release
float donor_release(float activation) {
    return inhibitor_release(activation, cfg(CFG_DONOR_RELEASE_GAMMA));
}

// coupler_inhibition
float coupler_inhibition(int channel, float donor0, float donor1, float donor2) {
    int base = CFG_COUPLER + channel * 3;
    float released = cfg(base) * donor0 + cfg(base + 1) * donor1 + cfg(base + 2) * donor2;
    return released * cfg(CFG_COUPLER_SCALE);
}

// chromatic_fringe_inhibition
float chromatic_fringe_inhibition(int channel, float delta0, float delta1, float delta2) {
    int base = CFG_COUPLER + channel * 3;
    float residual = (channel != 0 ? cfg(base) * delta0 : 0.0)
        + (channel != 1 ? cfg(base + 1) * delta1 : 0.0)
        + (channel != 2 ? cfg(base + 2) * delta2 : 0.0);
    return residual * cfg(CFG_COUPLER_SCALE) * cfg(CFG_CHROMATIC_FRINGE_AMOUNT);
}

// coupler_warp
float coupler_warp(int channel, float u) {
    float q = clamp((u - (-4.0)) * (127.0 / 8.0), 0.0, 127.0);
    int index = min(int(q), COUPLER_WARP_SAMPLES - 2);
    float frac = q - float(index);
    int base = CFG_COUPLER_WARP + channel * COUPLER_WARP_SAMPLES;
    float low_sample = cfg(base + index);
    float high_sample = cfg(base + index + 1);
    float linear_value = low_sample + frac * (high_sample - low_sample);
    float previous = cfg(base + max(index - 1, 0));
    float following = cfg(base + min(index + 2, COUPLER_WARP_SAMPLES - 1));
    float delta = max(high_sample - low_sample, 0.0);
    float low_slope = clamp(0.5 * (high_sample - previous), 0.0, 3.0 * delta);
    float high_slope = clamp(0.5 * (following - low_sample), 0.0, 3.0 * delta);
    float frac2 = frac * frac;
    float frac3 = frac2 * frac;
    float cubic = (2.0 * frac3 - 3.0 * frac2 + 1.0) * low_sample
        + (frac3 - 2.0 * frac2 + frac) * low_slope
        + (-2.0 * frac3 + 3.0 * frac2) * high_sample
        + (frac3 - frac2) * high_slope;
    bool nonlinear = cfg(CFG_COUPLER_RELEASE_GAMMA) != 1.0
        || cfg(CFG_COUPLER_RELEASE_GAMMA + 1) != 1.0
        || cfg(CFG_COUPLER_RELEASE_GAMMA + 2) != 1.0
        || cfg(CFG_DONOR_RELEASE_GAMMA) != 1.0;
    return nonlinear ? cubic : linear_value;
}

// film_activation
float film_activation(int channel, float formed) {
    int base = CFG_CURVES + channel * 6;
    return (formed - cfg(base)) / film_curve_range(channel);
}

// donor_inhibition
float donor_inhibition(int channel, float donor_released) {
    return cfg(CFG_DONOR_RELEASE + channel) * donor_released * cfg(CFG_COUPLER_SCALE);
}

// fringe_inhibition
float fringe_inhibition(int channel, float inhibition, int fringe_radius,
                        float delta0, float delta1, float delta2) {
    float fringe = chromatic_fringe_inhibition(channel, delta0, delta1, delta2);
    return fringe_radius > 0 ? inhibition + fringe : inhibition;
}

// inhibited_log_exposure
float inhibited_log_exposure(int channel, float log_exposure, float inhibition) {
    float u = log_exposure - inhibition;
    return u + coupler_warp(channel, u);
}

// adjacency_shift
float adjacency_shift(float residual) {
    return cfg(CFG_ADJACENCY_MODEL) > 0.5 ? 0.0 : -cfg(CFG_ADJACENCY_STRENGTH) * residual;
}

// developed_density
float developed_density(int channel, float formed) {
    int base = CFG_CURVES + channel * 6;
    float d_min = cfg(base);
    float range = film_curve_range(channel);
    return cfg(CFG_DEVELOP_COMPLEMENT) > 0.5 ? d_min + range - (formed - d_min) : formed;
}

// density_position(...).net
float density_position_net(int channel, float density) {
    int base = CFG_CURVES + channel * 6;
    float range = film_curve_range(channel);
    float amount = clamp((density - cfg(base)) / range, 0.0, 1.0);
    return amount * range;
}
"""

    /** Exposure.h, Halation.h (pointwise), Transfer.h, Print.h. */
    private const val OPTICS = """
const float LUMA_R = 0.2627002;
const float LUMA_G = 0.6779981;
const float LUMA_B = 0.0593017;

// tone_base
float tone_base(float stops, int frame_x, int frame_y) {
    int grid_w = int(cfg(CFG_TONE_GRID_WIDTH));
    int grid_h = int(cfg(CFG_TONE_GRID_HEIGHT));
    float frame_w = max(cfg(CFG_FRAME_WIDTH), 1.0);
    float frame_h = max(cfg(CFG_FRAME_HEIGHT), 1.0);
    float gx = clamp((float(frame_x) + 0.5) * float(grid_w) / frame_w - 0.5, 0.0, float(grid_w - 1));
    float gy = clamp((float(frame_y) + 0.5) * float(grid_h) / frame_h - 0.5, 0.0, float(grid_h - 1));
    int x0 = clamp(int(gx), 0, max(grid_w - 2, 0));
    int y0 = clamp(int(gy), 0, max(grid_h - 2, 0));
    int x1 = min(x0 + 1, grid_w - 1);
    int y1 = min(y0 + 1, grid_h - 1);
    float fx = clamp(gx - float(x0), 0.0, 1.0);
    float fy = clamp(gy - float(y0), 0.0, 1.0);
    float a00 = cfg(CFG_TONE_GRID_A + y0 * grid_w + x0);
    float a10 = cfg(CFG_TONE_GRID_A + y0 * grid_w + x1);
    float a01 = cfg(CFG_TONE_GRID_A + y1 * grid_w + x0);
    float a11 = cfg(CFG_TONE_GRID_A + y1 * grid_w + x1);
    float b00 = cfg(CFG_TONE_GRID_B + y0 * grid_w + x0);
    float b10 = cfg(CFG_TONE_GRID_B + y0 * grid_w + x1);
    float b01 = cfg(CFG_TONE_GRID_B + y1 * grid_w + x0);
    float b11 = cfg(CFG_TONE_GRID_B + y1 * grid_w + x1);
    float a = (1.0 - fy) * ((1.0 - fx) * a00 + fx * a10) + fy * ((1.0 - fx) * a01 + fx * a11);
    float b = (1.0 - fy) * ((1.0 - fx) * b00 + fx * b10) + fy * ((1.0 - fx) * b01 + fx * b11);
    return a * stops + b;
}

// creative_exposure
vec3 creative_exposure(float red, float green, float blue, int frame_x, int frame_y) {
    float knee = cfg(CFG_HDR_RANGE);
    float curvature = cfg(CFG_HDR_RANGE + 1);
    float source_peak = max(red, max(green, blue));
    float over = log(max(source_peak / knee, 1.0)) * (1.0 / 0.6931472);
    float range_ev = over / (1.0 + curvature * over) - over;
    float range_gain = (curvature > 0.0 && source_peak > knee) ? pow(2.0, range_ev) : 1.0;
    float r0 = red * range_gain * cfg(CFG_WHITE_BALANCE);
    float g0 = green * range_gain * cfg(CFG_WHITE_BALANCE + 1);
    float b0 = blue * range_gain * cfg(CFG_WHITE_BALANCE + 2);

    float metered = (LUMA_R * r0 + LUMA_G * g0 + LUMA_B * b0)
        * cfg(CFG_EXPOSURE_GAIN) * (1.0 / 0.18);
    float stops = log(max(metered, 1.0e-6)) * (1.0 / 0.6931472);
    float keyed = tone_base(stops, frame_x, frame_y);
    float high = clamp(keyed * (1.0 / 6.0), 0.0, 1.0);
    float low = clamp(-keyed * (1.0 / 6.0), 0.0, 1.0);
    float highlight_mask = high * high * (3.0 - 2.0 * high);
    float shadow_mask = low * low * (3.0 - 2.0 * low);
    float tone_ev = 3.0 * (cfg(CFG_HIGHLIGHTS) * highlight_mask + cfg(CFG_SHADOWS) * shadow_mask);
    float tone_gain = pow(2.0, tone_ev);
    float r1 = r0 * tone_gain;
    float g1 = g0 * tone_gain;
    float b1 = b0 * tone_gain;

    float luma1 = LUMA_R * r1 + LUMA_G * g1 + LUMA_B * b1;
    float peak = max(r1, max(g1, b1));
    float colourfulness = (peak - min(r1, min(g1, b1))) / max(peak, 1.0e-6);
    float chroma = cfg(CFG_SATURATION) * (1.0 + cfg(CFG_VIBRANCE) * (1.0 - colourfulness));
    return vec3(luma1 + chroma * (r1 - luma1),
                luma1 + chroma * (g1 - luma1),
                luma1 + chroma * (b1 - luma1));
}

// disc_edge_share
float disc_edge_share(float dist, float radius) {
    float u = clamp(dist / max(radius, 1.0e-6), -1.0, 1.0);
    float v = abs(u);
    float arc = sqrt(1.0 - v) * (1.5707288 + v * (-0.2121144 + v * (0.0742610 + v * -0.0187293)));
    float shaded = (arc - v * sqrt(1.0 - v * v)) * 0.318309886;
    return u >= 0.0 ? shaded : 1.0 - shaded;
}

// rounded_rect_distance
float rounded_rect_distance(int frame_x, int frame_y, float left, float top,
                            float right, float bottom, float corner) {
    float cx = float(frame_x) + 0.5;
    float cy = float(frame_y) + 0.5;
    float qx = max(left - cx, cx - right) + corner;
    float qy = max(top - cy, cy - bottom) + corner;
    float ox = max(qx, 0.0);
    float oy = max(qy, 0.0);
    return sqrt(ox * ox + oy * oy) + min(max(qx, qy), 0.0) - corner;
}

// gate_transmission
float gate_transmission(int frame_x, int frame_y) {
    float radius = cfg(CFG_GATE + 4);
    float dist = rounded_rect_distance(frame_x, frame_y, cfg(CFG_GATE), cfg(CFG_GATE + 1),
        cfg(CFG_GATE + 2), cfg(CFG_GATE + 3), cfg(CFG_GATE_CORNER));
    return radius >= 0.0 ? disc_edge_share(dist, radius) : 1.0;
}

// filing_noise sample
float filing_sample(int a, int b) {
    uint hash = pcg(uint(a) ^ pcg(uint(b)));
    return float(hash >> 8u) * (2.0 / 16777216.0) - 1.0;
}

// filing_noise
float filing_noise(float x, float y) {
    float fx = floor(x);
    float fy = floor(y);
    int ix = int(fx);
    int iy = int(fy);
    float tx = x - fx;
    float ty = y - fy;
    float u = tx * tx * (3.0 - 2.0 * tx);
    float v = ty * ty * (3.0 - 2.0 * ty);
    float low = filing_sample(ix, iy) + (filing_sample(ix + 1, iy) - filing_sample(ix, iy)) * u;
    float high = filing_sample(ix, iy + 1)
        + (filing_sample(ix + 1, iy + 1) - filing_sample(ix, iy + 1)) * u;
    return low + (high - low) * v;
}

// carrier_density
float carrier_density(int frame_x, int frame_y) {
    float radius = cfg(CFG_CARRIER + 5);
    float depth = cfg(CFG_CARRIER + 6);
    float cell = max(cfg(CFG_CARRIER + 7), 1.0);
    float dist = rounded_rect_distance(frame_x, frame_y, cfg(CFG_CARRIER), cfg(CFG_CARRIER + 1),
        cfg(CFG_CARRIER + 2), cfg(CFG_CARRIER + 3), cfg(CFG_CARRIER + 4));
    float wobble = filing_noise((float(frame_x) + 0.5) / cell, (float(frame_y) + 0.5) / cell);
    float filed = dist + depth * clamp(0.5 + 0.5 * wobble, 0.0, 1.0);
    float passed = disc_edge_share(filed, radius);
    float density = -log(max(passed, 1.0e-6)) * (1.0 / 2.3025851);
    return radius >= 0.0 ? density : 0.0;
}

// halation_returned
float halation_returned(int channel, float direct, float blurred0, float blurred1, float blurred2) {
    int kernel = CFG_HALATION_KERNEL + channel * 3;
    float scattered = cfg(kernel) * blurred0 + cfg(kernel + 1) * blurred1 + cfg(kernel + 2) * blurred2;
    return scattered - direct;
}

// halation_mix
float halation_mix(int channel, float direct, vec3 returned) {
    int row = CFG_HALATION_MATRIX + channel * 3;
    return direct + cfg(row) * returned.x + cfg(row + 1) * returned.y + cfg(row + 2) * returned.z;
}

// veiling_glare
float veiling_glare(float direct, float mean, float fraction) {
    return (1.0 - fraction) * direct + fraction * mean;
}

// diffusion_mix
float diffusion_mix(int channel, float direct, float blurred0, float blurred1, float blurred2) {
    int kernel = channel == 3 ? CFG_DONOR_DIFFUSION_KERNEL : CFG_DIFFUSION_KERNEL + channel * 3;
    return cfg(CFG_DIFFUSION_DIRECT) * direct + cfg(kernel) * blurred0
        + cfg(kernel + 1) * blurred1 + cfg(kernel + 2) * blurred2;
}

// mtf_luma_mix
float mtf_luma_mix(float per_layer, float per_layer_luma, float luma_blurred) {
    return per_layer + cfg(CFG_MTF_LUMA_SHARE) * (luma_blurred - per_layer_luma);
}

// srgb_encode
float srgb_encode(float lin) {
    float curve = 1.055 * pow(max(lin, 0.0031308), 1.0 / 2.4) - 0.055;
    return lin <= 0.0031308 ? lin * 12.92
        : (lin >= 1.0 ? 1.0 + (lin - 1.0) * (1.055 / 2.4) : curve);
}

// srgb_decode
float srgb_decode(float coded) {
    float curve = pow(max((coded + 0.055) / 1.055, 0.0), 2.4);
    return coded <= 0.04045 ? coded / 12.92
        : (coded >= 1.0 ? 1.0 + (coded - 1.0) / (1.055 / 2.4) : curve);
}

// host_transfer_encode_shaped
float host_transfer_encode_shaped(float value, int shape_index) {
    float c0 = cfg(CFG_OUTPUT_COEFFICIENTS);
    float c1 = cfg(CFG_OUTPUT_COEFFICIENTS + 1);
    float c2 = cfg(CFG_OUTPUT_COEFFICIENTS + 2);
    float c3 = cfg(CFG_OUTPUT_COEFFICIENTS + 3);
    float c4 = cfg(CFG_OUTPUT_COEFFICIENTS + 4);
    float c5 = cfg(CFG_OUTPUT_COEFFICIENTS + 5);
    float magnitude = abs(value);
    float power = (value < 0.0 ? -1.0 : 1.0)
        * (magnitude <= c4 ? magnitude * c0 : c1 * pow(magnitude, c2) + c3);
    float logarithmic = value <= c4 ? value * c0 + c3 : c1 * log(value + c5) + c2;
    return shape_index == 0 ? value : (shape_index == 1 ? power : logarithmic);
}

// host_transfer_shape
int host_transfer_shape() {
    float shape = cfg(CFG_OUTPUT_TRANSFER);
    return shape < 0.5 ? 0 : (shape < 1.5 ? 1 : 2);
}

// host_output_shouldered
float host_output_shouldered(float r, float g, float b, int row) {
    float host[3];
    for (int c = 0; c < 3; ++c) {
        host[c] = cfg(CFG_OUTPUT_MATRIX + 3 * c) * r
            + cfg(CFG_OUTPUT_MATRIX + 3 * c + 1) * g
            + cfg(CFG_OUTPUT_MATRIX + 3 * c + 2) * b;
    }
    float y = cfg(CFG_OUTPUT_GAMUT + 1) * host[0] + cfg(CFG_OUTPUT_GAMUT + 2) * host[1]
        + cfg(CFG_OUTPUT_GAMUT + 3) * host[2];
    float scale = 1.0;
    for (int c = 0; c < 3; ++c) {
        float v = host[c];
        scale = min(scale, v > 1.0 ? (1.0 - y) / max(v - y, 1.0e-20)
            : (v < 0.0 ? y / max(y - v, 1.0e-20) : 1.0));
    }
    float picked = host[row];
    float fitted = !(y > 0.0 && y < 1.0) ? max(picked, 0.0)
        : (scale >= 1.0 ? picked : y + (picked - y) * max(scale, 0.0));
    bool inside = min(host[0], min(host[1], host[2])) >= 0.0
        && max(host[0], max(host[1], host[2])) <= 1.0;
    float in_host_primaries = (cfg(CFG_OUTPUT_GAMUT) == 0.0 || inside) ? picked : fitted;
    float knee = cfg(CFG_OUTPUT_SHOULDER);
    return knee < 0.0 ? in_host_primaries
        : display_shoulder(in_host_primaries, clamp(knee, 0.0, 1.0));
}

// color_grade
float color_grade(int channel, float value) {
    float lift = cfg(CFG_GRADE_LIFT + channel);
    float gain = cfg(CFG_GRADE_GAIN + channel);
    float inverse_gamma = cfg(CFG_GRADE_INV_GAMMA + channel);
    bool encoded = cfg(CFG_GRADE_SPACE) != 0.0;
    float working = encoded ? srgb_encode(value) : value;
    float lifted = working * (gain - lift) + lift;
    float graded = inverse_gamma == 1.0 ? lifted : pow(max(lifted, 0.0), inverse_gamma);
    return encoded ? srgb_decode(graded) : graded;
}

// transmittance_of
float transmittance_of(float density) { return fs_pow10(-density); }

// density_of
float density_of(float transmittance) { return -fs_log10(max(transmittance, 1.0e-6)); }

// print_mtf_read
float print_mtf_read(float transmittance, float spread) {
    float keep = cfg(CFG_PRINT_SHARPEN);
    return keep > 0.0 ? spread + keep * (transmittance - spread) : spread;
}

// print_finish ease
float finish_ease(float t) {
    float x = clamp(t, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

// print_finish
vec3 print_finish(vec3 rgb) {
    float s0 = cfg(CFG_PRINT_FINISH);
    float s1 = cfg(CFG_PRINT_FINISH + 1);
    float s2 = cfg(CFG_PRINT_FINISH + 2);
    float s3 = cfg(CFG_PRINT_FINISH + 3);
    float s4 = cfg(CFG_PRINT_FINISH + 4);
    float s5 = cfg(CFG_PRINT_FINISH + 5);
    float s6 = cfg(CFG_PRINT_FINISH + 6);
    const float kR = 0.2289746;
    const float kG = 0.6917385;
    const float kB = 0.0792869;
    float r = rgb.x * (1.0 + s0);
    float g = rgb.y * (1.0 + s1);
    float b = rgb.z * (1.0 + s2);
    float luminance = kR * r + kG * g + kB * b;
    float stops = log(max(luminance, 1.0e-6) * (1.0 / 0.18)) * (1.0 / 0.6931472);
    float moved = s3 * 0.75 * finish_ease(stops * (1.0 / 3.0))
        + s4 * 0.75 * finish_ease(-stops * (1.0 / 4.0));
    float tone = luminance > 1.0e-6 ? exp(moved * 0.6931472) : 1.0;
    float r1 = r * tone;
    float g1 = g * tone;
    float b1 = b * tone;
    float luma = kR * r1 + kG * g1 + kB * b1;
    float peak = max(r1, max(g1, b1));
    float colourfulness = (peak - min(r1, min(g1, b1))) / max(peak, 1.0e-6);
    float chroma = (1.0 + s5) * (1.0 + s6 * (1.0 - colourfulness));
    vec3 finished = vec3(luma + chroma * (r1 - luma), luma + chroma * (g1 - luma),
                         luma + chroma * (b1 - luma));
    bool neutral = s0 == 0.0 && s1 == 0.0 && s2 == 0.0 && s3 == 0.0 && s4 == 0.0
        && s5 == 0.0 && s6 == 0.0;
    return neutral ? rgb : finished;
}

// plain_print (kRec2020ToP3)
float plain_print(vec3 scene, int channel) {
    float gain = cfg(CFG_EXPOSURE_GAIN);
    float r = scene.x * gain;
    float g = scene.y * gain;
    float b = scene.z * gain;
    float row0 = 1.343578253 * r + -0.282179671 * g + -0.061398582 * b;
    float row1 = -0.065297453 * r + 1.075787916 * g + -0.010490463 * b;
    float row2 = 0.002821787 * r + -0.019598495 * g + 1.016776707 * b;
    return color_grade(channel, channel == 0 ? row0 : (channel == 1 ? row1 : row2));
}
"""

    /** Sampling.h tetrahedral interpolation (`lut_sample_with`), four channels at once. */
    private const val LUT = """
// lut_sample (tetrahedral, SpectralLUT.sample)
vec4 lut_sample(highp sampler3D lut, float px, float py, float pz) {
    float qx = clamp(px, 0.0, 1.0) * 32.0;
    float qy = clamp(py, 0.0, 1.0) * 32.0;
    float qz = clamp(pz, 0.0, 1.0) * 32.0;
    int x0 = min(int(qx), 31);
    int y0 = min(int(qy), 31);
    int z0 = min(int(qz), 31);
    float fx = qx - float(x0);
    float fy = qy - float(y0);
    float fz = qz - float(z0);
    float largest = max(fx, max(fy, fz));
    float smallest = min(fx, min(fy, fz));
    float middle = max(min(fx, fy), min(max(fx, fy), fz));
    bool x_largest = fx >= fy && fx >= fz;
    bool y_largest = !x_largest && fy >= fz;
    bool x_smallest = fx <= fy && fx <= fz;
    bool y_smallest = !x_smallest && fy <= fz;
    ivec3 a = ivec3(x_largest ? 1 : 0, y_largest ? 1 : 0, (!x_largest && !y_largest) ? 1 : 0);
    ivec3 b = ivec3(x_smallest ? 0 : 1, y_smallest ? 0 : 1, (!x_smallest && !y_smallest) ? 0 : 1);
    ivec3 o = ivec3(x0, y0, z0);
    vec4 c000 = texelFetch(lut, o, 0);
    vec4 near_step = texelFetch(lut, o + a, 0);
    vec4 far_step = texelFetch(lut, o + b, 0);
    vec4 c111 = texelFetch(lut, o + ivec3(1), 0);
    return c000 + largest * (near_step - c000) + middle * (far_step - near_step)
        + smallest * (c111 - far_step);
}

// exposure_domain (x, y, z, scale)
vec4 exposure_domain(vec3 rgb) {
    float r = rgb.x;
    float g = rgb.y;
    float b = rgb.z;
    float y = LUMA_R * r + LUMA_G * g + LUMA_B * b;
    float dr = 0.670231843 * r + 0.152168745 * g + 0.177599412 * b;
    float dg = 0.044501114 * r + 0.854482372 * g + 0.101016514 * b;
    float db = 0.025777047 * g + 0.974222953 * b;
    float tr = dr < 0.0 ? y / (y - dr) : 1.0;
    float tg = dg < 0.0 ? y / (y - dg) : 1.0;
    float tb = db < 0.0 ? y / (y - db) : 1.0;
    float s = min(min(tr, tg), tb);
    bool beyond_locus = dr < 0.0 || dg < 0.0 || db < 0.0;
    float pr = y <= 0.0 ? 0.0 : (beyond_locus ? y + s * (dr - y) : dr);
    float pg = y <= 0.0 ? 0.0 : (beyond_locus ? y + s * (dg - y) : dg);
    float pb = y <= 0.0 ? 0.0 : (beyond_locus ? y + s * (db - y) : db);
    float wr = max(pr, 0.0);
    float wg = max(pg, 0.0);
    float wb = max(pb, 0.0);
    float radiance_scale = max(max(wr, max(wg, wb)), 1.0e-6);
    return vec4(wr / radiance_scale, wg / radiance_scale, wb / radiance_scale,
                radiance_scale * cfg(CFG_EXPOSURE_GAIN) / 0.18);
}

// scene_exposure, all four records (the fourth is the donor stock's)
vec4 scene_exposure(highp sampler3D exposure_lut, vec4 source, int frame_x, int frame_y) {
    vec3 scene = creative_exposure(source.x, source.y, source.z, frame_x, frame_y);
    vec4 domain = exposure_domain(scene);
    vec4 recovered = max(lut_sample(exposure_lut, domain.x, domain.y, domain.z) * domain.w, vec4(0.0));
    vec4 raw_exp = cfg(CFG_RECORD_INPUT) != 0.0 ? source : recovered;
    float flash = cfg(CFG_CAMERA_PREFLASH);
    return flash > 0.0 ? raw_exp + flash : raw_exp;
}
"""

    /** Grain.h. */
    private const val GRAIN = """
// dye_cloud_granularity_variance
float dye_cloud_granularity_variance(float density, float amplitude, float toe, float decay,
                                     float hump, float hump_density, float hump_width) {
    float offset = (density - hump_density) / max(hump_width, 1.0e-4);
    return (1.0 - exp(-density / max(toe, 1.0e-4)))
        * (1.0 + amplitude * exp(-density / max(decay, 1.0e-4))
           + hump * exp(-0.5 * offset * offset));
}

// silver_granularity_variance
float silver_granularity_variance(float density) {
    return density * pow(10.0, 0.21004 * density + 0.06114 * density * density);
}

// reversal_granularity_variance
float reversal_granularity_variance(float density, float exponent, float shoulder) {
    float power = pow(density / max(shoulder, 1.0e-4), 2.0 * exponent);
    return power / (1.0 + power);
}

// grain_density_modulation
float grain_density_modulation(int layer, float net_density) {
    float fog = cfg(CFG_GRAIN_FOG + layer);
    float anchor = max(cfg(CFG_GRAIN_ANCHOR + layer) + fog, 1.0e-4);
    float here = max(net_density, 0.0) + fog;
    int record = CFG_GRAIN_DENSITY_RECORDS + layer * 6;
    float amplitude = cfg(record);
    float toe = cfg(record + 1);
    float decay = cfg(record + 2);
    float hump = cfg(record + 3);
    float hump_density = cfg(record + 4);
    float hump_width = cfg(record + 5);
    float dye_cloud = dye_cloud_granularity_variance(here, amplitude, toe, decay, hump,
            hump_density, hump_width)
        / max(dye_cloud_granularity_variance(anchor, amplitude, toe, decay, hump,
            hump_density, hump_width), 1.0e-6);
    float silver = silver_granularity_variance(here)
        / max(silver_granularity_variance(anchor), 1.0e-6);
    float exponent = cfg(CFG_GRAIN_REVERSAL_PROFILE);
    float shoulder = cfg(CFG_GRAIN_REVERSAL_PROFILE + 1);
    float reversal = reversal_granularity_variance(here, exponent, shoulder)
        / max(reversal_granularity_variance(anchor, exponent, shoulder), 1.0e-20);
    float selwyn = here / anchor;
    float law = cfg(CFG_GRAIN_LAW);
    float ratio = law > 2.5 ? reversal : (law > 1.5 ? selwyn : (law > 0.5 ? silver : dye_cloud));
    return sqrt(max(ratio, 0.0));
}

// grain_mix
float grain_mix(float own, float common_field) {
    float rho = clamp(cfg(CFG_GRAIN_CORRELATION), 0.0, 1.0);
    return sqrt(1.0 - rho) * own + sqrt(rho) * common_field;
}
"""

    /** FilmTiles.h: the film grain model sampled from its running-sum tiles. */
    private const val FILM_TILES = """
uniform highp sampler2DArray uFilmTiles;

// film_tile_entry (model on: every index clamped into the tile)
float film_tile_entry(int ix, int iy, int level, int record) {
    int layer = clamp(record, 0, 2) * FILM_TILE_LEVELS + clamp(level, 0, FILM_TILE_LEVELS - 1);
    return texelFetch(uFilmTiles,
        ivec3(clamp(ix, 0, FILM_TILE_SIDE), clamp(iy, 0, FILM_TILE_SIDE), layer), 0).r;
}

// film_tile_sum_inside
float film_tile_sum_inside(float px, float py, int level, int record) {
    int ix = min(int(floor(px)), FILM_TILE_SIDE - 1);
    int iy = min(int(floor(py)), FILM_TILE_SIDE - 1);
    float ax = px - float(ix);
    float ay = py - float(iy);
    float s00 = film_tile_entry(ix, iy, level, record);
    float s10 = film_tile_entry(ix + 1, iy, level, record);
    float s01 = film_tile_entry(ix, iy + 1, level, record);
    float s11 = film_tile_entry(ix + 1, iy + 1, level, record);
    return s00 + (s10 - s00) * ax + (s01 - s00) * ay + (s11 - s10 - s01 + s00) * ax * ay;
}

// film_tile_sum_wrapped
float film_tile_sum_wrapped(float px, float py, int level, int record) {
    const float side = float(FILM_TILE_SIDE);
    bool wx = px >= side;
    bool wy = py >= side;
    float qx = wx ? px - side : px;
    float qy = wy ? py - side : py;
    int iy = min(int(floor(qy)), FILM_TILE_SIDE - 1);
    int ix = min(int(floor(qx)), FILM_TILE_SIDE - 1);
    float ay = qy - float(iy);
    float ax = qx - float(ix);
    float column = film_tile_entry(FILM_TILE_SIDE, iy, level, record);
    column = column + (film_tile_entry(FILM_TILE_SIDE, iy + 1, level, record) - column) * ay;
    float row = film_tile_entry(ix, FILM_TILE_SIDE, level, record);
    row = row + (film_tile_entry(ix + 1, FILM_TILE_SIDE, level, record) - row) * ax;
    float corner = film_tile_entry(FILM_TILE_SIDE, FILM_TILE_SIDE, level, record);
    return film_tile_sum_inside(qx, qy, level, record)
        + (wx ? column : 0.0) + (wy ? row : 0.0) + ((wx && wy) ? corner : 0.0);
}

// film_tile_box
float film_tile_box(float x0, float x1, float y0, float y1, int level, int record) {
    return film_tile_sum_wrapped(x1, y1, level, record)
        - film_tile_sum_wrapped(x0, y1, level, record)
        - film_tile_sum_wrapped(x1, y0, level, record)
        + film_tile_sum_wrapped(x0, y0, level, record);
}

// film_tile_grain share
float film_tile_share(float c, int b) {
    const float block = float(FILM_TILE_BLOCK);
    const float seam = float(FILM_TILE_SEAM);
    float start = float(b) * block;
    return clamp((c - start + seam) * (0.5 / seam), 0.0, 1.0)
        * clamp((start + block + seam - c) * (0.5 / seam), 0.0, 1.0);
}

// film_tile_grain norm_axis
float film_tile_norm_axis(float c, int b0, int b1) {
    float first = film_tile_share(c, b0);
    float second = b1 > b0 ? film_tile_share(c, b1) : 0.0;
    return first * first + second * second;
}

// film_tile_grain laying
float film_tile_laying(int layer, int px, int py, int record, uint seed, int table, int k, float w) {
    const float block = float(FILM_TILE_BLOCK);
    const float margin = float(FILM_TILE_MARGIN);
    const float seam = float(FILM_TILE_SEAM);
    float pitch = cfg(CFG_FILM_TILE + FILM_TILE_PITCH);
    float footprint = cfg(CFG_FILM_TILE + FILM_TILE_FOOTPRINT);
    float shift = block * float(layer) / float(FILM_TILE_LAYERS);
    float cx = (float(px) + 0.5) * pitch + shift;
    float cy = (float(py) + 0.5) * pitch + shift;
    float x0 = cx - 0.5 * footprint;
    float x1 = x0 + footprint;
    float y0 = cy - 0.5 * footprint;
    float y1 = y0 + footprint;
    int bx0 = int(floor((cx - seam) * (1.0 / block)));
    int by0 = int(floor((cy - seam) * (1.0 / block)));
    int bx1 = min(int(floor((cx + seam) * (1.0 / block))), bx0 + 1);
    int by1 = min(int(floor((cy + seam) * (1.0 / block))), by0 + 1);
    float norm = film_tile_norm_axis(cx, bx0, bx1) * film_tile_norm_axis(cy, by0, by1);
    float grain = 0.0;
    for (int piece_y = 0; piece_y < 2; ++piece_y) {
        for (int piece_x = 0; piece_x < 2; ++piece_x) {
            if (!(bx0 + piece_x <= bx1 && by0 + piece_y <= by1)) continue;
            int bx = bx0 + piece_x;
            int by = by0 + piece_y;
            float fbx = float(bx) * block;
            float fby = float(by) * block;
            float u0 = max(x0, fbx - margin) - fbx;
            float u1 = min(x1, fbx + block + margin) - fbx;
            float v0 = max(y0, fby - margin) - fby;
            float v1 = min(y1, fby + block + margin) - fby;
            float area = max((u1 - u0) * (v1 - v0), 1.0e-6);
            uint hash = pixel_hash(bx, by, seed, FILM_TILE_STREAM + 3 * layer + record);
            float ox = float(hash % uint(FILM_TILE_SIDE)) + margin;
            float oy = float((hash >> 8u) % uint(FILM_TILE_SIDE)) + margin;
            int turn = int((hash >> 16u) & 7u);
            bool flip_x = (turn & 1) != 0;
            bool flip_y = (turn & 2) != 0;
            bool swap_axes = (turn & 4) != 0;
            float p0 = flip_x ? block - u1 : u0;
            float p1 = flip_x ? block - u0 : u1;
            float q0 = flip_y ? block - v1 : v0;
            float q1 = flip_y ? block - v0 : v1;
            float a0 = (swap_axes ? q0 : p0) + ox;
            float a1 = (swap_axes ? q1 : p1) + ox;
            float b0 = (swap_axes ? p0 : q0) + oy;
            float b1 = (swap_axes ? p1 : q1) + oy;
            float light_a = cfg(table + k) + film_tile_box(a0, a1, b0, b1, k, record) / area;
            float light_b = cfg(table + k + 1) + film_tile_box(a0, a1, b0, b1, k + 1, record) / area;
            float density_a = -log(max(light_a, 1.0e-9)) * 0.43429448190325176
                - cfg(table + FILM_TILE_LEVELS + k);
            float density_b = -log(max(light_b, 1.0e-9)) * 0.43429448190325176
                - cfg(table + FILM_TILE_LEVELS + k + 1);
            grain += film_tile_share(cx, bx) * film_tile_share(cy, by)
                * (density_a + (density_b - density_a) * w);
        }
    }
    return grain / sqrt(max(norm, 1.0e-12));
}

// film_tile_grain
float film_tile_grain(float gross, int px, int py, int record, uint seed) {
    int base = CFG_FILM_TILE;
    float d_min = cfg(base + FILM_TILE_DMIN + record);
    float d_max = cfg(base + FILM_TILE_DMAX + record);
    int table = base + FILM_TILE_TABLES + record * 3 * FILM_TILE_LEVELS;
    float t = clamp((gross - d_min) / max(d_max - d_min, 1.0e-6), 0.0, 1.0)
        * float(FILM_TILE_LEVELS - 1);
    int k = clamp(int(floor(t)), 0, FILM_TILE_LEVELS - 2);
    float w = t - float(k);
    float grain = film_tile_laying(0, px, py, record, seed, table, k, w);
    for (int layer = 1; layer < FILM_TILE_LAYERS; ++layer) {
        grain = grain + film_tile_laying(layer, px, py, record, seed, table, k, w);
    }
    float rho = cfg(table + 2 * FILM_TILE_LEVELS + k);
    float kept = (1.0 - w) * (1.0 - w) + w * w + 2.0 * w * (1.0 - w) * rho;
    return cfg(base + FILM_TILE_AMOUNT + record) * grain
        / sqrt(max(kept * float(FILM_TILE_LAYERS), 1.0e-12));
}
"""

    /**
     * Pyramid/grid reads of the scattered stages (GpuBackend::scattered `read_scale`), the
     * decimated-Gaussian upsample (`gpu_gaussian_on_grid`) and Sampling.h `annular_sample`.
     * Grid cells are addressed in frame cells; `origin` is where the grid's own lattice starts.
     */
    private const val GRIDS = """
const float RING_DX[16] = float[16](
     1.000000000,  0.923879533,  0.707106781,  0.382683432,
     0.000000000, -0.382683432, -0.707106781, -0.923879533,
    -1.000000000, -0.923879533, -0.707106781, -0.382683432,
     0.000000000,  0.382683432,  0.707106781,  0.923879533);
const float RING_DY[16] = float[16](
     0.000000000,  0.382683432,  0.707106781,  0.923879533,
     1.000000000,  0.923879533,  0.707106781,  0.382683432,
     0.000000000, -0.382683432, -0.707106781, -0.923879533,
    -1.000000000, -0.923879533, -0.707106781, -0.382683432);

bool grid_inside(ivec2 origin, ivec2 size, int cx, int cy) {
    int lx = cx - origin.x;
    int ly = cy - origin.y;
    return lx >= 0 && lx < size.x && ly >= 0 && ly < size.y;
}

// grid_at
vec4 grid_at(highp sampler2D grid, ivec2 origin, ivec2 size, int cx, int cy) {
    return grid_inside(origin, size, cx, cy)
        ? texelFetch(grid, ivec2(cx - origin.x, cy - origin.y), 0) : vec4(0.0);
}

// grid_valid
float grid_valid(ivec2 origin, ivec2 size, int cx, int cy) {
    return grid_inside(origin, size, cx, cy) ? 1.0 : 0.0;
}

// bilinear_sample over grid_at
vec4 bilinear_grid(highp sampler2D grid, ivec2 origin, ivec2 size, float px, float py,
                   bool separate_weights) {
    int x0 = int(floor(px));
    int y0 = int(floor(py));
    float fx = px - floor(px);
    float fy = py - floor(py);
    vec4 s00 = grid_at(grid, origin, size, x0, y0);
    vec4 s01 = grid_at(grid, origin, size, x0, y0 + 1);
    vec4 s10 = grid_at(grid, origin, size, x0 + 1, y0);
    vec4 s11 = grid_at(grid, origin, size, x0 + 1, y0 + 1);
    if (separate_weights) {
        float w00 = (1.0 - fx) * (1.0 - fy);
        float w01 = (1.0 - fx) * fy;
        float w10 = fx * (1.0 - fy);
        float w11 = fx * fy;
        return w00 * s00 + w01 * s01 + w10 * s10 + w11 * s11;
    }
    return (1.0 - fx) * ((1.0 - fy) * s00 + fy * s01) + fx * ((1.0 - fy) * s10 + fy * s11);
}

// bilinear_sample over grid_valid
float bilinear_valid(ivec2 origin, ivec2 size, float px, float py, bool separate_weights) {
    int x0 = int(floor(px));
    int y0 = int(floor(py));
    float fx = px - floor(px);
    float fy = py - floor(py);
    float s00 = grid_valid(origin, size, x0, y0);
    float s01 = grid_valid(origin, size, x0, y0 + 1);
    float s10 = grid_valid(origin, size, x0 + 1, y0);
    float s11 = grid_valid(origin, size, x0 + 1, y0 + 1);
    if (separate_weights) {
        float w00 = (1.0 - fx) * (1.0 - fy);
        float w01 = (1.0 - fx) * fy;
        float w10 = fx * (1.0 - fy);
        float w11 = fx * fy;
        return w00 * s00 + w01 * s01 + w10 * s10 + w11 * s11;
    }
    return (1.0 - fx) * ((1.0 - fy) * s00 + fy * s01) + fx * ((1.0 - fy) * s10 + fy * s11);
}

// GpuBackend::scattered read_scale (centre, or annular_sample when `annular`)
vec4 read_scale(highp sampler2D grid, ivec2 origin, ivec2 size, int stride, int frame_x,
                int frame_y, bool separate_weights, bool annular, float ring_config) {
    float sample_x = (float(frame_x) + 0.5) / float(stride) - 0.5;
    float sample_y = (float(frame_y) + 0.5) / float(stride) - 0.5;
    vec4 center = bilinear_grid(grid, origin, size, sample_x, sample_y, separate_weights)
        / max(bilinear_valid(origin, size, sample_x, sample_y, separate_weights), 1.0e-12);
    if (!annular) return center;
    float ring_radius = ring_config / float(stride);
    vec4 ring = vec4(0.0);
    float ring_valid = 0.0;
    for (int d = 0; d < 16; ++d) {
        float rx = sample_x + ring_radius * RING_DX[d];
        float ry = sample_y + ring_radius * RING_DY[d];
        ring += bilinear_grid(grid, origin, size, rx, ry, separate_weights);
        ring_valid += bilinear_valid(origin, size, rx, ry, separate_weights);
    }
    vec4 ring_data = ring_radius > 1.0e-4 ? ring * (1.0 / 16.0)
        : bilinear_grid(grid, origin, size, sample_x, sample_y, separate_weights);
    float ring_weight = ring_radius > 1.0e-4 ? ring_valid * (1.0 / 16.0)
        : bilinear_valid(origin, size, sample_x, sample_y, separate_weights);
    return ring_data / max(ring_weight, 1.0e-12);
}

// gpu_gaussian_on_grid upsample
vec4 grid_upsample(highp sampler2D grid, ivec2 size, int stride, ivec2 phase, ivec2 p) {
    float sample_x = (float(p.x + phase.x) + 0.5) / float(stride) - 0.5;
    float sample_y = (float(p.y + phase.y) + 0.5) / float(stride) - 0.5;
    int x0 = int(floor(sample_x));
    int y0 = int(floor(sample_y));
    float fx = sample_x - floor(sample_x);
    float fy = sample_y - floor(sample_y);
    float w00 = (1.0 - fx) * (1.0 - fy);
    float w01 = (1.0 - fx) * fy;
    float w10 = fx * (1.0 - fy);
    float w11 = fx * fy;
    ivec2 zero = ivec2(0);
    float sample_weight = w00 * grid_valid(zero, size, x0, y0)
        + w01 * grid_valid(zero, size, x0, y0 + 1)
        + w10 * grid_valid(zero, size, x0 + 1, y0)
        + w11 * grid_valid(zero, size, x0 + 1, y0 + 1);
    return (w00 * grid_at(grid, zero, size, x0, y0)
            + w01 * grid_at(grid, zero, size, x0, y0 + 1)
            + w10 * grid_at(grid, zero, size, x0 + 1, y0)
            + w11 * grid_at(grid, zero, size, x0 + 1, y0 + 1))
        / max(sample_weight, 1.0e-12);
}
"""

    private const val IMAGE_MAIN_GUARD = """
uniform ivec2 uSize;
// First row of the band this dispatch covers (FotufilmRenderer dispatches image passes in bands).
uniform int uBandRow;
ivec2 pixel() { return ivec2(gl_GlobalInvocationID.xy) + ivec2(0, uBandRow); }
bool outside(ivec2 p) { return p.x >= uSize.x || p.y >= uSize.y; }
"""

    private fun compute(localX: Int, localY: Int, vararg sections: String): String = buildString {
        append("#version 310 es\n")
        append(COMPUTE_PRECISION)
        append("layout(local_size_x = ").append(localX).append(", local_size_y = ").append(localY)
            .append(") in;\n")
        append(DEFINES)
        for (section in sections) append(section)
    }

    private fun image(vararg sections: String) = compute(8, 8, CORE, IMAGE_MAIN_GUARD, *sections)

    /** Stage 1 (`scene_exposure`): input scene light to per-record exposure, donor in alpha. */
    val EXPOSURE = image(FILM, OPTICS, LUT, """
uniform highp sampler2D uInput;
uniform highp sampler3D uExposureLut;
uniform ivec2 uInputOffset;
uniform ivec2 uOrigin;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 source = texelFetch(uInput, p + uInputOffset, 0);
    imageStore(uOut, p, scene_exposure(uExposureLut, source, p.x + uOrigin.x, p.y + uOrigin.y));
}
""")

    /** `gpu_decimate_level` / `decimated_grid`: box average over factor² cells, edge cells over what they hold. */
    val DECIMATE = image("""
uniform highp sampler2D uSource;
uniform ivec2 uSourceSize;
uniform int uFactor;
uniform ivec2 uOffset;
uniform ivec2 uOutOffset;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 total = vec4(0.0);
    float count = 0.0;
    for (int cy = 0; cy < uFactor; ++cy) {
        for (int cx = 0; cx < uFactor; ++cx) {
            int sx = p.x * uFactor - uOffset.x + cx;
            int sy = p.y * uFactor - uOffset.y + cy;
            bool valid = sx >= 0 && sx < uSourceSize.x && sy >= 0 && sy < uSourceSize.y;
            total += valid ? texelFetch(uSource, ivec2(sx, sy), 0) : vec4(0.0);
            count += valid ? 1.0 : 0.0;
        }
    }
    imageStore(uOut, p + uOutOffset, total / max(count, 1.0));
}
""")

    /** `gpu_gaussian` kernel table, normalised per channel (native GPU: weight / total). */
    val GAUSSIAN_KERNEL = compute(128, 1, CORE, """
uniform vec4 uSigma;
uniform int uRadius;
uniform int uRadius3;
uniform int uMerged;
uniform int uExtent;
uniform int uDecimateStride;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
// decimated_gaussian_sigma
float decimated_gaussian_sigma(float sigma, int stride) {
    return stride == 1 ? sigma
        : sqrt(max(sigma * sigma / float(stride * stride) - 0.25, 0.0625));
}
float kernel_value(int k, int channel) {
    float sigma = uSigma[channel];
    if (uDecimateStride > 0) sigma = decimated_gaussian_sigma(sigma, uDecimateStride);
    float denominator = 2.0 * sigma * sigma;
    float weight = exp(-float(k * k) / denominator);
    if (uMerged != 0) {
        int window = channel == 3 ? uRadius3 : uRadius;
        float total = 0.0;
        for (int t = -uExtent; t <= uExtent; ++t) {
            total += abs(t) <= window ? exp(-float(t * t) / denominator) : 0.0;
        }
        return abs(k) <= window ? weight / total : 0.0;
    }
    float total = 0.0;
    for (int t = -uExtent; t <= uExtent; ++t) total += exp(-float(t * t) / denominator);
    return weight / total;
}
void main() {
    int index = int(gl_GlobalInvocationID.x);
    if (index > 2 * uExtent) return;
    int k = index - uExtent;
    imageStore(uOut, ivec2(index, 0),
        vec4(kernel_value(k, 0), kernel_value(k, 1), kernel_value(k, 2), kernel_value(k, 3)));
}
""")

    /** `gpu_triple_box_blur` kernel: three chained boxes folded into one (native GPU count form). */
    val BOX_KERNEL = compute(128, 1, CORE, """
uniform int uRadius;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    int index = int(gl_GlobalInvocationID.x);
    if (index > 6 * uRadius) return;
    int k = index - 3 * uRadius;
    float box_scale = 1.0 / float(uRadius * 2 + 1);
    float count = 0.0;
    for (int fold_y = -uRadius; fold_y <= uRadius; ++fold_y) {
        for (int fold_x = -uRadius; fold_x <= uRadius; ++fold_x) {
            count += abs(k - fold_x - fold_y) <= uRadius ? 1.0 : 0.0;
        }
    }
    imageStore(uOut, ivec2(index, 0), vec4(count * box_scale * box_scale * box_scale));
}
""")

    /** One direction of `gpu_gaussian` / `gpu_triple_box_blur`: `normalized_taps` over the frame. */
    val BLUR = image("""
uniform highp sampler2D uSource;
uniform highp sampler2D uKernel;
uniform int uExtent;
uniform int uVertical;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 data = vec4(0.0);
    vec4 weight = vec4(0.0);
    for (int t = -uExtent; t <= uExtent; ++t) {
        ivec2 q = uVertical != 0 ? ivec2(p.x, p.y + t) : ivec2(p.x + t, p.y);
        bool inside = uVertical != 0 ? (q.y >= 0 && q.y < uSize.y) : (q.x >= 0 && q.x < uSize.x);
        vec4 kern = texelFetch(uKernel, ivec2(t + uExtent, 0), 0);
        vec4 value = inside ? texelFetch(uSource, q, 0) : vec4(0.0);
        data += value * kern;
        weight += inside ? kern : vec4(0.0);
    }
    imageStore(uOut, p, data / max(weight, vec4(1.0e-12)));
}
""")

    /** `diffused` (diffusion_mix over the lens-diffusion pyramid, bilinear separate weights). */
    val DIFFUSION_MIX = image(OPTICS, GRIDS, """
uniform highp sampler2D uBase;
uniform highp sampler2D uGrid0;
uniform highp sampler2D uGrid1;
uniform highp sampler2D uGrid2;
uniform ivec2 uGridOrigin[3];
uniform ivec2 uGridSize[3];
uniform int uStride[3];
uniform ivec2 uOrigin;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    int fx = p.x + uOrigin.x;
    int fy = p.y + uOrigin.y;
    vec4 base = texelFetch(uBase, p, 0);
    vec4 s0 = read_scale(uGrid0, uGridOrigin[0], uGridSize[0], uStride[0], fx, fy, true, false, 0.0);
    vec4 s1 = read_scale(uGrid1, uGridOrigin[1], uGridSize[1], uStride[1], fx, fy, true, false, 0.0);
    vec4 s2 = read_scale(uGrid2, uGridOrigin[2], uGridSize[2], uStride[2], fx, fy, true, false, 0.0);
    vec4 result;
    for (int c = 0; c < 4; ++c) result[c] = diffusion_mix(c, base[c], s0[c], s1[c], s2[c]);
    imageStore(uOut, p, result);
}
""")

    /** `frame_mean` spans: 64-pixel row spans of the widened lens light. */
    val FLARE_SPANS = image("""
uniform highp sampler2D uLens;
uniform int uWidth;
uniform int uRowStart;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
vec4 widened(ivec2 q) {
    vec4 lens = texelFetch(uLens, q, 0);
    return vec4(lens.xyz, (lens.x + lens.y + lens.z) / 3.0);
}
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    int y = p.y + uRowStart;
    vec4 total = vec4(0.0);
    for (int within = 0; within < 64; ++within) {
        int column = p.x * 64 + within;
        total += column < uWidth ? widened(ivec2(min(column, uWidth - 1), y)) : vec4(0.0);
    }
    imageStore(uOut, p, total);
}
""")

    /** `frame_mean` rows: the spans of each row summed in order. */
    val FLARE_ROWS = compute(128, 1, CORE, """
uniform highp sampler2D uSpans;
uniform int uSpanCount;
uniform int uRows;
uniform int uOutRow;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    int y = int(gl_GlobalInvocationID.x);
    if (y >= uRows) return;
    vec4 total = vec4(0.0);
    for (int across = 0; across < uSpanCount; ++across) total += texelFetch(uSpans, ivec2(across, y), 0);
    imageStore(uOut, ivec2(0, y + uOutRow), total);
}
""")

    /** `frame_mean` total: rows summed in order over width × height. */
    val FLARE_TOTAL = compute(128, 1, CORE, """
uniform highp sampler2D uRowSums;
uniform int uWidth;
uniform int uHeight;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    if (gl_GlobalInvocationID.x != 0u) return;
    vec4 total = vec4(0.0);
    for (int y = 0; y < uHeight; ++y) total += texelFetch(uRowSums, ivec2(0, y), 0);
    imageStore(uOut, ivec2(0, 0), total / (float(uWidth) * float(uHeight)));
}
""")

    /**
     * The light through the camera gate (`through_gate`, merged-luma `widened_light`) and its
     * veiling glare (`flared`). Alpha carries the gated record neutral when the MTF is merged.
     */
    val LIGHT = image(OPTICS, """
uniform highp sampler2D uLens;
uniform highp sampler2D uFlareMean;
uniform ivec2 uOrigin;
uniform int uMerged;
uniform int uFlare;
uniform int uFlareMeasured;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 lens = texelFetch(uLens, p, 0);
    float gate_pass = gate_transmission(p.x + uOrigin.x, p.y + uOrigin.y);
    vec4 light = vec4(lens.x * gate_pass, lens.y * gate_pass, lens.z * gate_pass, 0.0);
    if (uMerged != 0) light.w = ((lens.x + lens.y + lens.z) / 3.0) * gate_pass;
    if (uFlare != 0) {
        vec4 mean;
        if (uFlareMeasured != 0) {
            mean = texelFetch(uFlareMean, ivec2(0, 0), 0);
        } else {
            float m0 = cfg(CFG_FLARE_MEAN);
            float m1 = cfg(CFG_FLARE_MEAN + 1);
            float m2 = cfg(CFG_FLARE_MEAN + 2);
            mean = vec4(m0, m1, m2, (m0 + m1 + m2) / 3.0);
        }
        float fraction = cfg(CFG_FLARE);
        for (int c = 0; c < 4; ++c) light[c] = veiling_glare(light[c], gate_pass * mean[c], fraction);
    }
    imageStore(uOut, p, light);
}
""")

    /** `mtf_mixed` + `mtf_separated`: the two scales blended, then the luminance arm. */
    val MTF_SEPARATE = image(OPTICS, """
uniform highp sampler2D uPerLayer;
uniform highp sampler2D uSecondary;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 per_layer = texelFetch(uPerLayer, p, 0);
    vec4 secondary = texelFetch(uSecondary, p, 0);
    vec3 mixed;
    for (int c = 0; c < 3; ++c) {
        float primary_share = cfg(CFG_MTF_PRIMARY_SHARE + c);
        mixed[c] = primary_share * per_layer[c] + (1.0 - primary_share) * secondary[c];
    }
    float neutral = (mixed.x + mixed.y + mixed.z) / 3.0;
    vec4 separated = vec4(0.0);
    for (int c = 0; c < 3; ++c) separated[c] = mtf_luma_mix(mixed[c], neutral, per_layer.w);
    imageStore(uOut, p, separated);
}
""")

    /** `halation_returned` + `halated` over the three scales (bilinear, optional annulus). */
    val HALATION = image(OPTICS, GRIDS, """
uniform highp sampler2D uLight;
uniform highp sampler2D uGrid0;
uniform highp sampler2D uGrid1;
uniform highp sampler2D uGrid2;
uniform ivec2 uGridOrigin[3];
uniform ivec2 uGridSize[3];
uniform int uStride[3];
uniform int uAnnular;
uniform ivec2 uOrigin;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    int fx = p.x + uOrigin.x;
    int fy = p.y + uOrigin.y;
    bool annular = uAnnular != 0;
    vec4 light = texelFetch(uLight, p, 0);
    vec4 s0 = read_scale(uGrid0, uGridOrigin[0], uGridSize[0], uStride[0], fx, fy, false, annular,
        cfg(CFG_HALATION_RING_RADIUS));
    vec4 s1 = read_scale(uGrid1, uGridOrigin[1], uGridSize[1], uStride[1], fx, fy, false, annular,
        cfg(CFG_HALATION_RING_RADIUS + 1));
    vec4 s2 = read_scale(uGrid2, uGridOrigin[2], uGridSize[2], uStride[2], fx, fy, false, annular,
        cfg(CFG_HALATION_RING_RADIUS + 2));
    vec3 returned;
    for (int j = 0; j < 3; ++j) returned[j] = halation_returned(j, light[j], s0[j], s1[j], s2[j]);
    vec4 halated = vec4(0.0);
    for (int c = 0; c < 3; ++c) halated[c] = halation_mix(c, light[c], returned);
    imageStore(uOut, p, halated);
}
""")

    /**
     * `log_exposure`, `activation`, `released`, and the donor layer's `donor_released` (alpha of
     * the released image, so the coupler field's decimated blur carries the donor's alongside).
     */
    val ACTIVATION = image(FILM, OPTICS, """
uniform highp sampler2D uLight;
uniform highp sampler2D uLens;
uniform ivec2 uOrigin;
uniform int uDonor;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uLog;
layout(rgba32f, binding = 1) writeonly uniform highp image2D uActivation;
layout(rgba32f, binding = 2) writeonly uniform highp image2D uReleased;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 light = texelFetch(uLight, p, 0);
    vec4 log_exposure = vec4(0.0);
    vec4 activation = vec4(0.0);
    vec4 released = vec4(0.0);
    for (int c = 0; c < 3; ++c) {
        log_exposure[c] = fs_log10(max(light[c], 1.0e-6));
        activation[c] = film_activation(c, film_density(c, log_exposure[c]));
        released[c] = coupler_release(c, activation[c]);
    }
    if (uDonor != 0) {
        float gate_pass = gate_transmission(p.x + uOrigin.x, p.y + uOrigin.y);
        float donor_exposure = texelFetch(uLens, p, 0).w * gate_pass;
        float donor_log = fs_log10(max(donor_exposure, 1.0e-6));
        float donor_activation = (curve_density(CFG_DONOR_CURVE, donor_log) - cfg(CFG_DONOR_CURVE))
            / max(curve_range(CFG_DONOR_CURVE), 1.0e-6);
        released.w = donor_release(donor_activation);
    }
    imageStore(uLog, p, log_exposure);
    imageStore(uActivation, p, activation);
    imageStore(uReleased, p, released);
}
""")

    /** `inhibited`, `inhibited_selected`, `density` (developed_from with adjacency). */
    val DENSITY = image(FILM, GRIDS, """
uniform highp sampler2D uLight;
uniform highp sampler2D uLog;
uniform highp sampler2D uActivation;
uniform highp sampler2D uReleased;
uniform highp sampler2D uCouplerGrid;
uniform highp sampler2D uFringeGrid;
uniform highp sampler2D uAdjacencyGrid;
uniform highp sampler2D uAdjacencySecondaryGrid;
// Per decimated field (coupler, fringe, adjacency, adjacency secondary): grid size, stride, phase.
uniform ivec2 uFieldSize[4];
uniform int uFieldStride[4];
uniform ivec2 uFieldPhase[4];
uniform int uSpatial;
uniform int uCouplers;
uniform int uCouplerDiffusion;
uniform int uFringe;
uniform int uFringeRadius;
uniform int uAdjacency;
uniform int uDonor;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 log_view;
    vec4 activation = vec4(0.0);
    vec4 released = vec4(0.0);
    if (uSpatial != 0) {
        log_view = texelFetch(uLog, p, 0);
        activation = texelFetch(uActivation, p, 0);
        released = texelFetch(uReleased, p, 0);
    } else {
        vec4 light = texelFetch(uLight, p, 0);
        log_view = vec4(fs_log10(max(light.x, 1.0e-6)), fs_log10(max(light.y, 1.0e-6)),
                        fs_log10(max(light.z, 1.0e-6)), 0.0);
    }
    vec4 coupler_diffused = released;
    vec4 fringe_diffused = released;
    if (uCouplerDiffusion != 0) {
        coupler_diffused = grid_upsample(uCouplerGrid, uFieldSize[0], uFieldStride[0], uFieldPhase[0], p);
        if (uFringe != 0) {
            fringe_diffused = grid_upsample(uFringeGrid, uFieldSize[1], uFieldStride[1], uFieldPhase[1], p);
        }
    }
    vec4 adjacency_diffused = vec4(0.0);
    vec4 adjacency_secondary = vec4(0.0);
    if (uAdjacency != 0) {
        adjacency_diffused = grid_upsample(uAdjacencyGrid, uFieldSize[2], uFieldStride[2], uFieldPhase[2], p);
        adjacency_secondary = grid_upsample(uAdjacencySecondaryGrid, uFieldSize[3], uFieldStride[3],
            uFieldPhase[3], p);
    }
    // Donor diffusion shares the coupler field's sigma and radius and rides in its alpha.
    float donor_diffused = uCouplerDiffusion != 0 ? coupler_diffused.w : released.w;
    vec4 density = vec4(0.0);
    for (int c = 0; c < 3; ++c) {
        float effective = log_view[c];
        float residual = 0.0;
        if (uSpatial != 0) {
            float inhibited = log_view[c];
            if (uCouplers != 0 || uDonor != 0) {
                float inhibition;
                if (uCouplers != 0) {
                    float coupler_term = coupler_inhibition(c, coupler_diffused.x, coupler_diffused.y,
                        coupler_diffused.z);
                    if (uFringe != 0) {
                        coupler_term = fringe_inhibition(c, coupler_term, uFringeRadius,
                            fringe_diffused.x - coupler_diffused.x,
                            fringe_diffused.y - coupler_diffused.y,
                            fringe_diffused.z - coupler_diffused.z);
                    }
                    inhibition = coupler_term;
                    if (uDonor != 0) inhibition = inhibition + donor_inhibition(c, donor_diffused);
                } else {
                    inhibition = donor_inhibition(c, donor_diffused);
                }
                inhibited = inhibited_log_exposure(c, log_view[c], inhibition);
            }
            effective = inhibited;
            if (uAdjacency != 0) {
                residual = activation[c] - adjacency_transport(adjacency_diffused[c], adjacency_secondary[c]);
                effective = inhibited - adjacency_shift(residual);
            }
        }
        float formed = film_density(c, effective);
        if (uAdjacency != 0) formed = adjacency_density(c, formed, residual);
        density[c] = developed_density(c, formed);
    }
    imageStore(uOut, p, density);
}
""")

    /** `poisson_noise` / `mottle_noise` (GpuBackend::grain_fields, analytic seeded draws). */
    val GRAIN_NOISE = image(GRAIN, """
uniform ivec2 uOrigin;
uniform uint uSeed;
uniform float uGrainLambda;
uniform float uMottleLambda;
uniform int uMonochrome;
uniform int uMottle;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uNoise;
layout(rgba32f, binding = 1) writeonly uniform highp image2D uMottleNoise;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    int x = p.x + uOrigin.x;
    int y = p.y + uOrigin.y;
    vec4 noise = vec4(0.0);
    if (uMonochrome != 0) {
        noise.x = normal_sample(x, y, uSeed, 3);
    } else {
        float common_draw = poisson_sample(x, y, uSeed, uGrainLambda, 3);
        for (int c = 0; c < 3; ++c) {
            noise[c] = grain_mix(poisson_sample(x, y, uSeed, uGrainLambda, c), common_draw);
        }
    }
    imageStore(uNoise, p, noise);
    if (uMottle != 0) {
        vec4 mottle = vec4(0.0);
        if (uMonochrome != 0) {
            mottle.x = normal_sample(x, y, uSeed, 7);
        } else {
            float common_draw = poisson_sample(x, y, uSeed, uMottleLambda, 7);
            for (int c = 0; c < 3; ++c) {
                mottle[c] = grain_mix(poisson_sample(x, y, uSeed, uMottleLambda, c + 4), common_draw);
            }
        }
        imageStore(uMottleNoise, p, mottle);
    }
}
""")

    /** `grained` (clump grain on the developed density). */
    val GRAIN_APPLY = image(FILM, GRAIN, """
uniform highp sampler2D uDensity;
uniform highp sampler2D uGrainField;
uniform highp sampler2D uMottleField;
uniform int uMonochrome;
uniform int uMottle;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 density = texelFetch(uDensity, p, 0);
    vec4 grain = texelFetch(uGrainField, p, 0);
    vec4 mottle = uMottle != 0 ? texelFetch(uMottleField, p, 0) : vec4(0.0);
    vec4 developed = vec4(0.0);
    for (int c = 0; c < 3; ++c) {
        int layer = uMonochrome != 0 ? 0 : c;
        float modulation = grain_density_modulation(c, density_position_net(c, density[c]));
        float clump = cfg(CFG_GRAIN + c) * modulation * grain[layer];
        if (uMottle != 0) clump = clump + cfg(CFG_MOTTLE + c) * modulation * mottle[layer];
        developed[c] = density[c] + clump;
    }
    imageStore(uOut, p, developed);
}
""")

    /** `film_grain` + `filmed` (grain mode 1), with the colour-grain mix. */
    val FILM_GRAIN = image(FILM_TILES, """
uniform highp sampler2D uDensity;
uniform ivec2 uOrigin;
uniform uint uSeed;
uniform int uMonochrome;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    int x = p.x + uOrigin.x;
    int y = p.y + uOrigin.y;
    vec4 density = texelFetch(uDensity, p, 0);
    vec3 film;
    for (int c = 0; c < 3; ++c) {
        int record = uMonochrome != 0 ? 1 : c;
        film[c] = film_tile_grain(density[c], x, y, record, uSeed);
    }
    float colour = clamp(cfg(CFG_FILM_TILE + FILM_TILE_COLOUR), 0.0, 1.0);
    float common_share = sqrt(3.0 - 2.0 * colour * colour) - colour;
    float mean = (film.x + film.y + film.z) / 3.0;
    vec4 developed = vec4(0.0);
    for (int c = 0; c < 3; ++c) {
        float mixed = uMonochrome != 0 ? film[c] : colour * film[c] + common_share * mean;
        developed[c] = density[c] + mixed;
    }
    imageStore(uOut, p, developed);
}
""")

    /** `in_carrier` + `print_mtf_transmittance`. */
    val TRANSMITTANCE = image(OPTICS, """
uniform highp sampler2D uDeveloped;
uniform ivec2 uOrigin;
layout(rgba32f, binding = 0) writeonly uniform highp image2D uOut;
void main() {
    ivec2 p = pixel();
    if (outside(p)) return;
    vec4 developed = texelFetch(uDeveloped, p, 0);
    float carried = carrier_density(p.x + uOrigin.x, p.y + uOrigin.y);
    vec4 transmittance = vec4(0.0);
    for (int c = 0; c < 3; ++c) transmittance[c] = transmittance_of(developed[c] + carried);
    imageStore(uOut, p, transmittance);
}
""")

    /**
     * `printed` (print MTF read) or `in_carrier`, then `build_print` and the float delivery:
     * display-linear Display P3, or the host encode when FOTUFILM_FRAME_ENCODE_OUT is set.
     * Also the `FOTUFILM_FRAME_NO_FILM` print (`plain_print`).
     */
    val PRINT: String = buildString {
        append("#version 300 es\n")
        append(FRAGMENT_PRECISION)
        append(DEFINES)
        append(CORE)
        append(FILM)
        append(OPTICS)
        append(LUT)
        append("""
in vec2 vTexCoord;
out vec4 fragColor;
uniform highp sampler2D uInput;
uniform highp sampler2D uDeveloped;
uniform highp sampler2D uTransmittance;
uniform highp sampler2D uSpread;
uniform highp sampler3D uFilmLut;
uniform highp sampler3D uPaperLut;
uniform ivec2 uInputOffset;
uniform ivec2 uOrigin;
uniform int uNoFilm;
uniform int uPrintMtf;
uniform int uReversal;
uniform int uMonochrome;
uniform int uFinish;
uniform int uEncode;
// -1: coefficient-driven shape; 0 linear, 1 power, 2 logarithmic.
uniform int uShape;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int fx = p.x + uOrigin.x;
    int fy = p.y + uOrigin.y;
    vec4 source = texelFetch(uInput, p + uInputOffset, 0);
    vec3 printed;
    if (uNoFilm != 0) {
        vec3 scene = creative_exposure(source.x, source.y, source.z, fx, fy);
        float neutral = LUMA_R * scene.x + LUMA_G * scene.y + LUMA_B * scene.z;
        vec3 delivered = uMonochrome != 0 ? vec3(neutral) : scene;
        for (int c = 0; c < 3; ++c) printed[c] = plain_print(delivered, c);
    } else {
        vec3 developed;
        if (uPrintMtf != 0) {
            vec4 transmittance = texelFetch(uTransmittance, p, 0);
            vec4 spread = texelFetch(uSpread, p, 0);
            for (int c = 0; c < 3; ++c) {
                developed[c] = density_of(print_mtf_read(transmittance[c], spread[c]));
            }
        } else {
            vec4 density = texelFetch(uDeveloped, p, 0);
            float carried = carrier_density(fx, fy);
            developed = vec3(density.x + carried, density.y + carried, density.z + carried);
        }
        vec3 activation;
        for (int c = 0; c < 3; ++c) activation[c] = film_activation(c, developed[c]);
        vec3 relative = lut_sample(uFilmLut, activation.x, activation.y, activation.z).xyz;
        vec3 display;
        if (uReversal != 0) {
            display = relative;
        } else {
            vec3 activated;
            for (int c = 0; c < 3; ++c) activated[c] = paper_activation(c, relative[c]);
            display = lut_sample(uPaperLut, activated.x, activated.y, activated.z).xyz;
        }
        if (uFinish != 0) display = print_finish(display);
        vec3 composed = uMonochrome != 0 ? vec3((display.x + display.y + display.z) / 3.0) : display;
        for (int c = 0; c < 3; ++c) printed[c] = color_grade(c, composed[c]);
    }
    if (uEncode != 0) {
        float r = max(printed.x, 0.0);
        float g = max(printed.y, 0.0);
        float b = max(printed.z, 0.0);
        float alpha = source.w;
        bool premultiply = cfg(CFG_OUTPUT_PREMULTIPLIED) != 0.0 && alpha != 1.0;
        int shape = uShape >= 0 ? uShape : host_transfer_shape();
        vec3 encoded;
        for (int row = 0; row < 3; ++row) {
            float value = host_transfer_encode_shaped(host_output_shouldered(r, g, b, row), shape);
            encoded[row] = premultiply ? value * alpha : value;
        }
        fragColor = vec4(encoded, alpha);
    } else {
        fragColor = vec4(max(printed, vec3(0.0)), source.w);
    }
}
""")
    }
}
