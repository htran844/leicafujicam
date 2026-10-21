package vn.hoang.leicafujicam.raw

import java.io.File
import com.google.gson.JsonParser
import org.junit.Assert.assertEquals
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class CanonRecipeColorMathTest {
    @Test
    fun publicIsoAloneCannotLoadTheMissingNativeIsoRecipe() {
        val repository = sequenceOf(File("."), File("..")).first {
            File(it, "research/canon_dpp/r5-pixel-golden").isDirectory
        }
        val assets = File(repository, "app/src/main/assets")
        val manifestPath = "canon/eos_r5/standard/gamma-manifest.json"
        for (nativeIso in listOf(null, "00000000")) {
            val manifest = JsonParser.parseString(File(assets, manifestPath).readText()).asJsonObject
            val properties = manifest.getAsJsonObject("modelProvider").getAsJsonObject("properties")
            // This was the original bug: public ISO100 was present, but the native
            // provider saw a missing/zero ISO and silently selected another gamma_c.
            assertEquals("64000000", properties["0x10026"].asString)
            if (nativeIso == null) properties.remove("0x1000a")
            else properties.addProperty("0x1000a", nativeIso)
            assertThrows(IllegalArgumentException::class.java) {
                CanonProfile.load(CanonPictureStyle.Standard) { path ->
                    if (path == manifestPath) manifest.toString().toByteArray()
                    else File(assets, path).readBytes()
                }
            }
        }
    }

    @Test
    fun r5DispatchYuvAndRgbConversionMatchOriginalDppForAllSixStyles() {
        val repository = sequenceOf(File("."), File("..")).first {
            File(it, "research/canon_dpp/r5-pixel-golden").isDirectory
        }
        val assetRoot = File(repository, "app/src/main/assets")
        val assets = File(assetRoot, "canon/eos_r5")
        for (style in listOf("standard", "portrait", "landscape", "neutral", "faithful", "monochrome")) {
            val directory = File(assets, style)
            val plan = CanonProfile.load(CanonPictureStyle.fromPersistedValue(style)) {
                File(assetRoot, it).readBytes()
            }
            assertEquals(style, plan.style.persistedValue)
            assertEquals(33, plan.icc.grid)
            val parameters = CanonRecipeColorMath.R5PixelParameters(plan.kernelWords)
            val recombination = CanonRecipeColorMath.R5RecombinationParameters.fromRawParameters(
                File(directory, "parameters.bin").readBytes(),
            )
            fun table(offset: Int, size: Int) = IntArray(size) { plan.lookupAtlas[offset + it].toInt() }
            val y = CanonRecipeColorMath.RawGammaTable(table(0, 131072))
            val c = CanonRecipeColorMath.RawGammaTable(table(131072, 131072))
            val uv = CanonRecipeColorMath.R5UvGainTable(table(262144, 65536))
            val weightTables = List(4) {
                CanonRecipeColorMath.ChromaWeightTable(table(327680 + it * 1024, 1024))
            }
            File(repository, "research/canon_dpp/r5-render-golden/$style.tsv").forEachLine { row ->
                if (!row.startsWith("#") && row.isNotBlank()) {
                    val values = row.split('\t').map { it.toInt() }
                    val result = CanonRecipeColorMath.renderR5Pixel(
                        values[2], values[1], values[0], values[3], parameters, y, c, uv,
                        weightTables[0], weightTables[1], weightTables[2], weightTables[3], recombination,
                    )
                    assertEquals("$style $row", CanonRecipeColorMath.Yuv(
                        values[4], values[5], values[6],
                    ), result)
                    assertArrayEquals("$style YUV to RGB $row", values.subList(7, 10).toIntArray(),
                        CanonRecipeColorMath.yuvToRgb(result))
                }
            }
        }
    }
}
