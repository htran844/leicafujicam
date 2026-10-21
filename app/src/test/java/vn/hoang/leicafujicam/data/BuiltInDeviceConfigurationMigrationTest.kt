package vn.hoang.leicafujicam.data

import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.mutablePreferencesOf
import androidx.datastore.preferences.core.stringPreferencesKey
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class BuiltInDeviceConfigurationMigrationTest {
    @get:Rule val temporaryFolder = TemporaryFolder()

    private fun configurations(): List<DeviceConfiguration> =
        File("src/main/assets/device_configurations").listFiles()!!
            .filter { it.extension == "json" }.map { it.inputStream().use(DeviceConfiguration::read) }

    @Test
    fun firstDatastoreEmissionIncludesDefaultsAndRestartPreservesUserChanges() = runBlocking {
        val file = File(temporaryFolder.root, "startup.preferences_pb")
        val firstJob = SupervisorJob()
        val firstStore = PreferenceDataStoreFactory.create(
            migrations = listOf(BuiltInDeviceConfigurationMigration("OPPO", "PMA110", ::configurations)),
            scope = CoroutineScope(firstJob + Dispatchers.IO),
            produceFile = { file },
        )
        try {
            val firstPreferences = firstStore.data.first()
            assertEquals("2", firstPreferences[stringPreferencesKey("preferred_main_camera_id")])
            assertEquals("oppo/pma110", firstPreferences[BuiltInDeviceConfigurationMigration.APPLIED_DEVICE])
            assertNotNull(firstPreferences[stringPreferencesKey("isz_lens_configs")])
            assertNotNull(firstPreferences[stringPreferencesKey("raw_noise_profile_ids_by_lens")])
            firstStore.edit {
                it[stringPreferencesKey("preferred_main_camera_id")] = "0"
                it.remove(stringPreferencesKey("isz_lens_configs"))
                it[booleanPreferencesKey("show_grid")] = true
            }
        } finally {
            firstJob.cancelAndJoin()
        }

        val restartJob = SupervisorJob()
        val restartedStore = PreferenceDataStoreFactory.create(
            migrations = listOf(BuiltInDeviceConfigurationMigration("oppo", "pma110", {
                error("An applied device must not reload or reapply defaults on restart")
            })),
            scope = CoroutineScope(restartJob + Dispatchers.IO),
            produceFile = { file },
        )
        try {
            val preferences = restartedStore.data.first()
            assertEquals("0", preferences[stringPreferencesKey("preferred_main_camera_id")])
            assertNull(preferences[stringPreferencesKey("isz_lens_configs")])
            assertEquals(true, preferences[booleanPreferencesKey("show_grid")])
        } finally {
            restartJob.cancelAndJoin()
        }
    }

    @Test
    fun bothX8ModelsMatchWithNormalizedDeviceIdentity() = runBlocking {
        for (model in listOf("PKJ110", " pku110 ")) {
            val migration = BuiltInDeviceConfigurationMigration(" OPPO ", model, ::configurations)
            assertTrue(migration.shouldMigrate(emptyPreferences()))
            val preferences = migration.migrate(mutablePreferencesOf(booleanPreferencesKey("show_grid") to true))
            assertEquals("2", preferences[stringPreferencesKey("preferred_main_camera_id")])
            assertEquals("2:59,3:0,4:0", preferences[stringPreferencesKey("raw_custom_black_levels")])
            assertEquals(true, preferences[booleanPreferencesKey("show_grid")])
            assertFalse(migration.shouldMigrate(preferences))
        }
    }

    @Test
    fun unknownDeviceOrDifferentManufacturerDoesNotReceiveDefaults() = runBlocking {
        for ((manufacturer, model) in listOf("OPPO" to "PMA110-extra", "Other" to "PMA110", "OPPO" to "")) {
            val migration = BuiltInDeviceConfigurationMigration(manufacturer, model, ::configurations)
            val current = mutablePreferencesOf(stringPreferencesKey("preferred_main_camera_id") to "custom")
            assertFalse(migration.shouldMigrate(current))
            assertEquals(current, migration.migrate(current))
            assertNull(current[BuiltInDeviceConfigurationMigration.APPLIED_DEVICE])
        }
    }

    @Test
    fun duplicateMatchesAndMissingHardwareIdentityAreRejected() = runBlocking {
        val x9 = configurations().first { "PMA110" in it.models }
        val duplicate = BuiltInDeviceConfigurationMigration("OPPO", "PMA110", { listOf(x9, x9) })
        try {
            duplicate.shouldMigrate(emptyPreferences())
            fail("Expected duplicate match rejection")
        } catch (_: IllegalArgumentException) { }
        val generic = DeviceConfiguration.parse("""{
            "format":"photon_device_configuration","version":1,"name":"Generic",
            "overrides":{"preferred_main_camera_id":"2"}
        }""")
        val missingIdentity = BuiltInDeviceConfigurationMigration("OPPO", "PMA110", { listOf(generic) })
        try {
            missingIdentity.shouldMigrate(emptyPreferences())
            fail("Expected missing hardware identity rejection")
        } catch (_: IllegalArgumentException) { }
    }

    @Test
    fun failedAssetValidationDoesNotCommitAnAppliedMarkerAndCanRetry() = runBlocking {
        var missingAsset = true
        var appliedCount = 0
        val migration = BuiltInDeviceConfigurationMigration(
            "OPPO", "PMA110", ::configurations,
            validateAssets = { require(!missingAsset) },
            onApplied = { appliedCount++ },
        )
        val current = mutablePreferencesOf(stringPreferencesKey("preferred_main_camera_id") to "0")
        try {
            migration.migrate(current)
            fail("Expected asset validation failure")
        } catch (_: IllegalArgumentException) { }
        assertEquals("0", current[stringPreferencesKey("preferred_main_camera_id")])
        assertNull(current[BuiltInDeviceConfigurationMigration.APPLIED_DEVICE])
        assertEquals(0, appliedCount)
        assertTrue(migration.shouldMigrate(current))
        missingAsset = false
        val applied = migration.migrate(current)
        migration.cleanUp()
        assertEquals("2", applied[stringPreferencesKey("preferred_main_camera_id")])
        assertEquals(1, appliedCount)
    }
}
