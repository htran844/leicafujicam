package vn.hoang.leicafujicam.raw

import kotlin.math.pow

/** HDRNet linear output -> MGC Standard Gamma -> sRGB decode -> inverse ACR3. */
internal object MgcHdrNetOutputCurve {
    // CPU LUT, independent of GL_MAX_TEXTURE_SIZE. Keep both normalized endpoints.
    private const val SAMPLE_COUNT = 65537

    private val preAcrSamples by lazy {
        FloatArray(SAMPLE_COUNT) { index ->
            val encoded = standardGamma(index.toDouble() / (SAMPLE_COUNT - 1))
            val linear = if (encoded <= 0.04045) {
                encoded / 12.92
            } else {
                ((encoded + 0.055) / 1.055).pow(2.4)
            }
            ACR3Curve.inputForOutput(linear.toFloat())
        }
    }

    fun samples(): FloatArray = preAcrSamples

    /** Full linear-input Standard function (0x4E94060), including its power transform. */
    internal fun standardGamma(input: Double): Double {
        val knots = MgcStandardGammaData.KNOTS
        val position = input.coerceIn(0.0, 1.0).pow(MgcStandardGammaData.EXPONENT) * knots.lastIndex
        val index = position.toInt().coerceAtMost(knots.lastIndex - 1)
        val t = position - index
        val y0 = knots[index]
        val y1 = knots[index + 1]

        // Original cubic evaluator 0x4E94680: central tangents inside the table;
        // special first/last segment coefficients, not duplicated endpoint knots.
        val slope: Double
        val quadratic: Double
        val cubic: Double
        when (index) {
            0 -> {
                val nextSlope = (knots[2] - y0) * 0.5
                cubic = (y0 + nextSlope - y1) * 0.5
                quadratic = 0.0
                slope = nextSlope - 3.0 * cubic
            }
            knots.lastIndex - 1 -> {
                slope = (y1 - knots[index - 1]) * 0.5
                cubic = (y0 + slope - y1) * 0.5
                quadratic = -3.0 * cubic
            }
            else -> {
                slope = (y1 - knots[index - 1]) * 0.5
                val nextSlope = (knots[index + 2] - y0) * 0.5
                cubic = 2.0 * y0 + (slope + (nextSlope - 2.0 * y1))
                quadratic = (y1 - slope - y0) - cubic
            }
        }
        return y0 + t * (slope + t * (quadratic + t * cubic))
    }
}
