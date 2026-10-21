package vn.hoang.leicafujicam.raw

/** Versioned HDRNet recipe shared by gallery metadata and the embedded Photon XMP summary. */
internal object RawPhotonHdrMetadata {
    private const val PROPERTY = "photonHdrNetRatio"
    private const val CONTRACT_PROPERTY = "photonHdrNetRatioContract"
    private const val CURRENT_CONTRACT = "mgc_fast_moments_v25_portrait_mask_v1"
    private const val SHORT_GAIN_PROPERTY = "photonHdrNetSourceToShortGain"
    private const val SHORT_GAIN_CONTRACT_PROPERTY = "photonHdrNetSourceToShortGainContract"
    private const val CURRENT_SHORT_GAIN_CONTRACT = "mgc_fast_moments_v25_final_short_v1"
    private const val POST_EXPOSURE_PROPERTY = "photonHdrNetPostExposureEv"
    private const val POST_EXPOSURE_CONTRACT_PROPERTY =
        "photonHdrNetPostExposureEvContract"
    private const val CURRENT_POST_EXPOSURE_CONTRACT =
        "hdrnet_post_dehaze_viewfinder_rolloff_v2"
    private const val INPUT_EXPOSURE_PROPERTY = "photonHdrNetInputExposureEv"
    private const val INPUT_EXPOSURE_CONTRACT_PROPERTY = "photonHdrNetInputExposureEvContract"
    const val INPUT_EXPOSURE_CONTRACT = "hdrnet_input_viewfinder_v1"
    private const val SUMMARY_INPUT_EXPOSURE = "hdrNetInputExposureEv"
    private const val SUMMARY_INPUT_EXPOSURE_CONTRACT = "hdrNetInputExposureEvContract"

    fun read(properties: Map<String, String>): Float? = properties[PROPERTY]
        ?.toFloatOrNull()
        ?.takeIf { it.isFinite() && it >= 1f }

    fun readFinalShortGain(properties: Map<String, String>): Float? {
        if (properties[SHORT_GAIN_CONTRACT_PROPERTY] != CURRENT_SHORT_GAIN_CONTRACT) return null
        return properties[SHORT_GAIN_PROPERTY]
            ?.toFloatOrNull()
            ?.takeIf { it.isFinite() && it > 0f }
    }

    fun readPostExposureEv(properties: Map<String, String>): Float? {
        // An input-exposure recipe must never resurrect a stale post-exposure value.
        if (INPUT_EXPOSURE_CONTRACT_PROPERTY in properties) return null
        if (properties[POST_EXPOSURE_CONTRACT_PROPERTY] !=
            CURRENT_POST_EXPOSURE_CONTRACT
        ) return null
        return properties[POST_EXPOSURE_PROPERTY]
            ?.toFloatOrNull()
            ?.takeIf {
                it.isFinite() && it in
                    MeteringSystem.RAW_EXPOSURE_MIN_EV..MeteringSystem.RAW_EXPOSURE_MAX_EV
            }
    }

    fun readInputExposureEv(properties: Map<String, String>): Float? {
        if (properties[INPUT_EXPOSURE_CONTRACT_PROPERTY] != INPUT_EXPOSURE_CONTRACT ||
            !isCurrentCaptureContract(properties) ||
            read(properties) == null || readFinalShortGain(properties) == null
        ) return null
        return properties[INPUT_EXPOSURE_PROPERTY]?.toFloatOrNull()?.takeIf(::isValidExposureEv)
    }

    fun write(
        properties: Map<String, String>,
        hdrRatio: Float?,
        finalShortGain: Float? = null,
        postExposureEv: Float? = null,
        inputExposureEv: Float? = null,
    ): Map<String, String> {
        val validRatio = hdrRatio?.takeIf { it.isFinite() && it >= 1f } ?: return properties
        val validShortGain = finalShortGain?.takeIf { it.isFinite() && it > 0f }
        val validInputExposureEv = inputExposureEv?.takeIf(::isValidExposureEv)
        if (inputExposureEv != null &&
            (validInputExposureEv == null || validShortGain == null)
        ) return properties
        var result = properties + mapOf(
            PROPERTY to validRatio.toString(),
            CONTRACT_PROPERTY to CURRENT_CONTRACT,
        )
        if (validShortGain != null) {
            result += mapOf(
                SHORT_GAIN_PROPERTY to validShortGain.toString(),
                SHORT_GAIN_CONTRACT_PROPERTY to CURRENT_SHORT_GAIN_CONTRACT,
            )
        }
        val validPostExposureEv = postExposureEv?.takeIf(::isValidExposureEv)
        if (validInputExposureEv != null) {
            result = result - POST_EXPOSURE_PROPERTY - POST_EXPOSURE_CONTRACT_PROPERTY
            result += mapOf(
                INPUT_EXPOSURE_PROPERTY to validInputExposureEv.toString(),
                INPUT_EXPOSURE_CONTRACT_PROPERTY to INPUT_EXPOSURE_CONTRACT,
            )
        } else if (validPostExposureEv != null) {
            result = result - INPUT_EXPOSURE_PROPERTY - INPUT_EXPOSURE_CONTRACT_PROPERTY
            result += mapOf(
                POST_EXPOSURE_PROPERTY to validPostExposureEv.toString(),
                POST_EXPOSURE_CONTRACT_PROPERTY to CURRENT_POST_EXPOSURE_CONTRACT,
            )
        }
        return result
    }

    /** Only called for an imported DNG containing a valid embedded Photon PGTM. */
    fun restoreFromSummary(
        properties: Map<String, String>,
        summary: String?,
    ): Map<String, String> {
        if (summary == null || summary.length > 256 * 1024 ||
            summary.lineSequence().firstOrNull()?.trim() != "leicafujicam RAW AE SummaryText v1"
        ) return properties
        val fields = mutableMapOf<String, String>()
        for (line in summary.lineSequence()) {
            val separator = line.indexOf('=')
            if (separator <= 0) continue
            val key = line.substring(0, separator).trim()
            if (fields.put(key, line.substring(separator + 1).trim()) != null) return properties
        }
        val shortGain = (fields["hdrNetFinalShortGain"] ?: fields["finalShortGain"])
            ?.toFloatOrNull()?.takeIf { it.isFinite() && it > 0f } ?: return properties
        val ratio = (fields["hdrNetFinalHdrRatio"] ?: fields["finalHdrRatio"])
            ?.toFloatOrNull()?.takeIf { it.isFinite() && it >= 1f } ?: return properties
        if (SUMMARY_INPUT_EXPOSURE in fields || SUMMARY_INPUT_EXPOSURE_CONTRACT in fields) {
            if (fields[SUMMARY_INPUT_EXPOSURE_CONTRACT] != INPUT_EXPOSURE_CONTRACT) return properties
            val inputEv = fields[SUMMARY_INPUT_EXPOSURE]?.toFloatOrNull()
                ?.takeIf(::isValidExposureEv) ?: return properties
            return write(properties, ratio, shortGain, inputExposureEv = inputEv)
        }
        // Earlier Photon summaries used this name exclusively for downstream exposure.
        // Keep it in the legacy field so regeneration can rebuild the old brightness target.
        val legacyPostEv = fields["hdrNetPostExposureEv"]?.toFloatOrNull()
            ?.takeIf(::isValidExposureEv)
        return write(properties, ratio, shortGain, postExposureEv = legacyPostEv)
    }

    fun appendInputExposureSummary(summary: StringBuilder, inputExposureEv: Float?) {
        val exposureEv = inputExposureEv?.takeIf(::isValidExposureEv) ?: return
        summary.appendLine("$SUMMARY_INPUT_EXPOSURE=$exposureEv")
        summary.appendLine("$SUMMARY_INPUT_EXPOSURE_CONTRACT=$INPUT_EXPOSURE_CONTRACT")
    }

    private fun isValidExposureEv(value: Float): Boolean = value.isFinite() && value in
        MeteringSystem.RAW_EXPOSURE_MIN_EV..MeteringSystem.RAW_EXPOSURE_MAX_EV

    fun isCurrentCaptureContract(properties: Map<String, String>): Boolean {
        return properties[CONTRACT_PROPERTY] == CURRENT_CONTRACT
    }
}
