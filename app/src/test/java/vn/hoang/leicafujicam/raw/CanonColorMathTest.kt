package vn.hoang.leicafujicam.raw

import org.junit.Assert.assertEquals
import org.junit.Test

class CanonColorMathTest {
    @Test
    fun matchesOriginalDppHelperOutputsAtIntegerBoundaries() {
        // Synthetic Int32 tables are identical to generate_color_math_golden.py. Expected
        // results come from executing the original DLL instructions, not this Kotlin formula.
        val direct = CanonColorMath.DirectInt32Curve(IntArray(65_536) { index ->
            (index shl 16) or (65_535 - index)
        })
        val coarseValues = IntArray(1_024) { it }
        coarseValues[0] = -2
        coarseValues[1] = Int.MAX_VALUE
        coarseValues[2] = Int.MIN_VALUE
        coarseValues[3] = 0x04000001
        coarseValues[1023] = 0x01000001
        val coarse = CanonColorMath.CoarseInt32Curve(coarseValues)

        val rows = requireNotNull(javaClass.getResourceAsStream("/canon/color_math_golden.tsv"))
            .bufferedReader().use { it.readLines() }
            .filter { it.isNotBlank() && !it.startsWith("#") }
        require(rows.isNotEmpty())
        for (row in rows) {
            val fields = row.split('\t')
            require(fields.size == 5)
            val first = fields[1].toInt()
            val actual = when (fields[0]) {
                "none" -> CanonColorMath.rgbRecipeLookup(first)
                "direct" -> CanonColorMath.rgbRecipeLookup(first, direct = direct)
                "coarse" -> CanonColorMath.rgbRecipeLookup(first, coarse = coarse)
                "both" -> CanonColorMath.rgbRecipeLookup(first, direct, coarse)
                "aux" -> CanonColorMath.auxiliaryWeightedCode(
                    first, fields[2].toInt(), fields[3].toInt(),
                )
                else -> error("Unknown Canon oracle case: $row")
            }
            assertEquals(row, fields[4].toInt(), actual)
        }
    }

    @Test(expected = IllegalArgumentException::class)
    fun directTableRejectsCoarseCardinality() {
        CanonColorMath.DirectInt32Curve(IntArray(1_024))
    }

    @Test(expected = IllegalArgumentException::class)
    fun coarseTableRejectsDirectCardinality() {
        CanonColorMath.CoarseInt32Curve(IntArray(65_536))
    }

    @Test(expected = IllegalArgumentException::class)
    fun auxiliaryCodeRejectsValuesOutsideTheAuditedInputDomain() {
        CanonColorMath.auxiliaryWeightedCode(65_536, 0, 0)
    }
}
