package vn.hoang.leicafujicam.raw

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.pow

/** Original R5 MLIB ICC → original DPP sRGB profile. No Adobe look or fitted LUT. */
internal class CanonIccProfile private constructor(
    val grid: Int,
    val clut: FloatArray,
    val inputCurves: Array<FloatArray>,
    val outputCurves: Array<FloatArray>,
    val targetCurves: Array<FloatArray>,
    /** Column-major inverse of the output profile's RGB→XYZ D50 matrix. */
    val xyzToTarget: FloatArray,
    val pcsWhite: FloatArray,
) {
    /** ICC tetrahedral reference, independent of the GLES implementation; not an original UCS oracle. */
    fun evaluate(rgb: FloatArray): FloatArray {
        require(rgb.size == 3 && rgb.all { it.isFinite() })
        val p = FloatArray(3) { sample(inputCurves[it], rgb[it]) * (grid - 1) }
        val base = IntArray(3) { p[it].toInt().coerceAtMost(grid - 2) }
        val fraction = FloatArray(3) { p[it] - base[it] }
        val order = (0..2).sortedByDescending { fraction[it] }
        fun corner(index: IntArray, channel: Int) = clut[((index[0] * grid + index[1]) * grid + index[2]) * 3 + channel]
        val vertices = Array(4) { base.copyOf() }
        for (i in 1..3) {
            vertices[i] = vertices[i - 1].copyOf()
            vertices[i][order[i - 1]]++
        }
        val f = order.map { fraction[it] }
        val weights = floatArrayOf(1f - f[0], f[0] - f[1], f[1] - f[2], f[2])
        val lab = FloatArray(3) { channel ->
            sample(outputCurves[channel], (0..3).sumOf { (corner(vertices[it], channel) * weights[it]).toDouble() }.toFloat())
        }
        // ICC v2 lut16 PCS Lab: L*=100 is 0xff00; a*=b*=0 is 0x8000.
        val fy = (lab[0] * (65535f / 65280f) * 100f + 16f) / 116f
        val fx = fy + (lab[1] * (65535f / 256f) - 128f) / 500f
        val fz = fy - (lab[2] * (65535f / 256f) - 128f) / 200f
        fun labInverse(v: Float) = if (v > 6f / 29f) v * v * v else (v - 4f / 29f) * (108f / 841f)
        val xyz = floatArrayOf(labInverse(fx) * pcsWhite[0], labInverse(fy) * pcsWhite[1], labInverse(fz) * pcsWhite[2])
        return FloatArray(3) { channel ->
            val linear = xyzToTarget[channel] * xyz[0] + xyzToTarget[3 + channel] * xyz[1] + xyzToTarget[6 + channel] * xyz[2]
            val encoded = inverse(targetCurves[channel], linear)
            if (encoded <= 0.04045f) encoded / 12.92f else ((encoded + 0.055f) / 1.055f).pow(2.4f)
        }
    }

    companion object {
        private fun sample(curve: FloatArray, value: Float): Float {
            val p = value.coerceIn(0f, 1f) * (curve.size - 1)
            val i = p.toInt().coerceAtMost(curve.lastIndex - 1)
            return curve[i] + (curve[i + 1] - curve[i]) * (p - i)
        }
        private fun inverse(curve: FloatArray, value: Float): Float {
            if (value <= curve.first()) return 0f
            if (value >= curve.last()) return 1f
            var lo = 0
            var hi = curve.lastIndex
            while (hi - lo > 1) {
                val mid = (lo + hi) ushr 1
                if (curve[mid] <= value) lo = mid else hi = mid
            }
            val span = curve[hi] - curve[lo]
            return (lo + if (span > 0f) (value - curve[lo]) / span else 0f) / curve.lastIndex
        }
        private class Icc(val raw: ByteArray) {
            val buffer: ByteBuffer = ByteBuffer.wrap(raw).order(ByteOrder.BIG_ENDIAN)
            init {
                require(raw.size >= 132 && buffer.getInt(0) == raw.size && text(36) == "acsp") { "Invalid Canon ICC header" }
                require(text(16) == "RGB ") { "Expected RGB Canon ICC" }
            }
            fun text(offset: Int) = String(raw, offset, 4, Charsets.US_ASCII)
            fun tag(name: String): IntRange {
                val count = buffer.getInt(128)
                require(count in 1..128 && 132L + count * 12L <= raw.size)
                for (i in 0 until count) {
                    val offset = 132 + i * 12
                    if (text(offset) == name) {
                        val start = buffer.getInt(offset + 4)
                        val size = buffer.getInt(offset + 8)
                        require(start >= 128 && size >= 8 && start.toLong() + size <= raw.size)
                        return start until start + size
                    }
                }
                error("Canon ICC tag missing: $name")
            }
            fun fixed(offset: Int) = buffer.getInt(offset) / 65536f
            fun u16(offset: Int) = (buffer.getShort(offset).toInt() and 65535) / 65535f
            fun xyz(name: String): FloatArray {
                val tag = tag(name)
                require(text(tag.first) == "XYZ " && tag.count() >= 20)
                return FloatArray(3) { fixed(tag.first + 8 + it * 4) }
            }
            fun curve(name: String): FloatArray {
                val tag = tag(name)
                require(text(tag.first) == "curv" && tag.count() >= 12)
                val size = buffer.getInt(tag.first + 8)
                require(size in 2..65536 && 12L + size * 2L <= tag.count()) { "Expected sampled Canon output TRC" }
                return FloatArray(size) { u16(tag.first + 12 + it * 2) }.also { curve ->
                    require((1 until curve.size).all { curve[it - 1] <= curve[it] }) { "Non-monotonic Canon output TRC" }
                }
            }
        }
        fun parse(styleIcc: ByteArray, outputIcc: ByteArray): CanonIccProfile {
            val source = Icc(styleIcc)
            val target = Icc(outputIcc)
            require(source.text(20) == "Lab " && (styleIcc[8].toInt() and 255) == 2) { "Expected Canon v2 Lab PCS" }
            val range = source.tag("A2B0")
            val start = range.first
            require(source.text(start) == "mft2" && styleIcc[start + 8].toInt() == 3 && styleIcc[start + 9].toInt() == 3)
            val grid = styleIcc[start + 10].toInt() and 255
            require(grid in 2..65)
            for (i in 0 until 9) require(source.fixed(start + 12 + i * 4) == if (i % 4 == 0) 1f else 0f) { "Non-identity Canon mft2 matrix" }
            val n = source.buffer.getShort(start + 48).toInt() and 65535
            val m = source.buffer.getShort(start + 50).toInt() and 65535
            require(n >= 2 && m >= 2 && 52L + (3L * n + 3L * grid * grid * grid + 3L * m) * 2 <= range.count())
            var cursor = start + 52
            val input = Array(3) { FloatArray(n) { source.u16(cursor).also { cursor += 2 } } }
            val clut = FloatArray(grid * grid * grid * 3) { source.u16(cursor).also { cursor += 2 } }
            val output = Array(3) { FloatArray(m) { source.u16(cursor).also { cursor += 2 } } }
            val columns = arrayOf(target.xyz("rXYZ"), target.xyz("gXYZ"), target.xyz("bXYZ"))
            val a=columns[0][0]; val b=columns[1][0]; val c=columns[2][0]
            val d=columns[0][1]; val e=columns[1][1]; val f=columns[2][1]
            val g=columns[0][2]; val h=columns[1][2]; val i=columns[2][2]
            val determinant = a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
            require(determinant.isFinite() && kotlin.math.abs(determinant) > 1e-8f)
            val inverse = floatArrayOf(e*i-f*h, f*g-d*i, d*h-e*g, c*h-b*i, a*i-c*g, b*g-a*h, b*f-c*e, c*d-a*f, a*e-b*d)
            for (index in inverse.indices) inverse[index] /= determinant
            return CanonIccProfile(grid, clut, input, output, arrayOf(target.curve("rTRC"),target.curve("gTRC"),target.curve("bTRC")), inverse, FloatArray(3) { source.fixed(68 + it * 4) })
        }
    }
}
