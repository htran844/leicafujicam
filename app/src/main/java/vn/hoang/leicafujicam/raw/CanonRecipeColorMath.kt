package vn.hoang.leicafujicam.raw

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * Audited ordinary R5 RawRecipe arithmetic, separate from RGBRecipe's different LUT contract.
 * The complete pixel path is a CPU reference; input calibration and Profile processing belong
 * outside it, so this is not itself the complete floating-point Picture Style renderer.
 */
internal object CanonRecipeColorMath {
    /** The native R5 store contract: unsigned luminance and signed chroma, before RGB conversion. */
    data class Yuv(val y: Int, val u: Int, val v: Int)

    /** CDppRecipedYUV2RGBProc (0x206090), ordinary non-HDR, before ProfileProc. */
    fun yuvToRgb(value: Yuv): IntArray {
        val (y, u, v) = value
        return intArrayOf(
            (((v * 0xb37 + y * 0x800 - u) * 2 + 0x800) / 4096).coerceIn(0, 65535),
            ((y * 0x1002 - v * 0xb6d - u * 0x584 + 0x800) / 4096).coerceIn(0, 65535),
            ((y * 0xfff + u * 0x1c56 + 0x800) / 4096).coerceIn(0, 65535),
        )
    }

    /** Final R5 gamma_uv table: 65,536 signed Int32 entries, consumed as Q8 radial gains. */
    class R5UvGainTable(entries: IntArray) {
        init {
            require(entries.size == 65_536) { "R5 gamma_uv requires 65536 Int32 entries" }
        }

        private val values = entries.copyOf()
        internal operator fun get(index: Int): Int = values[index]
    }

    /** Final direct table for RawRecipe Y or C, distinct from RGBRecipe's 65536-entry table. */
    class RawGammaTable(entries: IntArray) {
        init {
            require(entries.size == 0x20000) { "RawRecipe Y/C requires 131072 Int32 entries" }
        }
        private val values = entries.copyOf()
        internal operator fun get(index: Int): Int {
            require(index in values.indices) { "RawRecipe input escaped its prepared LUT domain" }
            return values[index]
        }
    }

    class ChromaWeightTable(entries: IntArray) {
        init {
            require(entries.size == 1024 && entries.all { it in 0..65535 }) {
                "RawRecipe chroma weights require 1024 UInt16 entries"
            }
        }
        private val values = entries.copyOf()
        internal operator fun get(index: Int): Int = values[index]
    }

    /**
     * Scalar fields from the original prepared 0x1f5b20 argument block (Int32 word offsets).
     * Native pointer fields are unused here: all tables are passed separately. Construct this
     * only from a recovered recipe compiler; this class supplies no neutral guesses for fields.
     * This entry accepts an already white-balanced input and excludes spatial processing.
     */
    class R5PixelParameters(kernelWords: IntArray) {
        init {
            require(kernelWords.size == 0x168)
            for (offset in intArrayOf(8, 9, 0xb, 0xc, 0xd, 0x31, 0x62, 0xdc, 0x105, 0x107)) {
                require(kernelWords[offset] == 0) { "Unsupported R5 kernel flag at word $offset" }
            }
            require(kernelWords[0xe] == 1 && kernelWords[0x67] == 1)
            require(kernelWords[0x30] == 1)
            require(kernelWords[1] == 0x20000 && kernelWords[2] > 0 && kernelWords[3] > 0)
            require(kernelWords[0xf] in 0..1)
            require(kernelWords[0x120] == 1 && kernelWords[0x121] == 1)
        }
        private val words = kernelWords.copyOf()
        internal operator fun get(index: Int): Int = words[index]
        internal fun long(index: Int): Long = (words[index].toLong() and 0xffffffffL) or
            (words[index + 1].toLong() shl 32)
    }

    /**
     * Ordinary, nonspatial R5 pixel path from RVA 0x1f5b20. Input is the kernel's uint16 BGR
     * planes plus its separately prepared auxiliary plane; output is unsigned Y, signed U/V.
     * It retains separate luminance/chroma paths, including Monochrome's pre-C offsets.
     * Source normalization, auxiliary-plane production and scalar/table compilation remain
     * the caller's explicit responsibilities. No WB, exposure, ALO or HDR is added here.
     */
    fun renderR5Pixel(
        blue: Int,
        green: Int,
        red: Int,
        auxiliaryCode: Int,
        p: R5PixelParameters,
        gammaY: RawGammaTable,
        gammaC: RawGammaTable,
        gammaUv: R5UvGainTable,
        negativeDifferenceWeight: ChromaWeightTable,
        luminanceWeight: ChromaWeightTable,
        positiveDifferenceWeight: ChromaWeightTable,
        secondDifferenceWeight: ChromaWeightTable,
        recombination: R5RecombinationParameters,
    ): Yuv {
        require(blue in 0..65535 && green in 0..65535 && red in 0..65535 && auxiliaryCode in 0..65535)
        val range = p[1]
        val scaleNumerator = p[2]
        val scaleDenominator = p[3]
        val preLimit = p.long(0x84).toInt()
        var b = (blue * 2 * scaleNumerator / scaleDenominator).coerceAtMost(preLimit)
        var g = (green * 2 * scaleNumerator / scaleDenominator).coerceAtMost(preLimit)
        var r = (red * 2 * scaleNumerator / scaleDenominator).coerceAtMost(preLimit)
        fun indexCode(value: Int, limit: Int): Int =
            ((value.toLong() * p.long(0x76) * 4 / 4096).toInt())
                .coerceAtMost(limit)
        val ri = indexCode(r, p.long(0x70).toInt())
        val gi = indexCode(g, p.long(0x72).toInt())
        val bi = indexCode(b, p.long(0x74).toInt())
        val lumaIndex = ((gi + bi * 2 + ri) / 128).coerceIn(0, 1023)
        val firstDifferenceIndex = (gi - ri) / 32
        val secondDifferenceIndex = ((gi - bi) / 32).coerceIn(0, 1023)
        b = b.coerceAtMost(p.long(0x6e).toInt())
        g = g.coerceAtMost(p.long(0x6c).toInt())
        r = r.coerceAtMost(p.long(0x6a).toInt())

        val initialU = (blue * 0x800 - green * 0x54c - red * 0x2b3) / 4096
        val initialV = (red * 0x800 - green * 0x6b2 - blue * 0x14d) / 4096
        val reconstructedB = (auxiliaryCode * 0xfff + 0x800 + initialU * 0x1c56) / 4096 * 2
        val reconstructedG = (auxiliaryCode * 0x1002 - initialV * 0xb6d - initialU * 0x584 + 0x800) / 4096 * 2
        val reconstructedR = ((initialV * 0xb37 + auxiliaryCode * 0x800 - initialU) * 2 + 0x800) / 4096 * 2
        fun yInput(value: Int): Int = (value * scaleNumerator / scaleDenominator)
            .coerceAtMost(range - 1).coerceIn(0, p.long(0x68).toInt())
            .let { if (it > range) range - 1 else it }
        val y = ((
            gammaY[yInput(reconstructedR)] * p[0x2a] +
                gammaY[yInput(reconstructedG)] * p[0x2b] +
                gammaY[yInput(reconstructedB)] * p[0x2c] + 0x800
            ) / 4096).coerceIn(0, 65535)

        val firstDifference = ((p[0x12] * (b - g) + p[0x10] * (r - g)) / 1024)
            .coerceIn(-range, range)
        val secondDifference = ((p[0x1a] * (b - g) + p[0x18] * (r - g)) / 1024)
            .coerceIn(-range, range)
        val differenceWeight = if (firstDifferenceIndex >= 0) {
            positiveDifferenceWeight[firstDifferenceIndex.coerceAtMost(1023)]
        } else {
            negativeDifferenceWeight[(-firstDifferenceIndex).coerceAtMost(1023)]
        }
        val weight = minOf(luminanceWeight[lumaIndex], differenceWeight, secondDifferenceWeight[secondDifferenceIndex])
        val firstGain = p[if (firstDifference > 0) 0x25 else 0x26]
        val secondGain = p[if (secondDifference > 0) 0x23 else 0x24]
        val firstOffset = ((weight * firstDifference / 1024) * firstGain / 128)
            .coerceAtMost(p.long(0x88).toInt())
        val secondOffset = ((weight * secondDifference / 1024) * secondGain / 128)
            .coerceAtMost(p.long(0x88).toInt())
        val base = (p[0x1d] * g + p[0x1e] * b + p[0x1c] * r) / 1024
        val colorFirst = base + firstOffset
        val colorThird = base + secondOffset
        val colorSecond = ((base * 9 - colorThird) * 0x600 - colorFirst * 0x1000) / 0x2000
        fun cLookup(value: Int, monoOffset: Int): Int {
            val input = value.coerceIn(1 - range, range - 1) + if (p[0xf] == 1) monoOffset else 0
            return if (input < 0) {
                gammaC[0] * 2 - gammaC[-input]
            } else {
                gammaC[input.coerceAtMost(p.long(0x86).toInt())]
            }
        }
        val c1 = cLookup(colorFirst, p[0x2d])
        val c2 = cLookup(colorSecond, p[0x2e])
        val c3 = cLookup(colorThird, p[0x2f])
        val d1 = ((c3 * 0x800 - c2 * 0x550 - c1 * 0x2b0) / 4096).coerceIn(-32768, 32767)
        val d2 = ((c1 * 0x800 - c2 * 0x6b0 - c3 * 0x150) / 4096).coerceIn(-32768, 32767)
        val gain = gammaUv[(if (abs(d1) < abs(d2)) d2 else d1) + 32768]
        val u = gain * d1 / 256
        val v = gain * d2 / 256
        val mixedU = (u + (recombination.secondToFirst + if (v <= 0) recombination.nonpositiveSecondExtra else 0) * v / 8192)
            .coerceIn(-32768, 32767)
        val mixedV = (v + (recombination.firstToSecond + if (u <= 0) recombination.nonpositiveFirstExtra else 0) * u / 8192)
            .coerceIn(-32768, 32767)
        val weightedColor = p[0x163].toShort().toInt() * c1 + (p[0x163] shr 16) * c2 +
            p[0x164].toShort().toInt() * c3
        val colorLuma = (weightedColor + ((weightedColor shr 31) and 63)) shr 8
        val corrected = CanonR5ChromaMath.apply(mixedU shr 1, mixedV shr 1, colorLuma, p)
        return Yuv((y shr 2) * 4, corrected.first, corrected.second)
    }

    class R5RecombinationParameters private constructor(
        internal val nonpositiveSecondExtra: Int,
        internal val nonpositiveFirstExtra: Int,
        internal val secondToFirst: Int,
        internal val firstToSecond: Int,
    ) {
        companion object {
            /**
             * Extracts the signed coefficients actually copied by RVA 0x1e0410 from property
             * 0x2e0f00 into kernel fields 0x1f..0x22. Caller must supply the selected R5 recipe.
             * Unsupported optional color branches are rejected, never silently disabled.
             */
            fun fromRawParameters(property2e0f00: ByteArray): R5RecombinationParameters {
                require(property2e0f00.size == 0x900) { "RawRecipe parameters require 0x900 bytes" }
                val source = ByteBuffer.wrap(property2e0f00).order(ByteOrder.LITTLE_ENDIAN)
                require(source.getInt(0x54) == 0 && source.getInt(0x58) == 0) {
                    "This R5 reference does not implement optional luminance/chroma correction flags"
                }
                return R5RecombinationParameters(
                    source.getShort(0x20).toInt(),
                    source.getShort(0x22).toInt(),
                    source.getShort(0x24).toInt(),
                    source.getShort(0x26).toInt(),
                )
            }
        }
    }

}
