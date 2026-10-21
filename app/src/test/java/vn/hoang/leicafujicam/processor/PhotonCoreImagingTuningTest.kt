package vn.hoang.leicafujicam.processor

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertEquals
import org.junit.Test

class PhotonCoreImagingTuningTest {
    @Test
    fun domainControlsKeepIndependentFixedLayouts() {
        val tuning = PhotonDenoiseTuning(
            lumaStrengthScale = PhotonPyramidScales(1f, 2f, 3f, 4f, 5f),
            detailReconstructionScale = PhotonPyramidScales(6f, 7f, 8f, 9f, 10f),
            outlierRejectionScale = PhotonPyramidScales(2f, 3f, 4f, 5f, 6f),
            chromaStrengthScale = PhotonPyramidScales(5f, 4f, 3f, 2f, 1f),
        )

        assertArrayEquals(
            floatArrayOf(1f, 2f, 3f, 4f, 5f),
            tuning.lumaStrengthScale.toFloatArray(),
            0f,
        )
        assertArrayEquals(
            floatArrayOf(6f, 7f, 8f, 9f, 10f),
            tuning.detailReconstructionScale.toFloatArray(),
            0f,
        )
    }

    @Test
    fun normalizationKeepsControlsInsideOperationalRanges() {
        val fusion = PhotonFusionTuning(
            mergeGradientThreshold = -2f,
            missingReferenceSignal = 2f,
            noiseCorrelationScale = Float.NaN,
        ).normalized()
        val denoise = PhotonDenoiseTuning(
            lumaStrengthScale = PhotonPyramidScales(-1f, 100f, 1f, 1f, 1f),
            sabreLumaNodes = PhotonSabreLumaTuningNodes(snr5 = PhotonPyramidOverrides(-1f)),
        ).normalized()

        assertEquals(-2f, requireNotNull(fusion.mergeGradientThreshold), 0f)
        assertEquals(1f, fusion.missingReferenceSignal, 0f)
        assertEquals(1f, fusion.noiseCorrelationScale, 0f)
        assertArrayEquals(
            floatArrayOf(0f, 16f, 1f, 1f, 1f),
            denoise.lumaStrengthScale.toFloatArray(),
            0f,
        )
        assertEquals(-1f, requireNotNull(PhotonCoreImagingTuning.fusion.mergeGradientThreshold), 0f)
        assertEquals(0f, requireNotNull(denoise.sabreLumaNodes.snr5.level1), 0f)
        val defaults = PhotonCoreImagingTuning.denoise
        assertEquals(PhotonPyramidScales.IDENTITY, defaults.lumaStrengthScale)
        assertEquals(PhotonPyramidScales.IDENTITY, defaults.chromaStrengthScale)
        assertEquals(PhotonPyramidScales.IDENTITY, defaults.detailReconstructionScale)
        assertEquals(PhotonPyramidScales.IDENTITY, defaults.outlierRejectionScale)
        assertEquals(PhotonSabreLumaTuningNodes.DEFAULT, defaults.sabreLumaNodes)
        assertEquals(1f, defaults.frequencyResponse.responseOffset, 0f)
        assertEquals(1f, defaults.noiseSpectrumSeed, 0f)
        assertNull(denoise.sabreLumaRevertNodes.snr5.level1)
        assertNull(denoise.sabreLumaOutlierNodes.snr20.level2)
    }

    @Test
    fun dehazeNormalizationKeepsIndependentOperationalControls() {
        val normalized = PhotonDehazeTuning(
            enabled = true,
            strength = 9f,
            dynamicHighlightStrength = -1f,
        ).normalized()

        assertEquals(4f, normalized.strength, 0f)
        assertEquals(0f, normalized.dynamicHighlightStrength, 0f)
        assertEquals(true, normalized.isActive)
    }

    @Test
    fun missingReferenceSignalUsesPhotonFallbackDirectly() {
        assertEquals(0.12f, resolveFusionReferenceSignal(0.12f, 0.3f), 0f)
        assertEquals(0.3f, resolveFusionReferenceSignal(null, 0.3f), 0f)
        assertEquals(0.18f, resolveFusionReferenceSignal(null, Float.NaN), 0f)
    }
}
