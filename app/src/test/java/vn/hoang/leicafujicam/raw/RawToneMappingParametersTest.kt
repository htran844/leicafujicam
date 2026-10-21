package vn.hoang.leicafujicam.raw

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RawToneMappingParametersTest {
    @Test
    fun canonExposureAppliesOnlyToCanonAcrossStylesAndHdrModes() {
        for (style in CanonPictureStyle.entries) {
            for (hdr in listOf(false, true)) {
                val parameters = RawToneMappingParameters(canonPictureStyle = style, usePhotonHdr = hdr)
                for (engine in RawRenderingEngine.entries) {
                    assertEquals(
                        if (engine.isCanon) -0.5f else 0f,
                        parameters.engineExposureCompensationEv(engine), 0f,
                    )
                    assertEquals(
                        if (engine.isCanon) 0.75f else 0f,
                        parameters.copy(canonExposureCompensationEv = 0.75f)
                            .engineExposureCompensationEv(engine), 0f,
                    )
                }
            }
        }
    }

    @Test
    fun photonHdrIsIndependentFromAdobeProfileToneMap() {
        val parameters = RawToneMappingParameters.DEFAULT
            .withPhotonHdr(true)
            .withOppoMasterToneMap(true)

        assertTrue(parameters.usePhotonHdr)
        assertTrue(parameters.useOppoMasterToneMap)
        assertEquals(RawProfileToneMapMode.OppoMaster, parameters.profileToneMapMode)
    }

    @Test
    fun changingAdobeProfileToneMapPreservesPhotonHdr() {
        val parameters = RawToneMappingParameters.DEFAULT
            .withPhotonHdr(true)
            .withProfileToneMapMode(RawProfileToneMapMode.Default)

        assertTrue(parameters.usePhotonHdr)
        assertEquals(RawProfileToneMapMode.Default, parameters.profileToneMapMode)
    }

    @Test
    fun profileAndAcr3ToneMapsRemainDistinctSelections() {
        val profile = RawToneMappingParameters.DEFAULT
            .withProfileToneMapMode(RawProfileToneMapMode.Profile)
        val acr3 = profile.withProfileToneMapMode(RawProfileToneMapMode.Default)

        assertTrue(profile.useProfileToneMap)
        assertEquals(RawProfileToneMapMode.Profile, profile.profileToneMapMode)
        assertEquals(false, acr3.useProfileToneMap)
        assertEquals(RawProfileToneMapMode.Default, acr3.profileToneMapMode)
    }
}
