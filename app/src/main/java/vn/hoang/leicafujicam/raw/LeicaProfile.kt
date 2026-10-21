package vn.hoang.leicafujicam.raw

import android.content.Context
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/** M9 1.216 matrix/LUT recipe. Spatial processing and source calibration remain upstream. */
internal data class LeicaRenderPlan(
    val tables: FloatArray,
    /** Two row-major signed integer matrices consumed by ASMExecuteColorMatrix_10. */
    val redAtLeastGreenMatrix: IntArray,
    val redBelowGreenMatrix: IntArray,
    val curveIndex: Int,
)

internal object LeicaProfile {
    const val TABLE_WIDTH = 1024
    const val TABLE_HEIGHT = 2
    private const val ASSET = "leica/m9/PROCESS/LUTS.bin"
    private const val SHA256 = "25debaf581d3fdbeca6ad7c6426ca5d9e597b98856c540ceddcc912b79409d8d"
    // M9 sRGB, standard saturation/contrast. Shared editor adjustments follow the engine.
    private const val COLOR_SPACE = 0
    private const val SATURATION = 2
    private const val CONTRAST = 2
    private data class Profile(val renderPlan: LeicaRenderPlan, val calibration: DcpProfile)
    @Volatile private var cached: Profile? = null

    // Source ISO does not identify M9 capture modes; always use the normal curve.
    fun createRenderPlan(context: Context): LeicaRenderPlan = profile(context).renderPlan

    fun calibrationProfile(context: Context): DcpProfile = profile(context).calibration

    private fun profile(context: Context): Profile = cached ?: synchronized(this) {
        cached ?: decode(context.assets.open(ASSET).use { it.readBytes() }).also { cached = it }
    }

    private fun decode(bytes: ByteArray): Profile {
        require(bytes.size == 427744)
        require(MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it.toInt() and 255) } == SHA256) {
            "Leica M9 LUT asset checksum mismatch"
        }
        val data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        require(data.getInt(0) == 13 && data.getInt(0x18) == 20 && data.getInt(0x1c) == 2048)
        require(data.getInt(0x54) == 2 && data.getInt(0x58) == 5 && data.getInt(0x5c) == 5)
        val curveOffset = data.getInt(4)
        val matrixOffset = data.getInt(8)
        val calibrationOffset = data.getInt(12)
        require(curveOffset == 0xcf8 && matrixOffset == 0xb44 && calibrationOffset == 0xcd0)
        require(matrixOffset + 11 * 36 == calibrationOffset && calibrationOffset + 40 == curveOffset)
        require(curveOffset + 20 * 2048 == data.getInt(0x10))
        val matrixIndex = COLOR_SPACE * 5 + SATURATION
        fun matrix(branch: Int) = IntArray(9) {
            data.getShort(matrixOffset + matrixIndex * 36 + branch * 18 + it * 2).toInt()
        }.also { coefficients ->
            // 14-bit input and signed coefficients must fit the shader's 32-bit sums.
            for (row in 0..2) require((0..2).sumOf {
                kotlin.math.abs(coefficients[row * 3 + it]).toLong() * 16383L
            } <= Int.MAX_VALUE)
        }
        val firstMatrix = matrix(0)
        val secondMatrix = matrix(1)
        val curveIndex = COLOR_SPACE * 5 + CONTRAST
        val table = FloatArray(2048) {
            (bytes[curveOffset + curveIndex * 2048 + it].toInt() and 255) / 255f
        }
        require(table.first() == 0f && table.last() == 1f)
        require((1 until table.size).all { table[it] >= table[it - 1] })
        val renderPlan = LeicaRenderPlan(table, firstMatrix, secondMatrix, curveIndex)
        // Two illuminant + 9 signed /10000 ColorMatrix records in DNG row-major order.
        fun calibrationMatrix(slot: Int) = FloatArray(9) {
            data.getShort(calibrationOffset + slot * 20 + 2 + it * 2) / 10000f
        }
        require(data.getShort(calibrationOffset).toInt() == 17)
        require(data.getShort(calibrationOffset + 20).toInt() == 21)
        val calibration = RawCameraCalibration(
            colorMatrix1 = calibrationMatrix(0),
            colorMatrix2 = calibrationMatrix(1),
            calibrationIlluminant1 = 17,
            calibrationIlluminant2 = 21,
        ).toDcpProfile("Leica M9 firmware calibration")
        return Profile(renderPlan, calibration)
    }
}
