package vn.hoang.leicafujicam.data

import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.floatPreferencesKey
import androidx.datastore.preferences.core.mutablePreferencesOf
import androidx.datastore.preferences.core.stringPreferencesKey
import com.google.gson.JsonParser
import vn.hoang.leicafujicam.camera.IszLensConfig
import vn.hoang.leicafujicam.camera.RawBlackBorderCrop
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class DeviceConfigurationTest {
    @get:Rule val temporaryFolder = TemporaryFolder()

    private fun configuration(overrides: String): DeviceConfiguration = DeviceConfiguration.parse(
        """{"format":"photon_device_configuration","version":1,"name":"Example","overrides":$overrides}""",
    )

    private fun builtIn(name: String): DeviceConfiguration =
        File("src/main/assets/device_configurations/$name.json").inputStream().use(DeviceConfiguration::read)

    @Test
    fun multipleModelsAndLegacySingleModelUseTheSameRepresentation() {
        val x8 = builtIn("oppo_x8_ultra")
        assertEquals(listOf("PKJ110", "PKU110"), x8.models)
        assertEquals(x8.models, DeviceConfiguration.parse(x8.serialize()).models)
        assertEquals(listOf("PMA110"), builtIn("oppo_x9_ultra").models)

        val legacy = DeviceConfiguration.parse(
            """{"format":"photon_device_configuration","version":1,"name":"Legacy","model":"PKJ110","overrides":{"preferred_main_camera_id":"2"}}""",
        )
        assertEquals(listOf("PKJ110"), legacy.models)
        val serialized = JsonParser.parseString(legacy.serialize()).asJsonObject
        assertFalse(serialized.has("model"))
        assertEquals("PKJ110", serialized["models"].asJsonArray.single().asString)
        assertTrue(configuration("""{"preferred_main_camera_id":"2"}""").models.isEmpty())
    }

    @Test
    fun rejectsInvalidOrAmbiguousModelLists() {
        listOf(
            """"models":"PKJ110"""",
            """"models":[]""",
            """"models":null""",
            """"models":[" "]""",
            """"models":[" PKJ110"]""",
            """"models":[110]""",
            """"models":[null]""",
            """"models":["PKJ110","PKJ110"]""",
            """"model":"PKJ110","models":["PKU110"]""",
        ).forEach { metadata ->
            assertThrows(Exception::class.java) {
                DeviceConfiguration.parse(
                    """{"format":"photon_device_configuration","version":1,"name":"Invalid",$metadata,"overrides":{"preferred_main_camera_id":"2"}}""",
                )
            }
        }
    }

    @Test
    fun builtInsApplyExactValuesAndPreserveUnspecifiedSettings() {
        val preferences = mutablePreferencesOf(
            booleanPreferencesKey("show_grid") to true,
            stringPreferencesKey("raw_custom_black_levels") to "99:20.0",
        )
        DeviceConfigurationFields.apply(preferences, builtIn("oppo_x8_ultra"))
        assertEquals("2", preferences[stringPreferencesKey("preferred_main_camera_id")])
        assertEquals("99:20.0,2:59,3:0,4:0", preferences[stringPreferencesKey("raw_custom_black_levels")])
        assertEquals("2:Custom,3:Custom,4:Custom", preferences[stringPreferencesKey("raw_black_level_modes")])
        assertEquals(true, preferences[booleanPreferencesKey("show_grid")])

        val x9 = builtIn("oppo_x9_ultra")
        DeviceConfigurationFields.apply(preferences, x9)
        val firstApply = preferences.toPreferences()
        DeviceConfigurationFields.apply(preferences, x9)
        assertEquals(firstApply, preferences.toPreferences())
        val lenses = IszLensConfig.deserializeList(preferences[stringPreferencesKey("isz_lens_configs")])
        assertEquals(listOf("2", "4"), lenses.map { it.baseCameraId })
        assertEquals(listOf(2f, 2f), lenses.map { it.iszZoomRatio })
        assertEquals(listOf("oplus_agingtest_mode_select_22", "oplus_agingtest_mode_select_19"), lenses.map { it.vendorCaptureProfileId })
        lenses.forEach {
            assertEquals(RawBlackBorderCrop(leftPx = 508), it.migrateLegacyPortraitCrop(90).rawBlackBorderCropForPortraitDisplay(90))
        }
        val noise = JsonParser.parseString(preferences[stringPreferencesKey("raw_noise_profile_ids_by_lens")]).asJsonObject
        assertEquals("builtin_noise_x9_ultra", noise["2"].asString)
        assertEquals("builtin_noise_x9_ultra_3x", noise["4"].asString)
    }

    @Test
    fun mapRemovalDcpDisableAndScalarResetRemainDistinct() {
        val preferences = mutablePreferencesOf(
            stringPreferencesKey("raw_custom_black_levels") to "2:10.0,3:20.0",
            stringPreferencesKey("raw_dcp_ids_by_lens") to """{"2":"old","3":"keep"}""",
            stringPreferencesKey("preferred_macro_camera_id") to "4",
        )
        DeviceConfigurationFields.apply(preferences, configuration("""{
            "raw_custom_black_levels":{"2":null,"4":0},
            "raw_dcp_ids_by_lens":{"2":null},
            "preferred_macro_camera_id":null
        }"""))
        assertEquals("3:20.0,4:0", preferences[stringPreferencesKey("raw_custom_black_levels")])
        val dcp = JsonParser.parseString(preferences[stringPreferencesKey("raw_dcp_ids_by_lens")]).asJsonObject
        assertTrue(dcp["2"].isJsonNull)
        assertEquals("keep", dcp["3"].asString)
        assertNull(preferences[stringPreferencesKey("preferred_macro_camera_id")])
    }

    @Test
    fun applyingConfigurationHandlesLegacyZoomEncodingAndVirtualLensMapKeys() {
        val preferences = mutablePreferencesOf(
            floatPreferencesKey("default_focal_length") to -2f,
            stringPreferencesKey("custom_focal_lengths") to "2x,35,1.5x",
            stringPreferencesKey("raw_custom_black_levels") to "isz:2:2:profile:59.0",
            stringPreferencesKey("openai_api_key_encrypted_v1") to "private",
        )
        DeviceConfigurationFields.apply(preferences, configuration("""{
            "custom_focal_lengths":[-2,35,-1.5],
            "raw_custom_black_levels":{"2":60}
        }"""))
        assertEquals("-2,35,-1.5", preferences[stringPreferencesKey("custom_focal_lengths")])
        assertEquals(-2f, preferences[floatPreferencesKey("default_focal_length")])
        assertEquals("isz:2:2:profile:59.0,2:60", preferences[stringPreferencesKey("raw_custom_black_levels")])
        assertEquals("private", preferences[stringPreferencesKey("openai_api_key_encrypted_v1")])
    }

    @Test
    fun rejectsInvalidValuesAndUnrecognizedFieldsBeforeApplying() {
        listOf(
            """{"openai_api_key":"no"}""",
            """{"preferred_main_camera_id":2}""",
            """{"use_p010":"true"}""",
            """{"nr_level":1.5}""",
            """{"raw_custom_black_levels":{"2":-1}}""",
            """{"vendor_capture_settings":{"2":{"oplus_agingtest_mode_select":128}}}""",
            """{"isz_lens_configs":[{"base_camera_id":"2","isz_zoom_ratio":2,"raw_black_border_crop_left_px":508,"raw_black_border_crop_sensor_left_px":508}]}""",
        ).forEach { invalid -> assertThrows(Exception::class.java) { configuration(invalid) } }
        assertThrows(Exception::class.java) { DeviceConfiguration.parse(builtIn("oppo_x8_ultra").serialize().replace("\"version\": 1", "\"version\": 2")) }
        assertThrows(Exception::class.java) { configuration("{use_p010:true}") }
        assertThrows(Exception::class.java) { DeviceConfiguration.read(ByteArray(DeviceConfiguration.MAX_FILE_BYTES + 1) { 32 }.inputStream()) }
    }

    @Test
    fun inconsistentIszVendorProfileRollsBackWholeDatastoreTransaction() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        val store = PreferenceDataStoreFactory.create(scope = scope) {
            File(temporaryFolder.root, "device.preferences_pb")
        }
        try {
            store.edit { it[stringPreferencesKey("preferred_main_camera_id")] = "0" }
            val before = store.data.first()
            val inconsistent = configuration("""{
                "preferred_main_camera_id":"2",
                "isz_lens_configs":[{"base_camera_id":"2","isz_zoom_ratio":2,"vendor_capture_profile_id":"oplus_agingtest_mode_select_22"}]
            }""")
            try {
                store.edit { DeviceConfigurationFields.apply(it, inconsistent) }
                fail("Expected mismatched ISZ configuration to be rejected")
            } catch (_: IllegalArgumentException) { }
            assertEquals(before, store.data.first())
        } finally {
            scope.cancel()
        }
    }
}
