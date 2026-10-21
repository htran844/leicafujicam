package vn.hoang.leicafujicam.raw

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Test

class CanonR5SeedCompilerTest {
    private fun asset(): ByteArray = File("src/main/assets/${CanonR5SeedCompiler.ASSET_PATH}").readBytes()

    @Test
    fun sixStylesMatchOriginalSelectorAcrossAllR5BranchBoundaries() {
        val compiler = CanonR5SeedCompiler.fromAsset(asset())
        val rows = requireNotNull(javaClass.getResourceAsStream("/canon/r5_seed_selector_golden.tsv"))
            .bufferedReader().use { it.readLines() }
            .filter { it.isNotBlank() && !it.startsWith("#") }
        assertEquals(1152, rows.size)
        for (row in rows) {
            val fields = row.split('\t')
            val style = CanonPictureStyle.entries.single { it.dppStyleId == fields[0].toInt() }
            val seeds = compiler.compile(CanonR5SeedCompiler.Input(
                style = style,
                highlightTonePriority = fields[1] == "1",
                parameter1000a = fields[2].toInt(),
                flag1e0001 = fields[3] == "1",
            ))
            assertEquals(row, fields[5].toInt(), seeds.scale)
            val output = ByteBuffer.allocate(2048).order(ByteOrder.LITTLE_ENDIAN)
            seeds[fields[4].toInt()].copyWords().forEach { output.putShort(it.toShort()) }
            val digest = MessageDigest.getInstance("SHA-256").digest(output.array())
                .joinToString("") { "%02x".format(it) }
            assertEquals(row, fields[6], digest)
        }
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsChangedAssetBeforeLoadingTables() {
        val data = asset()
        data[data.lastIndex] = (data.last().toInt() xor 1).toByte()
        CanonR5SeedCompiler.fromAsset(data)
    }
}
