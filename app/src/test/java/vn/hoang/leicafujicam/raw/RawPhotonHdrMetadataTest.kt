package vn.hoang.leicafujicam.raw

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

class RawPhotonHdrMetadataTest {
    @Test
    fun `new input recipe keeps physical AE and removes legacy post exposure`() {
        val legacy = RawPhotonHdrMetadata.write(emptyMap(), 4f, 0.5f, postExposureEv = -2f)
        val updated = RawPhotonHdrMetadata.write(
            legacy, 4f, 0.5f, postExposureEv = -2f, inputExposureEv = -3f,
        )

        assertEquals(4f, RawPhotonHdrMetadata.read(updated)!!, 0f)
        assertEquals(0.5f, RawPhotonHdrMetadata.readFinalShortGain(updated)!!, 0f)
        assertEquals(-3f, RawPhotonHdrMetadata.readInputExposureEv(updated)!!, 0f)
        assertNull(RawPhotonHdrMetadata.readPostExposureEv(updated))
        assertFalse(updated.keys.any { it.contains("PostExposure") })
    }

    @Test
    fun `standalone DNG summary restores the same complete input recipe`() {
        val summary = buildString {
            appendLine("leicafujicam RAW AE SummaryText v1")
            appendLine("finalShortGain=9.0")
            appendLine("finalHdrRatio=2.0")
            appendLine("hdrNetFinalShortGain=0.5")
            appendLine("hdrNetFinalHdrRatio=4.0")
            RawPhotonHdrMetadata.appendInputExposureSummary(this, -2.5f)
        }
        val xmp = DngCameraRawProfileXmp.build("Photon HDR", true, false, summary)
            .toString(Charsets.UTF_8)
        val restored = RawPhotonHdrMetadata.restoreFromSummary(
            emptyMap(), DngCameraRawProfileXmp.readSceneExposureSummary(xmp),
        )

        assertEquals(0.5f, RawPhotonHdrMetadata.readFinalShortGain(restored)!!, 0f)
        assertEquals(4f, RawPhotonHdrMetadata.read(restored)!!, 0f)
        assertEquals(-2.5f, RawPhotonHdrMetadata.readInputExposureEv(restored)!!, 0f)
        assertNull(RawPhotonHdrMetadata.readPostExposureEv(restored))
    }

    @Test
    fun `legacy summary keeps post exposure separate and never relabels it`() {
        val restored = RawPhotonHdrMetadata.restoreFromSummary(
            emptyMap(),
            """
                leicafujicam RAW AE SummaryText v1
                hdrNetFinalShortGain=0.5
                hdrNetFinalHdrRatio=4.0
                hdrNetPostExposureEv=-1.88388
            """.trimIndent(),
        )

        assertEquals(-1.88388f, RawPhotonHdrMetadata.readPostExposureEv(restored)!!, 0f)
        assertNull(RawPhotonHdrMetadata.readInputExposureEv(restored))
    }

    @Test
    fun `unknown input contract never falls back to a legacy post value`() {
        val legacy = RawPhotonHdrMetadata.write(emptyMap(), 4f, 0.5f, postExposureEv = -2f)
        val unknown = legacy + mapOf(
            "photonHdrNetInputExposureEv" to "-3.0",
            "photonHdrNetInputExposureEvContract" to "unknown",
        )
        assertNull(RawPhotonHdrMetadata.readInputExposureEv(unknown))
        assertNull(RawPhotonHdrMetadata.readPostExposureEv(unknown))
    }

    @Test
    fun `ambiguous or incomplete embedded recipe cannot replace gallery state`() {
        val existing = mapOf("unrelated" to "preserved")
        val prefix = "leicafujicam RAW AE SummaryText v1\nfinalShortGain=0.5\nfinalHdrRatio=4.0\n"
        for (suffix in listOf(
            "hdrNetInputExposureEv=-2.5",
            "hdrNetInputExposureEv=NaN\nhdrNetInputExposureEvContract=${RawPhotonHdrMetadata.INPUT_EXPOSURE_CONTRACT}",
            "finalShortGain=0.75",
        )) {
            assertEquals(existing, RawPhotonHdrMetadata.restoreFromSummary(existing, prefix + suffix))
        }
    }
}
