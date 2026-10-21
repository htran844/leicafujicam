package vn.hoang.leicafujicam.raw.fotufilm

import vn.hoang.leicafujicam.raw.fotufilm.FotufilmConfigLayout as L
import kotlin.math.max
import kotlin.math.sqrt

/**
 * Host-side scalars of one frame: upstream `ResolvedFrameParams` (FotufilmResolvedFrameParams.h)
 * plus the stride/radius rules of `include/FotufilmHalide.h` and `FotufilmHalideGeometry.h`.
 * These are computed on the host upstream as well, so they are evaluated here in Float/Int with
 * the same expressions.
 */
internal class FotufilmFrameParams(configuration: FloatArray) {
    private fun sigma(offset: Int, c: FloatArray) = max(c[offset], 0.151f)
    private fun radius(offset: Int, c: FloatArray) = max(0, c[offset].toInt())

    val mtfSigma = FloatArray(3) { sigma(L.CONFIG_MTF_SIGMA + it, configuration) }
    val mtfRadius = IntArray(3) { radius(L.CONFIG_MTF_RADIUS + it, configuration) }
    val mtfLumaSigma = sigma(L.CONFIG_MTF_LUMA_SIGMA, configuration)
    val mtfLumaRadius = maxOf(
        0,
        configuration[L.CONFIG_MTF_LUMA_RADIUS].toInt(),
        configuration[L.CONFIG_MTF_SECONDARY_RADIUS].toInt(),
        configuration[L.CONFIG_MTF_SECONDARY_RADIUS + 1].toInt(),
        configuration[L.CONFIG_MTF_SECONDARY_RADIUS + 2].toInt(),
    )
    val halationRadius = IntArray(3) { radius(L.CONFIG_HALATION_RADIUS + it, configuration) }
    val halationStride = IntArray(3) { halationStride(halationRadius[it]) }
    val halationStridedRadius = IntArray(3) {
        halationStridedRadius(halationRadius[it], halationStride[it])
    }
    val diffusionRadius = IntArray(3) { radius(L.CONFIG_DIFFUSION_RADIUS + it, configuration) }
    val diffusionStride = IntArray(3) { diffusionStride(diffusionRadius[it]) }
    val diffusionStridedRadius = IntArray(3) {
        halationStridedRadius(diffusionRadius[it], diffusionStride[it])
    }
    val couplerSigma = sigma(L.CONFIG_COUPLER_SIGMA, configuration)
    val couplerRadius = radius(L.CONFIG_COUPLER_RADIUS, configuration)
    val adjacencySigma = sigma(L.CONFIG_ADJACENCY_SIGMA, configuration)
    val adjacencyRadius = radius(L.CONFIG_ADJACENCY_RADIUS, configuration)
    val adjacencySecondarySigma = sigma(L.CONFIG_ADJACENCY_SECONDARY_SIGMA, configuration)
    val adjacencySecondaryRadius = radius(L.CONFIG_ADJACENCY_SECONDARY_RADIUS, configuration)
    val fringeSigma = sigma(L.CONFIG_CHROMATIC_FRINGE_SIGMA, configuration)
    val fringeRadius = radius(L.CONFIG_CHROMATIC_FRINGE_RADIUS, configuration)
    val grainRadius = radius(L.CONFIG_GRAIN_RADIUS, configuration)
    val grainLambda = configuration[L.CONFIG_GRAIN_LAMBDA]
    val mottleRadius = radius(L.CONFIG_MOTTLE_RADIUS, configuration)
    val mottleLambda = configuration[L.CONFIG_MOTTLE_LAMBDA]
    val printMtfRadius = radius(L.CONFIG_PRINT_MTF_RADIUS, configuration)

    companion object {
        /** fotufilm_halation_box_sigma */
        fun halationBoxSigma(radius: Int): Float {
            val width = (2 * radius + 1).toFloat()
            return sqrt((width * width - 1.0f) * 0.25f)
        }

        /** fotufilm_halation_stride */
        fun halationStride(radius: Int): Int {
            val sigma = halationBoxSigma(radius)
            var stride = 1
            while (stride < 8 && (2 * stride).toFloat() * 2.5f <= sigma) stride *= 2
            return stride
        }

        /** fotufilm_diffusion_stride */
        fun diffusionStride(radius: Int): Int {
            val sigma = halationBoxSigma(radius)
            var stride = 1
            while (stride < 64 && (2 * stride).toFloat() * 2.5f <= sigma) stride *= 2
            return stride
        }

        /** fotufilm_halation_strided_radius */
        fun halationStridedRadius(radius: Int, stride: Int): Int {
            if (stride <= 1) return radius
            val sigma = halationBoxSigma(radius)
            var variance = sigma * sigma / (stride * stride).toFloat() - 0.25f
            if (variance < 0.25f) variance = 0.25f
            val width = sqrt(4.0f * variance + 1.0f)
            val scaled = ((width - 1.0f) * 0.5f + 0.5f).toInt()
            return if (scaled < 1) 1 else scaled
        }

        /** gaussian_grid_stride (Stages/Halation.h gaussian_stride) */
        fun gaussianStride(sigma: Float): Int = when {
            sigma >= 8.0f -> 8
            sigma >= 4.0f -> 4
            sigma >= 2.0f -> 2
            else -> 1
        }

        /** decimated_gaussian_radius / gaussian_grid_radius */
        fun decimatedGaussianRadius(radius: Int, stride: Int): Int =
            max(floorDiv(radius + stride - 1, stride), 1)

        /** Halide integer division: rounds toward negative infinity for a positive divisor. */
        fun floorDiv(a: Int, b: Int): Int = Math.floorDiv(a, b)

        /** Halide integer modulo: non-negative for a positive divisor. */
        fun floorMod(a: Int, b: Int): Int = Math.floorMod(a, b)
    }
}
