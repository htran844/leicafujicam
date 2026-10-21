package vn.hoang.leicafujicam.raw

object RawSharpeningDefaults {
    const val DEFAULT_STRENGTH = 0.4f
    const val ALGORITHM_STRENGTH_SCALE = 2f
    const val MAX_ALGORITHM_STRENGTH = 2f

    fun normalize(value: Float): Float = if (value.isFinite()) {
        value.coerceIn(0f, 1f)
    } else {
        DEFAULT_STRENGTH
    }

    /** MGC: the default UI value is unity, so 0/.4/1 map to 0/1/2.5. */
    fun toMgcStrength(value: Float): Float = normalize(value) / DEFAULT_STRENGTH

    /** Maps the persisted/UI 0..1 control onto the legacy GLES USM 0..2 domain. */
    fun toAlgorithmStrength(value: Float): Float =
        normalize(value) * ALGORITHM_STRENGTH_SCALE
}
