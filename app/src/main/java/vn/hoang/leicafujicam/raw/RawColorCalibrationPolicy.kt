package vn.hoang.leicafujicam.raw

import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.params.ColorSpaceTransform
import vn.hoang.leicafujicam.utils.PLog
import kotlin.math.abs
import kotlin.math.roundToInt

/** A source calibration decision shared by capture, manual WB and DNG serialization. */
internal object RawColorCalibrationPolicy {
    data class CameraCalibration(
        val colorMatrix1: ColorSpaceTransform?,
        val colorMatrix2: ColorSpaceTransform?,
        val forwardMatrix1: ColorSpaceTransform?,
        val forwardMatrix2: ColorSpaceTransform?,
        val illuminant1: Int,
        val illuminant2: Int,
        val calibration1: ColorSpaceTransform?,
        val calibration2: ColorSpaceTransform?,
    ) {
        fun toRawCalibration(): RawCameraCalibration = RawCameraCalibration(
            colorMatrix1 = colorMatrix1?.toMatrix(), colorMatrix2 = colorMatrix2?.toMatrix(),
            calibrationIlluminant1 = illuminant1, calibrationIlluminant2 = illuminant2,
            forwardMatrix1 = forwardMatrix1?.toMatrix(), forwardMatrix2 = forwardMatrix2?.toMatrix(),
            cameraCalibration1 = calibration1?.toMatrix(), cameraCalibration2 = calibration2?.toMatrix(),
        )
    }

    fun fromCharacteristics(characteristics: CameraCharacteristics): CameraCalibration {
        val illuminant1 = characteristics.get(CameraCharacteristics.SENSOR_REFERENCE_ILLUMINANT1) ?: 0
        val illuminant2 = characteristics.get(CameraCharacteristics.SENSOR_REFERENCE_ILLUMINANT2)?.toInt() ?: 0
        val cm1 = characteristics.get(CameraCharacteristics.SENSOR_COLOR_TRANSFORM1)
            ?.takeIf { isUsableColorMatrix(it.toMatrix(), illuminant1) }
        val cm2 = characteristics.get(CameraCharacteristics.SENSOR_COLOR_TRANSFORM2)
            ?.takeIf { isUsableColorMatrix(it.toMatrix(), illuminant2) }
        val (fm1, fm2) = ForwardMatrixPolicy.fromCharacteristics(characteristics)
        if (cm1 == null && cm2 == null && fm1 == null && fm2 == null) {
            PLog.w("RawColorCalibrationPolicy", "No usable source matrices; using fixed DNG calibration")
            return CameraCalibration(
                FixedRawCalibration.colorMatrix1().toTransform(), FixedRawCalibration.colorMatrix2().toTransform(),
                null, null,
                FixedRawCalibration.ILLUMINANT1, FixedRawCalibration.ILLUMINANT2,
                FixedRawCalibration.cameraCalibration1().toTransform(), FixedRawCalibration.cameraCalibration2().toTransform(),
            )
        }
        return CameraCalibration(cm1, cm2, fm1, fm2,
            illuminant1, illuminant2,
            characteristics.get(CameraCharacteristics.SENSOR_CALIBRATION_TRANSFORM1),
            characteristics.get(CameraCharacteristics.SENSOR_CALIBRATION_TRANSFORM2),
        )
    }

    /** Call after original-payload ForwardMatrix rejection, while retaining illuminant slots. */
    fun resolveProfile(profile: DcpProfile): DcpProfile {
        val cm1 = profile.colorMatrix1?.takeIf { isUsableColorMatrix(it, profile.calibrationIlluminant1) }
        val cm2 = profile.colorMatrix2?.takeIf { isUsableColorMatrix(it, profile.calibrationIlluminant2) }
        val fm1 = profile.forwardMatrix1?.takeIf(::isUsableForwardMatrix)
        val fm2 = profile.forwardMatrix2?.takeIf(::isUsableForwardMatrix)
        if (cm1 != null || cm2 != null || fm1 != null || fm2 != null) {
            return profile.copy(colorMatrix1 = cm1, colorMatrix2 = cm2, forwardMatrix1 = fm1, forwardMatrix2 = fm2)
        }
        PLog.w("RawColorCalibrationPolicy", "No usable embedded matrices; using fixed DNG calibration")
        return profile.copy(
            colorMatrix1 = FixedRawCalibration.colorMatrix1(), colorMatrix2 = FixedRawCalibration.colorMatrix2(),
            forwardMatrix1 = null, forwardMatrix2 = null,
            calibrationIlluminant1 = FixedRawCalibration.ILLUMINANT1,
            calibrationIlluminant2 = FixedRawCalibration.ILLUMINANT2,
            analogBalance = null,
            cameraCalibration1 = FixedRawCalibration.cameraCalibration1(),
            cameraCalibration2 = FixedRawCalibration.cameraCalibration2(),
            // Tables tied to the rejected source calibration do not belong to this
            // replacement, which uses only the selected DNG's fixed calibration.
            hueSatDeltas1 = null, hueSatDeltas2 = null, lookTable = null,
        )
    }

    fun isUsableColorMatrix(matrix: FloatArray?, illuminant: Int): Boolean {
        if (!isUsableMatrix(matrix)) return false
        if (SrgbColorMatrixPolicy.isPlaceholder(requireNotNull(matrix), illuminant)) {
            PLog.w("RawColorCalibrationPolicy", "Ignoring sRGB placeholder ColorMatrix, illuminant=$illuminant")
            return false
        }
        return true
    }

    // Structural check only; source ColorMatrix/ForwardMatrix policies also reject placeholders.
    fun isUsableMatrix(matrix: FloatArray?): Boolean {
        if (matrix == null || matrix.size != 9 || matrix.any { !it.isFinite() }) return false
        if (matrix.sumOf { abs(it).toDouble() } <= 0.01) return false
        val scale = matrix.maxOf { abs(it).toDouble() }
        val m = DoubleArray(9) { matrix[it] / scale }
        val det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
            m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6])
        return abs(det) > 1e-6
    }

    fun isUsableForwardMatrix(matrix: FloatArray?): Boolean {
        if (!isUsableMatrix(matrix)) return false
        val m = requireNotNull(matrix)
        val scale = m.maxOf { abs(it).toDouble() }
        return (0..2).all { row ->
            (m[row * 3].toDouble() + m[row * 3 + 1] + m[row * 3 + 2]) / scale > 1e-6
        }
    }

    private fun ColorSpaceTransform.toMatrix(): FloatArray = FloatArray(9) {
        getElement(it % 3, it / 3).toFloat()
    }

    private fun FloatArray.toTransform(): ColorSpaceTransform = ColorSpaceTransform(IntArray(18) {
        if (it % 2 == 0) (this[it / 2].toDouble() * 100_000_000).roundToInt() else 100_000_000
    })
}
