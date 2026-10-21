package vn.hoang.leicafujicam.raw

import vn.hoang.leicafujicam.processor.GlesComputeWorkGroup
import org.junit.Assert.assertEquals
import org.junit.Test

class RawMeteringDemosaicShadersTest {
    @Test
    fun halfResolutionShaderFitsGles31Baseline() {
        assertEquals(
            GlesComputeWorkGroup.Size(8, 8, 1),
            GlesComputeWorkGroup.declaredSize(RawMeteringDemosaicShaders.HALF_RESOLUTION),
        )
        GlesComputeWorkGroup.requireBaselineCompatible(
            RawMeteringDemosaicShaders.HALF_RESOLUTION,
            "RAW_METERING_HALF_RESOLUTION",
        )
    }
}
