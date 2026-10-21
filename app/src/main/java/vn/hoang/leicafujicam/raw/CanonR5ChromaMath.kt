package vn.hoang.leicafujicam.raw

/** Fixed R5 color block consumed by 0x22d5a0 after the Q8 UV curve and Q13 mixing.
 * All six recovered recipes enable only region 0 and diagonal UV gains. Validate that
 * contract before specializing the native three-region evaluator; no region is guessed.
 */
internal object CanonR5ChromaMath {
    fun validate(p: CanonRecipeColorMath.R5PixelParameters) {
        fun w(i: Int) = p[0x122 + i]
        require((0..2).all { w(it) == 1 })
        for (base in intArrayOf(45, 55)) {
            require(w(base) == 1 && w(base + 1) == 0 && w(base + 2) == 1 && w(base + 3) == 1)
            require((5..9).all { w(base + it) == 0 })
        }
    }

    private fun sector(code: Int, first: Int, second: Int): Int {
        val quadrant = (code shr 5) and 3
        val u = if (quadrant < 2) first.inv() else first
        val v = if (quadrant == 1 || quadrant == 2) second.inv() else second
        val angle = code and 63
        val a = if (angle > 31) 64 - angle else angle
        val b = if (angle > 31) angle - 32 else 32 - angle
        val value = a * u + b * v
        val result = when ((code shr 8) and 7) {
            0 -> (value * 3) shr 6
            1 -> value shr 4
            2 -> (value * 3) shr 5
            3 -> value shr 3
            4 -> (value * 3) shr 4
            5 -> (((value * 3) shr 1) + value * 3) shr 4
            6 -> value shr 1
            else -> value
        }
        return result.coerceIn(0, 1023)
    }

    private fun radialScale(value: Int, code: Int): Int = minOf(1023, when ((code shr 11) and 7) {
        0 -> (value * 3) shr 2
        1 -> value
        2 -> (value shr 2) + value
        3 -> (value * 3) shr 1
        4 -> value * 2
        5 -> value * 3
        6 -> ((value * 3) shr 1) + value * 3
        else -> value * 8
    })

    private fun shift(value: Int, amount: Int): Int =
        if (amount < 0) value shr (-amount and 31) else value shl (amount and 31)

    fun apply(u: Int, v: Int, colorLuma: Int, p: CanonRecipeColorMath.R5PixelParameters): Pair<Int, Int> {
        fun w(i: Int) = p[0x122 + i]
        val lumaIndex = colorLuma shr 4
        val delta = lumaIndex - w(8)
        val threshold = if (w(4) == 0) w(3) and 2047 else minOf(2047,
            if (delta < 0) ((w(5) * lumaIndex) shr 7) + (w(3) and 2047)
            else ((w(6) * delta.coerceIn(0, 1023)) shr 7) + w(7))
        val sectorA = sector(w(21), u shr 4, v shr 4)
        val sectorB = sector(w(22), u shr 4, v shr 4)
        val sectorWeight = if ((w(21) and 128) != 0) maxOf(sectorA, sectorB) else minOf(sectorA, sectorB)
        val ru = shift(u, w(25) - 4).coerceIn(-1024, 1023).let { if (it < 0) it.inv() else it }
        val rv = shift(v, w(25) - 4).coerceIn(-1024, 1023).let { if (it < 0) it.inv() else it }
        val radius = maxOf(ru + rv * 2, rv + ru * 2)
        val inner = radialScale((radius - threshold * 2).coerceIn(0, 2047), w(23))
        val outer = radialScale(((w(24) and 2047) * 2 - radius).coerceIn(0, 2047), w(24))
        val light = shift(colorLuma, w(28) - 4).coerceIn(0, 1023)
        val lower = ((light * 2 - (w(26) and 2047)).coerceIn(0, 1023) shl ((w(26) shr 11) and 3)).coerceAtMost(1023)
        val upper = (((w(27) and 2047) - light * 2).coerceIn(0, 1023) shl ((w(27) shr 11) and 3)).coerceAtMost(1023)
        val weight = minOf(sectorWeight, inner, outer, lower, upper)
        val gainU = (w(49) * weight) shr 7
        val gainV = (w(59) * weight) shr 7
        return (u + ((u * gainU) shr 10)).coerceIn(-16384, 16383) * 2 to
            (v + ((v * gainV) shr 10)).coerceIn(-16384, 16383) * 2
    }
}
