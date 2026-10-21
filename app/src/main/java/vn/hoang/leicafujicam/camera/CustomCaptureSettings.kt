package vn.hoang.leicafujicam.camera

import org.json.JSONObject

/** User-selected capture controls; excludes transient metering, focus points and AF/AE locks. */
data class CustomCaptureSettings(
    val exposureCompensationEv: Float,
    val exposureBiasEv: Float,
    val isIsoAuto: Boolean,
    val iso: Int,
    val isShutterSpeedAuto: Boolean,
    val shutterSpeedNs: Long,
    val isAutoFocus: Boolean,
    val focusDistance: Float,
    val isHyperfocalFocusEnabled: Boolean,
    val awbMode: Int,
    val awbTemperature: Int,
) {
    fun toJson(): String = JSONObject().apply {
        put("exposureCompensationEv", exposureCompensationEv)
        put("exposureBiasEv", exposureBiasEv)
        put("isIsoAuto", isIsoAuto)
        put("iso", iso)
        put("isShutterSpeedAuto", isShutterSpeedAuto)
        put("shutterSpeedNs", shutterSpeedNs)
        put("isAutoFocus", isAutoFocus)
        put("focusDistance", focusDistance)
        put("isHyperfocalFocusEnabled", isHyperfocalFocusEnabled)
        put("awbMode", awbMode)
        put("awbTemperature", awbTemperature)
    }.toString()

    companion object {
        fun fromState(state: CameraState): CustomCaptureSettings = CustomCaptureSettings(
            exposureCompensationEv = state.exposureCompensation * state.getExposureCompensationStep(),
            exposureBiasEv = state.exposureBias,
            isIsoAuto = state.isIsoAuto,
            iso = state.iso,
            isShutterSpeedAuto = state.isShutterSpeedAuto,
            shutterSpeedNs = state.shutterSpeed,
            isAutoFocus = state.isAutoFocus,
            focusDistance = state.focusDistance,
            isHyperfocalFocusEnabled = state.isHyperfocalFocusEnabled,
            awbMode = state.awbMode,
            awbTemperature = state.awbTemperature,
        )

        fun fromJson(json: String?): CustomCaptureSettings? {
            if (json.isNullOrBlank()) return null
            return runCatching {
                val value = JSONObject(json)
                CustomCaptureSettings(
                    exposureCompensationEv = value.getDouble("exposureCompensationEv").toFloat(),
                    exposureBiasEv = value.getDouble("exposureBiasEv").toFloat(),
                    isIsoAuto = value.getBoolean("isIsoAuto"),
                    iso = value.getInt("iso"),
                    isShutterSpeedAuto = value.getBoolean("isShutterSpeedAuto"),
                    shutterSpeedNs = value.getLong("shutterSpeedNs"),
                    isAutoFocus = value.getBoolean("isAutoFocus"),
                    focusDistance = value.getDouble("focusDistance").toFloat(),
                    isHyperfocalFocusEnabled = value.getBoolean("isHyperfocalFocusEnabled"),
                    awbMode = value.getInt("awbMode"),
                    awbTemperature = value.getInt("awbTemperature"),
                ).also {
                    require(it.exposureCompensationEv.isFinite() && it.exposureBiasEv.isFinite())
                    require(it.focusDistance.isFinite() && it.focusDistance >= 0f)
                    require(it.iso > 0 && it.shutterSpeedNs > 0 && it.awbTemperature > 0)
                }
            }.getOrNull()
        }
    }
}
