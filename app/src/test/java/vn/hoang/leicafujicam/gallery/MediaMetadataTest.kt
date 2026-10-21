package vn.hoang.leicafujicam.gallery

import vn.hoang.leicafujicam.raw.RawMetadata
import vn.hoang.leicafujicam.raw.CanonPictureStyle
import vn.hoang.leicafujicam.raw.RawToneMappingParameters
import vn.hoang.leicafujicam.raw.RawRenderingEngine
import org.junit.Assert.assertEquals
import org.junit.Test

class MediaMetadataTest {
    @Test
    fun mergingRawMetadataPreservesEachCanonStyleAndPhotonHdrState() {
        for (style in CanonPictureStyle.entries) {
            for (hdrEnabled in listOf(false, true)) {
                val metadata = MediaMetadata(
                    rawRenderingEngine = RawRenderingEngine.Canon,
                    rawExposureCompensation = 0.75f,
                    rawToneMappingParameters = RawToneMappingParameters(
                        canonPictureStyle = style,
                        canonExposureCompensationEv = -1.25f,
                        usePhotonHdr = hdrEnabled,
                    ),
                )

                val merged = metadata.merge(rawMetadata(iso = 1600))

                assertEquals(1600, merged.iso)
                assertEquals(RawRenderingEngine.Canon, merged.rawRenderingEngine)
                assertEquals(style, merged.rawToneMappingParameters.canonPictureStyle)
                assertEquals(-1.25f, merged.rawToneMappingParameters.canonExposureCompensationEv, 0f)
                assertEquals(0.75f, requireNotNull(merged.rawExposureCompensation), 0f)
                assertEquals(hdrEnabled, merged.rawToneMappingParameters.usePhotonHdr)
            }
        }
    }

    @Test
    fun mergeKeepsExistingIsoWhenRawIsoIsFallback100() {
        val metadata = MediaMetadata(iso = 800)
        val raw = rawMetadata(iso = 100)

        assertEquals(800, metadata.merge(raw).iso)
    }

    @Test
    fun mergeUsesRawIsoWhenRawIsoIsSpecificValue() {
        val metadata = MediaMetadata(iso = 800)
        val raw = rawMetadata(iso = 1600)

        assertEquals(1600, metadata.merge(raw).iso)
    }

    @Test
    fun mergeUsesFallback100WhenNoExistingIso() {
        val metadata = MediaMetadata(iso = null)
        val raw = rawMetadata(iso = 100)

        assertEquals(100, metadata.merge(raw).iso)
    }

    @Test
    fun importedCaptureInfoKeepsSourceExifMakeAndModel() {
        val captureInfo = MediaMetadata(
            brand = "FUJIFILM",
            deviceModel = "X-T5",
            isImported = true,
        ).toCaptureInfo()

        assertEquals("FUJIFILM", captureInfo.make)
        assertEquals("X-T5", captureInfo.model)
    }

    @Test
    fun captureInfoKeepsExposureBiasForExif() {
        val captureInfo = MediaMetadata(
            brand = "FUJIFILM",
            deviceModel = "X-T5",
            exposureBias = -2f / 3f,
            isImported = true,
        ).toCaptureInfo()

        assertEquals(-2f / 3f, captureInfo.exposureBias)
    }

    private fun rawMetadata(iso: Int): RawMetadata {
        return RawMetadata(
            width = 4000,
            height = 3000,
            cfaPattern = RawMetadata.CFA_RGGB,
            blackLevel = floatArrayOf(0f, 0f, 0f, 0f),
            whiteLevel = 1023f,
            whiteBalanceGains = floatArrayOf(1f, 1f, 1f, 1f),
            colorCorrectionMatrix = floatArrayOf(
                1f, 0f, 0f,
                0f, 1f, 0f,
                0f, 0f, 1f
            ),
            iso = iso
        )
    }
}
