package vn.hoang.leicafujicam.raw

/**
 * Integer reference for two audited DPP 4.21.30 helpers, not a Picture Style renderer.
 *
 * Evidence: research/canon_dpp/pipeline-decompiled/{1801d76c0,180027990}.c. These
 * functions operate on DPP integer code values; they do not establish the conversion from
 * the app's floating-point working space into that domain.
 */
internal object CanonColorMath {
    /** A final 65,536-entry signed Int32 lookup table consumed by RGBRecipe. */
    class DirectInt32Curve(entries: IntArray) {
        init {
            require(entries.size == 65_536) { "RGBRecipe direct curve requires 65536 Int32 entries" }
        }

        private val values = entries.copyOf()

        internal operator fun get(index: Int): Int = values[index]
    }

    /**
     * A final 1,024-entry signed Int32 lookup table consumed by RGBRecipe; output is shifted
     * left by six bits. This is NOT one of the extracted 1,024-entry UInt16 recipe seed tables.
     * The seed-table compiler must be recovered before those resources can produce this type.
     */
    class CoarseInt32Curve(entries: IntArray) {
        init {
            require(entries.size == 1_024) { "RGBRecipe coarse curve requires 1024 Int32 entries" }
        }

        private val values = entries.copyOf()

        internal operator fun get(index: Int): Int = values[index]
    }

    /**
     * DppCore RVA 0x1d76c0. Accepts every signed Int32 input, clamping only the lookup address
     * to 0..65535. A direct table takes precedence when both pointers are present. No table
     * returns the clamped input. Table results are not clamped; [Int.shl] deliberately retains
     * the original 32-bit wraparound behavior of the coarse-table path.
     */
    fun rgbRecipeLookup(
        input: Int,
        direct: DirectInt32Curve? = null,
        coarse: CoarseInt32Curve? = null,
    ): Int {
        val code = input.coerceIn(0, 65_535)
        if (direct != null) return direct[code]
        if (coarse != null) {
            val index = ((code + 1) shr 6).coerceAtMost(1_023)
            return coarse[index] shl 6
        }
        return code
    }

    /**
     * DppCore RVA 0x027990, used to prepare an auxiliary plane. The three arguments are the
     * original ordered UInt16 channels, represented as Int in 0..65535. Their mapping to an
     * app RGB space is not established, so this must not be substituted for app luminance.
     * The coefficient sum is 4096; the unsigned 32-bit accumulation cannot overflow for
     * valid inputs. The original helper truncates after shifting and clamps to UInt16.
     */
    fun auxiliaryWeightedCode(first: Int, second: Int, third: Int): Int {
        require(first in 0..65_535 && second in 0..65_535 && third in 0..65_535) {
            "Canon auxiliary plane inputs must be UInt16 code values"
        }
        return ((first * 0x4c8 + second * 0x964 + third * 0x1d4) ushr 12)
            .coerceAtMost(65_535)
    }
}
