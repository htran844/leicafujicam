package vn.hoang.leicafujicam.frame

import android.graphics.Bitmap
import android.graphics.Gainmap
import androidx.annotation.RequiresApi
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt

/** Re-encodes free-layer composites at the gainmap resolution, preserving untouched samples. */
@RequiresApi(34)
internal object FrameGainmapCompositor {
    fun composite(samples: Bitmap, gainmap: Gainmap, artwork: Bitmap, framedSdr: Bitmap): Bitmap {
        val width = samples.width
        val height = samples.height
        require(artwork.width == width && artwork.height == height)
        val sdr = Bitmap.createScaledBitmap(framedSdr, width, height, true)
        // Colored translucent artwork needs independent channel gains, including for a mono input.
        val result = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        try {
            val sampleRow = IntArray(width)
            val artworkRow = IntArray(width)
            val sdrRow = IntArray(width)
            val mono = samples.config == Bitmap.Config.ALPHA_8
            val minimum = gainmap.ratioMin
            val maximum = gainmap.ratioMax
            val gamma = gainmap.gamma
            val epsilonSdr = gainmap.epsilonSdr
            val epsilonHdr = gainmap.epsilonHdr
            val logMin = DoubleArray(3) { ln(minimum[it].toDouble()) }
            val logSpan = DoubleArray(3) { ln(maximum[it].toDouble()) - logMin[it] }
            val ratios = Array(3) { channel ->
                DoubleArray(256) { sample ->
                    exp(logMin[channel] + logSpan[channel] * (sample / 255.0).pow(gamma[channel].toDouble()))
                }
            }
            for (y in 0 until height) {
                samples.getPixels(sampleRow, 0, width, 0, y, width, 1)
                artwork.getPixels(artworkRow, 0, width, 0, y, width, 1)
                sdr.getPixels(sdrRow, 0, width, 0, y, width, 1)
                for (x in 0 until width) {
                    val original = sampleRow[x]
                    val art = artworkRow[x]
                    val alpha = (art ushr 24) / 255.0
                    var encodedPixel = 0xFF000000.toInt()
                    for (channel in 0..2) {
                        val shift = (2 - channel) * 8
                        val sample = if (mono) original ushr 24 else (original ushr shift) and 255
                        val encoded = if (alpha == 0.0 || logSpan[channel] == 0.0) sample else {
                            val finalSdr = ((sdrRow[x] ushr shift) and 255) / 255.0
                            val artworkSdr = ((art ushr shift) and 255) / 255.0
                            val finalHdr = if (alpha == 1.0) {
                                toLinear(finalSdr)
                            } else {
                                // Canvas composes the SDR bitmap in encoded sRGB. Recover its base
                                // and apply that same source-over operation to extended-sRGB HDR.
                                val base = ((finalSdr - alpha * artworkSdr) / (1.0 - alpha)).coerceIn(0.0, 1.0)
                                val baseHdr = ((toLinear(base) + epsilonSdr[channel]) * ratios[channel][sample]
                                    - epsilonHdr[channel]).coerceAtLeast(0.0)
                                toLinear((1.0 - alpha) * fromLinear(baseHdr) + alpha * artworkSdr)
                            }
                            val denominator = toLinear(finalSdr) + epsilonSdr[channel]
                            val ratio = if (denominator > 0.0) {
                                (finalHdr + epsilonHdr[channel]) / denominator
                            } else 1.0 // Black with zero offsets has no visible gain contribution.
                            val normalized = ((ln(ratio.coerceIn(minimum[channel].toDouble(), maximum[channel].toDouble()))
                                - logMin[channel]) / logSpan[channel]).coerceIn(0.0, 1.0)
                            (normalized.pow(1.0 / gamma[channel]) * 255.0).roundToInt().coerceIn(0, 255)
                        }
                        encodedPixel = encodedPixel or (encoded shl shift)
                    }
                    sampleRow[x] = encodedPixel
                }
                result.setPixels(sampleRow, 0, width, 0, y, width, 1)
            }
            return result
        } catch (error: Throwable) {
            result.recycle()
            throw error
        } finally {
            if (sdr !== framedSdr) sdr.recycle()
        }
    }

    private fun toLinear(value: Double): Double =
        if (value <= 0.04045) value / 12.92 else ((value + 0.055) / 1.055).pow(2.4)

    private fun fromLinear(value: Double): Double =
        if (value <= 0.0031308) value * 12.92 else 1.055 * value.pow(1.0 / 2.4) - 0.055
}
