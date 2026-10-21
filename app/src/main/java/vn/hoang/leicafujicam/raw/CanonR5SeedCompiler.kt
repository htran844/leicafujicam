package vn.hoang.leicafujicam.raw

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/**
 * EOS R5 (DPP ModelID 0x80000421, internal index 0x67) branch of RVA 0x126b60.
 * This compiles UInt16 recipe seeds, NOT final gamma curves or a complete render plan.
 *
 * Scope: six built-in styles, ordinary still-color selector mode, output color-space ID 1.
 * DPP HDR/override arguments are fixed to their ordinary branch (0, 0, -1); this does not
 * inspect or alter the host's usePhotonHdr, PGTM or HDR rendering.
 */
internal class CanonR5SeedCompiler private constructor(private val sources: Map<Int, IntArray>) {
    /**
     * These are original selector inputs, not user controls. parameter1000a comes from DPP
     * native ISO property 0x1000a (possibly adjusted by 0x1ff597). The EXIF parser writes
     * the same ISO into the public 0x10026 alias; see iso-property-audit.md.
     * highlightTonePriority is the caller's `property(0x10010) > 1` result.
     * flag1e0001 is the caller's property 0x1e0001; retain its identifier until decoded.
     */
    data class Input(
        val style: CanonPictureStyle,
        val parameter1000a: Int,
        val highlightTonePriority: Boolean,
        val flag1e0001: Boolean,
    )

    /** Never interchangeable with CanonColorMath.CoarseInt32Curve. */
    class SeedCurve internal constructor(val sourceRva: Int, private val words: IntArray) {
        val size: Int get() = words.size
        operator fun get(index: Int): Int = words[index]
        fun copyWords(): IntArray = words.copyOf()
    }

    class Seeds internal constructor(private val slots: List<SeedCurve>) {
        // DPP prepares object slots in the order 0, 1, 3, 2. Indices here are the selector
        // argument, not inferred gamma_y/c/uv names or object memory offsets.
        operator fun get(selectorSlot: Int): SeedCurve = slots[selectorSlot]
        val scale: Int get() = 100
    }

    fun compile(input: Input): Seeds = Seeds(List(4) { slot ->
        val rva = selectSource(slot, input)
        val source = requireNotNull(sources[rva]) { "Missing audited Canon seed table" }
        val words = source.copyOf()
        if (input.flag1e0001 && slot != 3) {
            val threshold = when (slot) { 0 -> 66.0; 1 -> 1023.0; else -> 445.0 }
            val endpointScale = when (slot) { 0 -> 0.8; 1 -> 1.0; else -> 0.85 }
            for (index in words.indices) {
                val x = index.toDouble()
                val address = if (x <= threshold) index else {
                    ((((x - threshold) * (endpointScale - 1.0)) / (1024.0 - threshold) + 1.0) * x)
                        .toInt().coerceIn(0, 1023)
                }
                words[index] = source[address]
                if (slot == 2 && input.parameter1000a == 102400 && x < 22.0) {
                    // Original constant at RVA 0x426ae0 and uint16 store/clip order.
                    words[index] = ((x * 34.54545454545455 + 256.0).toInt() and 0xffff)
                        .coerceAtMost(1016)
                }
            }
        }
        SeedCurve(rva, words)
    })

    private fun selectSource(slot: Int, input: Input): Int {
        val neutralFamily = input.style == CanonPictureStyle.Neutral ||
            input.style == CanonPictureStyle.Faithful
        val priority = input.highlightTonePriority
        val at50 = input.parameter1000a == 50
        return when (slot) {
            0 -> when {
                priority -> 0x3ffad0
                at50 -> 0x400ad0
                neutralFamily -> 0x3fead0
                else -> 0x3fe2d0
            }
            1 -> if (neutralFamily) 0x4042d0 else 0x3cfad0
            2 -> when {
                priority && neutralFamily -> 0x3e02d0
                priority -> if (input.parameter1000a < 1600) 0x4032d0 else 0x403ad0
                neutralFamily -> if (at50) 0x3d82d0 else 0x402ad0
                at50 -> 0x4012d0
                input.parameter1000a < 1600 -> 0x401ad0
                else -> 0x4022d0
            }
            3 -> 0x3c62d0
            else -> error("Invalid Canon seed selector slot")
        }
    }

    companion object {
        const val ASSET_PATH = "canon/eos_r5/recipe_seeds.bin"
        private const val MODEL_ID = 0x80000421L
        private const val ASSET_SHA256 = "42722abfd6e478e84ee567b90c8cde34abb861046119a67f117c1863b7f36f40"
        private const val DLL_SHA256 = "534157b14d45168e203beaec2176e6d0867e679b47c42f4b325e1413691d3a6c"
        private val SOURCE_RVAS = setOf(
            0x3c62d0, 0x3cfad0, 0x3d82d0, 0x3e02d0, 0x3fe2d0, 0x3fead0,
            0x3ffad0, 0x400ad0, 0x4012d0, 0x401ad0, 0x4022d0, 0x402ad0,
            0x4032d0, 0x403ad0, 0x4042d0,
        )

        /** All bytes and identities are checked before any source table can be used. */
        fun fromAsset(bytes: ByteArray): CanonR5SeedCompiler {
            val data = bytes.copyOf()
            require(hex(MessageDigest.getInstance("SHA-256").digest(data)) == ASSET_SHA256) {
                "Canon R5 seed asset fingerprint mismatch"
            }
            val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
            require(buffer.int == 0x31535243 && buffer.int == 1) { "Invalid Canon seed format" }
            require(buffer.int.toLong() and 0xffffffffL == MODEL_ID) { "Wrong Canon seed model" }
            val count = buffer.int
            require(count == SOURCE_RVAS.size && data.size == 48 + count * 2052) {
                "Invalid Canon seed asset length"
            }
            val dllHash = ByteArray(32).also(buffer::get)
            require(hex(dllHash) == DLL_SHA256) { "Wrong Canon seed source version" }
            val tables = mutableMapOf<Int, IntArray>()
            repeat(count) {
                val rva = buffer.int
                require(rva in SOURCE_RVAS && rva !in tables) { "Invalid Canon seed record" }
                tables[rva] = IntArray(1024) { buffer.short.toInt() and 0xffff }
            }
            return CanonR5SeedCompiler(tables)
        }

        private fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }
    }
}
