package vn.hoang.leicafujicam.processor

import kotlin.math.log2

/**
 * Photon core-pipeline defaults resolved from the physical active sensor area.
 *
 * The calibration uses a log2-area regression because doubling collecting area is the useful
 * photographic unit. Only dimensions with a meaningful size relationship are regressed; fields
 * dominated by lens-specific tuning use robust constants. Values outside the calibrated mobile
 * sensor range are clamped instead of extrapolated.
 */
object PhotonSensorSizeTuning {
    const val MODEL_ID = "photon-sensor-area-v1"
    const val MODEL_PROPERTY = "photonCoreTuningModel"
    const val SENSOR_AREA_PROPERTY = "photonSensorPhysicalAreaMm2"
    const val DEFAULT_RAW_MAX_QUALITY_TUNING_ENABLED = false

    const val MIN_CALIBRATED_AREA_MM2 = 20.48f
    const val MAX_CALIBRATED_AREA_MM2 = 128f
    const val REFERENCE_AREA_MM2 = 32f

    private val fusionNoiseCorrelationFit = LogAreaFit(
        valueAtReferenceArea = 0.767513962f,
        changePerAreaStop = -0.164396329f,
    )
    private val lumaStrengthFit = LogAreaFit(
        valueAtReferenceArea = 0.252164225f,
        changePerAreaStop = 0.059759506f,
    )
    private val denoiseResponseOffsetFit = LogAreaFit(
        valueAtReferenceArea = 13.499369928f,
        changePerAreaStop = -4.878369857f,
    )
    private val sabreSnr20Level1Fit = LogAreaFit(
        valueAtReferenceArea = 0.608876213f,
        changePerAreaStop = 0.033127858f,
    )

    /** Capture choice survives missing area so fixed sharpening can still be selected. */
    fun captureProperties(enabled: Boolean, sensorPhysicalAreaMm2: Float?): Map<String, String> {
        if (!enabled) return emptyMap()
        val area = sensorPhysicalAreaMm2?.takeIf { it.isFinite() && it > 0f }
        return buildMap {
            put(MODEL_PROPERTY, MODEL_ID)
            if (area != null) put(SENSOR_AREA_PROPERTY, area.toString())
        }
    }

    fun enabledFromProperties(properties: Map<String, String>): Boolean =
        properties[MODEL_PROPERTY] == MODEL_ID

    fun areaFromProperties(properties: Map<String, String>): Float? =
        if (enabledFromProperties(properties)) {
            properties[SENSOR_AREA_PROPERTY]?.toFloatOrNull()?.takeIf { it.isFinite() && it > 0f }
        } else null

    data class Parameters(
        val fusion: PhotonFusionTuning = PhotonCoreImagingTuning.fusion,
        val denoise: PhotonDenoiseTuning = PhotonCoreImagingTuning.denoise,
    )

    fun resolve(sensorPhysicalAreaMm2: Float?): Parameters =
        sensorPhysicalAreaMm2?.let(::forSensorAreaMm2) ?: Parameters()

    fun forPhysicalSizeMm(widthMm: Float, heightMm: Float): Parameters? {
        if (!widthMm.isFinite() || !heightMm.isFinite() || widthMm <= 0f || heightMm <= 0f) {
            return null
        }
        return forSensorAreaMm2(widthMm * heightMm)
    }

    fun forSensorAreaMm2(areaMm2: Float): Parameters? {
        if (!areaMm2.isFinite() || areaMm2 <= 0f) return null

        val calibratedArea = areaMm2.coerceIn(
            MIN_CALIBRATED_AREA_MM2,
            MAX_CALIBRATED_AREA_MM2,
        )
        val areaStops = log2((calibratedArea / REFERENCE_AREA_MM2).toDouble()).toFloat()
        val lumaStrength = lumaStrengthFit.evaluate(areaStops)

        return Parameters(
            fusion = PhotonFusionTuning(
                mergeGradientThreshold = PhotonCoreImagingTuning.fusion.mergeGradientThreshold,
                missingReferenceSignal = PhotonFusionTuning.DEFAULT_MISSING_REFERENCE_SIGNAL,
                noiseCorrelationScale = fusionNoiseCorrelationFit.evaluate(areaStops),
            ),
            denoise = PhotonDenoiseTuning(
                lumaStrengthScale = PhotonPyramidScales.uniform(lumaStrength),
                detailReconstructionScale = PhotonPyramidScales.uniform(0.3125f),
                outlierRejectionScale = PhotonPyramidScales.uniform(0.8125f),
                chromaStrengthScale = PhotonPyramidScales.uniform(0.3125f),
                frequencyResponse = PhotonDenoiseFrequencyResponse(
                    responseOffset = denoiseResponseOffsetFit.evaluate(areaStops),
                    cosineOffset = -1f,
                ),
                sabreLumaNodes = PhotonSabreLumaTuningNodes(
                    snr5 = PhotonPyramidOverrides(
                        level1 = 0.8f,
                        level2 = 2.2f,
                        level3 = 0.5f,
                        level4 = 1.65f,
                        level5 = 0.7f,
                    ),
                    snr20 = PhotonPyramidOverrides(
                        level1 = sabreSnr20Level1Fit.evaluate(areaStops),
                        level2 = 2.1f,
                        level3 = 0.4f,
                        level4 = 0.8f,
                        level5 = 0.2f,
                    ),
                    snr40 = PhotonPyramidOverrides(
                        level1 = 0.85f,
                        level2 = 0.4f,
                        level3 = 0.3f,
                        level4 = 0.457f,
                        level5 = 0.1f,
                    ),
                ),
            ),
        )
    }

    private data class LogAreaFit(
        val valueAtReferenceArea: Float,
        val changePerAreaStop: Float,
    ) {
        fun evaluate(areaStops: Float): Float =
            valueAtReferenceArea + changePerAreaStop * areaStops
    }
}
