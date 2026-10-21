package vn.hoang.leicafujicam.raw

import vn.hoang.leicafujicam.processor.DenoiseStrength

/**
 * Capture-time multipliers on the HDR+ denoise tuning. Unity keeps the native model
 * amplitude of the selected noise model. Profile coefficients and Photon core tuning
 * are applied independently of these capture-time user-strength multipliers.
 */
object RawDenoiseDefaults {
    const val RAW_MAX_LUMA_STRENGTH = 1.0f
    const val RAW_MAX_CHROMA_STRENGTH = 1.0f

    fun normalize(value: Float): Float = DenoiseStrength.clamp(value)
}
