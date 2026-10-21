package vn.hoang.leicafujicam.utils

import android.graphics.Bitmap
import android.graphics.Color
import kotlin.math.ceil
import kotlin.math.exp
import kotlin.math.roundToInt

/** Separable Gaussian convolution with clamped edges, including Android 11. */
internal object GaussianBlur {
    private fun kernel(sigma: Float): FloatArray {
        require(sigma > 0f && sigma.isFinite())
        val radius = ceil(3f * sigma).toInt()
        val weights = FloatArray(radius * 2 + 1) { index ->
            val distance = (index - radius).toFloat()
            exp(-distance * distance / (2f * sigma * sigma))
        }
        val total = weights.sum()
        weights.indices.forEach { weights[it] /= total }
        return weights
    }

    fun blur(bitmap: Bitmap, sigmaX: Float, sigmaY: Float) {
        val width = bitmap.width
        val height = bitmap.height
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        // Retain fractional channel values between the two separable passes.
        val horizontal = FloatArray(pixels.size * 3)
        val weightsX = kernel(sigmaX)
        val radiusX = weightsX.size / 2
        for (y in 0 until height) {
            for (x in 0 until width) {
                var red = 0f
                var green = 0f
                var blue = 0f
                for (tap in weightsX.indices) {
                    val color = pixels[y * width + (x + tap - radiusX).coerceIn(0, width - 1)]
                    val weight = weightsX[tap]
                    red += ((color ushr 16) and 255) * weight
                    green += ((color ushr 8) and 255) * weight
                    blue += (color and 255) * weight
                }
                val offset = (y * width + x) * 3
                horizontal[offset] = red
                horizontal[offset + 1] = green
                horizontal[offset + 2] = blue
            }
        }
        val weightsY = kernel(sigmaY)
        val radiusY = weightsY.size / 2
        for (y in 0 until height) {
            for (x in 0 until width) {
                var red = 0f
                var green = 0f
                var blue = 0f
                for (tap in weightsY.indices) {
                    val offset = ((y + tap - radiusY).coerceIn(0, height - 1) * width + x) * 3
                    val weight = weightsY[tap]
                    red += horizontal[offset] * weight
                    green += horizontal[offset + 1] * weight
                    blue += horizontal[offset + 2] * weight
                }
                pixels[y * width + x] = Color.rgb(
                    red.roundToInt().coerceIn(0, 255),
                    green.roundToInt().coerceIn(0, 255),
                    blue.roundToInt().coerceIn(0, 255)
                )
            }
        }
        bitmap.setPixels(pixels, 0, width, 0, 0, width, height)
    }
}
