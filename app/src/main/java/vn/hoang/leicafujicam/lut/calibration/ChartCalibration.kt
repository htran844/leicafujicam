package vn.hoang.leicafujicam.lut.calibration

import android.graphics.Bitmap
import android.graphics.PointF
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * On-device ColorChecker 24 calibration math.
 *
 * Pipeline: sample the 24 chart patches from a photo taken by this app with no
 * LUT applied, fit a polynomial correction (measured -> reference) in linear
 * RGB, bake that correction into a 33^3 LUT, then compose it with any built-in
 * style LUT (out = correction(style(in))), mirroring tools/calibration on PC.
 */
object ChartCalibration {
    const val LUT_SIZE = 33
    const val GRID_COLS = 6
    const val GRID_ROWS = 4
    const val PATCH_COUNT = GRID_COLS * GRID_ROWS

    /**
     * Reference patch values, sRGB (D65), 0..1, row-major starting at Dark Skin.
     * Source: BabelColor, "The ColorChecker (since 1976)" — same table as the
     * PC tool in tools/calibration/calibrate.py.
     */
    val REFERENCE: Array<FloatArray> = arrayOf(
        intArrayOf(115, 82, 68),   // 1  Dark Skin
        intArrayOf(194, 150, 130), // 2  Light Skin
        intArrayOf(98, 122, 157),  // 3  Blue Sky
        intArrayOf(87, 108, 67),   // 4  Foliage
        intArrayOf(133, 128, 177), // 5  Blue Flower
        intArrayOf(103, 189, 170), // 6  Bluish Green
        intArrayOf(214, 126, 44),  // 7  Orange
        intArrayOf(80, 91, 166),   // 8  Purplish Blue
        intArrayOf(193, 90, 99),   // 9  Moderate Red
        intArrayOf(94, 60, 108),   // 10 Purple
        intArrayOf(157, 188, 64),  // 11 Yellow Green
        intArrayOf(224, 163, 46),  // 12 Orange Yellow
        intArrayOf(56, 61, 150),   // 13 Blue
        intArrayOf(70, 148, 73),   // 14 Green
        intArrayOf(175, 54, 60),   // 15 Red
        intArrayOf(231, 199, 31),  // 16 Yellow
        intArrayOf(187, 86, 149),  // 17 Magenta
        intArrayOf(8, 133, 161),   // 18 Cyan
        intArrayOf(243, 243, 242), // 19 White 9.5
        intArrayOf(200, 200, 200), // 20 Neutral 8
        intArrayOf(160, 160, 160), // 21 Neutral 6.5
        intArrayOf(122, 122, 121), // 22 Neutral 5
        intArrayOf(85, 85, 85),    // 23 Neutral 3.5
        intArrayOf(52, 52, 52)     // 24 Black 2
    ).map { rgb -> FloatArray(3) { i -> rgb[i] / 255f } }.toTypedArray()

    // ------------------------------------------------------------------
    // Patch extraction
    // ------------------------------------------------------------------

    /**
     * Sample the 24 patch means from [bitmap]. [corners] are the four corners
     * of the patch grid area in bitmap pixels, ordered top-left, top-right,
     * bottom-right, bottom-left of the chart as placed; [rotation] rotates
     * which physical corner counts as the chart's top-left.
     */
    fun extractPatches(
        bitmap: Bitmap,
        corners: Array<PointF>,
        rotation: Int
    ): Array<FloatArray>? {
        require(corners.size == 4)
        val ordered = Array(4) { i -> corners[(i + rotation) % 4] }
        val unitToBitmap = homographyUnitToPoints(ordered) ?: return null
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val samplesPerAxis = 5
        val result = Array(PATCH_COUNT) { FloatArray(3) }
        for (row in 0 until GRID_ROWS) {
            for (col in 0 until GRID_COLS) {
                var rSum = 0.0
                var gSum = 0.0
                var bSum = 0.0
                var count = 0
                for (sy in 0 until samplesPerAxis) {
                    for (sx in 0 until samplesPerAxis) {
                        val u = (col + 0.32 + 0.36 * (sx + 0.5) / samplesPerAxis) / GRID_COLS
                        val v = (row + 0.32 + 0.36 * (sy + 0.5) / samplesPerAxis) / GRID_ROWS
                        val p = applyHomography(unitToBitmap, u, v)
                        val x = p.x.roundToInt().coerceIn(0, bitmap.width - 1)
                        val y = p.y.roundToInt().coerceIn(0, bitmap.height - 1)
                        val argb = pixels[y * bitmap.width + x]
                        rSum += ((argb shr 16) and 0xFF).toDouble()
                        gSum += ((argb shr 8) and 0xFF).toDouble()
                        bSum += (argb and 0xFF).toDouble()
                        count++
                    }
                }
                val idx = row * GRID_COLS + col
                result[idx][0] = (rSum / count / 255.0).toFloat()
                result[idx][1] = (gSum / count / 255.0).toFloat()
                result[idx][2] = (bSum / count / 255.0).toFloat()
            }
        }
        return result
    }

    /**
     * Try all four chart orientations and keep the one whose sampled patches
     * sit closest to the reference (mean Delta E 2000). Returns the winning
     * rotation together with its patches.
     */
    fun extractBestRotation(
        bitmap: Bitmap,
        corners: Array<PointF>
    ): Pair<Int, Array<FloatArray>>? {
        var best: Pair<Int, Array<FloatArray>>? = null
        var bestMean = Float.MAX_VALUE
        for (rotation in 0..3) {
            val patches = extractPatches(bitmap, corners, rotation) ?: continue
            val (mean, _) = meanMaxDeltaE(patches, REFERENCE)
            if (mean < bestMean) {
                bestMean = mean
                best = rotation to patches
            }
        }
        return best
    }

    // ------------------------------------------------------------------
    // Polynomial correction fit (linear RGB)
    // ------------------------------------------------------------------

    private const val FEATURE_COUNT = 11

    private fun features(lin: DoubleArray): DoubleArray {
        val (r, g, b) = Triple(lin[0], lin[1], lin[2])
        return doubleArrayOf(
            1.0, r, g, b, r * g, r * b, g * b, r * r, g * g, b * b, r * g * b
        )
    }

    /** weights[channel][feature] mapping measured linear RGB to reference linear RGB. */
    class PolynomialCorrection internal constructor(
        private val weights: Array<DoubleArray>
    ) {
        fun applyLinear(lin: DoubleArray): DoubleArray {
            val f = features(lin)
            return DoubleArray(3) { ch ->
                var sum = 0.0
                for (i in 0 until FEATURE_COUNT) sum += weights[ch][i] * f[i]
                sum.coerceIn(0.0, 1.0)
            }
        }

        fun applySrgb(rgb: FloatArray): FloatArray {
            val lin = DoubleArray(3) { srgbToLinear(rgb[it].toDouble()) }
            val corrected = applyLinear(lin)
            return FloatArray(3) { linearToSrgb(corrected[it]).toFloat() }
        }
    }

    fun fitCorrection(
        measured: Array<FloatArray>,
        reference: Array<FloatArray> = REFERENCE
    ): PolynomialCorrection {
        val n = measured.size
        val feat = Array(n) { i ->
            features(DoubleArray(3) { ch -> srgbToLinear(measured[i][ch].toDouble()) })
        }
        val target = Array(n) { i ->
            DoubleArray(3) { ch -> srgbToLinear(reference[i][ch].toDouble()) }
        }
        // Normal equations (F^T F + lambda I) w = F^T t, solved per channel.
        val gram = Array(FEATURE_COUNT) { DoubleArray(FEATURE_COUNT) }
        for (i in 0 until FEATURE_COUNT) {
            for (j in 0 until FEATURE_COUNT) {
                var sum = 0.0
                for (k in 0 until n) sum += feat[k][i] * feat[k][j]
                gram[i][j] = sum
            }
            gram[i][i] += 1e-6
        }
        val weights = Array(3) { ch ->
            val rhs = DoubleArray(FEATURE_COUNT) { i ->
                var sum = 0.0
                for (k in 0 until n) sum += feat[k][i] * target[k][ch]
                sum
            }
            solveLinear(gram.map { it.clone() }.toTypedArray(), rhs)
                ?: DoubleArray(FEATURE_COUNT) { if (it == ch + 1) 1.0 else 0.0 }
        }
        return PolynomialCorrection(weights)
    }

    // ------------------------------------------------------------------
    // Correction LUT (33^3, node order b -> g -> r, RGB per node)
    // ------------------------------------------------------------------

    fun nodeIndex(size: Int, b: Int, g: Int, r: Int): Int = ((b * size) + g) * size + r

    fun buildCorrectionData(correction: PolynomialCorrection): FloatArray {
        val data = FloatArray(LUT_SIZE * LUT_SIZE * LUT_SIZE * 3)
        val rgb = FloatArray(3)
        for (b in 0 until LUT_SIZE) {
            for (g in 0 until LUT_SIZE) {
                for (r in 0 until LUT_SIZE) {
                    rgb[0] = r / (LUT_SIZE - 1f)
                    rgb[1] = g / (LUT_SIZE - 1f)
                    rgb[2] = b / (LUT_SIZE - 1f)
                    val out = correction.applySrgb(rgb)
                    val base = nodeIndex(LUT_SIZE, b, g, r) * 3
                    data[base] = out[0]
                    data[base + 1] = out[1]
                    data[base + 2] = out[2]
                }
            }
        }
        return data
    }

    /** Apply a baked correction LUT (trilinear) to a set of sRGB patches. */
    fun applyCorrectionData(
        data: FloatArray,
        patches: Array<FloatArray>
    ): Array<FloatArray> = Array(patches.size) { i ->
        PlutIO.sample(data, LUT_SIZE, patches[i][0], patches[i][1], patches[i][2])
    }

    // ------------------------------------------------------------------
    // Color space conversions and Delta E 2000
    // ------------------------------------------------------------------

    fun srgbToLinear(c: Double): Double =
        if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055).pow(2.4)

    fun linearToSrgb(c: Double): Double {
        val v = c.coerceIn(0.0, 1.0)
        return if (v <= 0.0031308) v * 12.92 else 1.055 * v.pow(1.0 / 2.4) - 0.055
    }

    fun srgbToLab(rgb: FloatArray): DoubleArray {
        val r = srgbToLinear(rgb[0].toDouble())
        val g = srgbToLinear(rgb[1].toDouble())
        val b = srgbToLinear(rgb[2].toDouble())
        var x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047
        var y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b) / 1.0
        var z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883
        x = labF(x); y = labF(y); z = labF(z)
        return doubleArrayOf(116 * y - 16, 500 * (x - y), 200 * (y - z))
    }

    private fun labF(t: Double): Double {
        val d = 6.0 / 29.0
        return if (t > d * d * d) t.pow(1.0 / 3.0) else t / (3 * d * d) + 4.0 / 29.0
    }

    fun deltaE2000(lab1: DoubleArray, lab2: DoubleArray): Double {
        val (l1, a1, b1) = Triple(lab1[0], lab1[1], lab1[2])
        val (l2, a2, b2) = Triple(lab2[0], lab2[1], lab2[2])
        val c1 = kotlin.math.hypot(a1, b1)
        val c2 = kotlin.math.hypot(a2, b2)
        val cAvg = (c1 + c2) / 2.0
        val g = 0.5 * (1 - kotlin.math.sqrt(cAvg.pow(7) / (cAvg.pow(7) + 25.0.pow(7))))
        val a1p = a1 * (1 + g)
        val a2p = a2 * (1 + g)
        val c1p = kotlin.math.hypot(a1p, b1)
        val c2p = kotlin.math.hypot(a2p, b2)
        fun hue(ap: Double, b: Double, cp: Double): Double {
            if (cp == 0.0) return 0.0
            val h = Math.toDegrees(kotlin.math.atan2(b, ap))
            return if (h < 0) h + 360 else h
        }
        val h1p = hue(a1p, b1, c1p)
        val h2p = hue(a2p, b2, c2p)
        val dLp = l2 - l1
        val dCp = c2p - c1p
        val dhp: Double = when {
            c1p * c2p == 0.0 -> 0.0
            abs(h2p - h1p) <= 180 -> h2p - h1p
            h2p - h1p > 180 -> h2p - h1p - 360
            else -> h2p - h1p + 360
        }
        val dHp = 2 * kotlin.math.sqrt(c1p * c2p) * kotlin.math.sin(Math.toRadians(dhp) / 2)
        val lAvg = (l1 + l2) / 2.0
        val cAvgP = (c1p + c2p) / 2.0
        val hAvg: Double = when {
            c1p * c2p == 0.0 -> h1p + h2p
            abs(h1p - h2p) <= 180 -> (h1p + h2p) / 2.0
            h1p + h2p < 360 -> (h1p + h2p + 360) / 2.0
            else -> (h1p + h2p - 360) / 2.0
        }
        val t = 1 - 0.17 * kotlin.math.cos(Math.toRadians(hAvg - 30)) +
            0.24 * kotlin.math.cos(Math.toRadians(2 * hAvg)) +
            0.32 * kotlin.math.cos(Math.toRadians(3 * hAvg + 6)) -
            0.20 * kotlin.math.cos(Math.toRadians(4 * hAvg - 63))
        val dTheta = 30 * kotlin.math.exp(-(((hAvg - 275) / 25).pow(2)))
        val rc = 2 * kotlin.math.sqrt(cAvgP.pow(7) / (cAvgP.pow(7) + 25.0.pow(7)))
        val sl = 1 + 0.015 * (lAvg - 50).pow(2) / kotlin.math.sqrt(20 + (lAvg - 50).pow(2))
        val sc = 1 + 0.045 * cAvgP
        val sh = 1 + 0.015 * cAvgP * t
        val rt = -kotlin.math.sin(Math.toRadians(2 * dTheta)) * rc
        return kotlin.math.sqrt(
            (dLp / sl).pow(2) + (dCp / sc).pow(2) + (dHp / sh).pow(2) +
                rt * (dCp / sc) * (dHp / sh)
        )
    }

    /** Mean and max Delta E 2000 between two patch sets of equal size. */
    fun meanMaxDeltaE(
        patches: Array<FloatArray>,
        reference: Array<FloatArray> = REFERENCE
    ): Pair<Float, Float> {
        var sum = 0.0
        var max = 0.0
        for (i in patches.indices) {
            val de = deltaE2000(srgbToLab(patches[i]), srgbToLab(reference[i]))
            sum += de
            if (de > max) max = de
        }
        return (sum / patches.size).toFloat() to max.toFloat()
    }

    // ------------------------------------------------------------------
    // Homography / linear algebra helpers
    // ------------------------------------------------------------------

    /** Homography mapping the unit square onto the four given points. */
    private fun homographyUnitToPoints(points: Array<PointF>): FloatArray? {
        val unit = arrayOf(
            PointF(0f, 0f), PointF(1f, 0f), PointF(1f, 1f), PointF(0f, 1f)
        )
        val a = Array(8) { DoubleArray(8) }
        val b = DoubleArray(8)
        for (i in 0..3) {
            val (u, v) = unit[i].let { it.x.toDouble() to it.y.toDouble() }
            val (x, y) = points[i].let { it.x.toDouble() to it.y.toDouble() }
            a[2 * i] = doubleArrayOf(u, v, 1.0, 0.0, 0.0, 0.0, -x * u, -x * v)
            b[2 * i] = x
            a[2 * i + 1] = doubleArrayOf(0.0, 0.0, 0.0, u, v, 1.0, -y * u, -y * v)
            b[2 * i + 1] = y
        }
        val h = solveLinear(a, b) ?: return null
        return floatArrayOf(
            h[0].toFloat(), h[1].toFloat(), h[2].toFloat(),
            h[3].toFloat(), h[4].toFloat(), h[5].toFloat(),
            h[6].toFloat(), h[7].toFloat(), 1f
        )
    }

    private fun applyHomography(h: FloatArray, u: Double, v: Double): PointF {
        val w = h[6] * u + h[7] * v + h[8]
        val x = (h[0] * u + h[1] * v + h[2]) / w
        val y = (h[3] * u + h[4] * v + h[5]) / w
        return PointF(x.toFloat(), y.toFloat())
    }

    /** Gaussian elimination with partial pivoting; returns null if singular. */
    internal fun solveLinear(aIn: Array<DoubleArray>, bIn: DoubleArray): DoubleArray? {
        val n = bIn.size
        val a = Array(n) { i -> DoubleArray(n + 1) { j -> if (j < n) aIn[i][j] else bIn[i] } }
        for (col in 0 until n) {
            var pivot = col
            for (row in col + 1 until n) {
                if (abs(a[row][col]) > abs(a[pivot][col])) pivot = row
            }
            if (abs(a[pivot][col]) < 1e-12) return null
            if (pivot != col) {
                val tmp = a[pivot]; a[pivot] = a[col]; a[col] = tmp
            }
            for (row in col + 1 until n) {
                val factor = a[row][col] / a[col][col]
                for (j in col until n + 1) a[row][j] -= factor * a[col][j]
            }
        }
        val x = DoubleArray(n)
        for (i in n - 1 downTo 0) {
            var sum = a[i][n]
            for (j in i + 1 until n) sum -= a[i][j] * x[j]
            x[i] = sum / a[i][i]
        }
        return x
    }
}

/**
 * Minimal .plut reader/writer + trilinear sampling + LUT composition.
 * File layout matches the bundled built-in LUTs (and the PC calibration
 * tool): header ints LE [magic 'PLUT', version, size, dataType], for
 * version >= 2 a transfer-curve id, for version >= 3 a color-space ordinal,
 * then nodes in b -> g -> r order with R,G,B components.
 */
object PlutIO {
    private const val MAGIC = 0x54554C50 // 'PLUT'

    class LutGrid(val size: Int, val data: FloatArray)

    fun readPlut(bytes: ByteArray): LutGrid? {
        return try {
            val buffer = java.nio.ByteBuffer.wrap(bytes)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
            if (buffer.int != MAGIC) return null
            val version = buffer.int
            if (version !in 1..4) return null
            val size = buffer.int
            val dataType = buffer.int
            if (version >= 2) buffer.int // transfer curve id
            if (version >= 3) buffer.int // color space ordinal
            val count = size * size * size * 3
            val data = FloatArray(count)
            when (dataType) {
                1 -> {
                    val shorts = ShortArray(count)
                    buffer.asShortBuffer().get(shorts)
                    for (i in 0 until count) {
                        data[i] = (shorts[i].toInt() and 0xFFFF) / 65535f
                    }
                }
                0 -> {
                    val raw = ByteArray(count)
                    buffer.get(raw)
                    for (i in 0 until count) data[i] = (raw[i].toInt() and 0xFF) / 255f
                }
                else -> return null
            }
            LutGrid(size, data)
        } catch (e: Exception) {
            null
        }
    }

    /** Serialize a 33^3 grid as a version-1 UINT16 .plut identical in layout to built-ins. */
    fun writePlutV1(data: FloatArray): ByteArray {
        val size = ChartCalibration.LUT_SIZE
        require(data.size == size * size * size * 3)
        val out = java.nio.ByteBuffer.allocate(16 + data.size * 2)
            .order(java.nio.ByteOrder.LITTLE_ENDIAN)
        out.putInt(MAGIC)
        out.putInt(1)
        out.putInt(size)
        out.putInt(1)
        for (v in data) {
            out.putShort((v.coerceIn(0f, 1f) * 65535f).roundToInt().toShort())
        }
        return out.array()
    }

    fun sample(data: FloatArray, size: Int, r: Float, g: Float, b: Float): FloatArray {
        val maxIdx = size - 1
        val x = r.coerceIn(0f, 1f) * maxIdx
        val y = g.coerceIn(0f, 1f) * maxIdx
        val z = b.coerceIn(0f, 1f) * maxIdx
        val x0 = floor(x).toInt().coerceAtMost(maxIdx - 1).coerceAtLeast(0)
        val y0 = floor(y).toInt().coerceAtMost(maxIdx - 1).coerceAtLeast(0)
        val z0 = floor(z).toInt().coerceAtMost(maxIdx - 1).coerceAtLeast(0)
        val fx = x - x0
        val fy = y - y0
        val fz = z - z0
        val out = FloatArray(3)
        for (ch in 0..2) {
            var sum = 0f
            for (dz in 0..1) {
                for (dy in 0..1) {
                    for (dx in 0..1) {
                        val w = (if (dx == 1) fx else 1 - fx) *
                            (if (dy == 1) fy else 1 - fy) *
                            (if (dz == 1) fz else 1 - fz)
                        val idx = ChartCalibration.nodeIndex(size, z0 + dz, y0 + dy, x0 + dx) * 3 + ch
                        sum += w * data[idx]
                    }
                }
            }
            out[ch] = sum
        }
        return out
    }

    /** out(node) = correction(style(node)) on a fresh 33^3 grid. */
    fun compose(correctionData: FloatArray, style: LutGrid): FloatArray {
        val size = ChartCalibration.LUT_SIZE
        val out = FloatArray(size * size * size * 3)
        for (b in 0 until size) {
            for (g in 0 until size) {
                for (r in 0 until size) {
                    val styled = sample(
                        style.data, style.size,
                        r / (size - 1f), g / (size - 1f), b / (size - 1f)
                    )
                    val corrected = sample(
                        correctionData, size, styled[0], styled[1], styled[2]
                    )
                    val base = ChartCalibration.nodeIndex(size, b, g, r) * 3
                    out[base] = corrected[0]
                    out[base + 1] = corrected[1]
                    out[base + 2] = corrected[2]
                }
            }
        }
        return out
    }
}
