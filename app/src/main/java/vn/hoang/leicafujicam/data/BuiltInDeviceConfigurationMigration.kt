package vn.hoang.leicafujicam.data

import android.content.Context
import android.os.Build
import androidx.datastore.core.DataMigration
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.stringPreferencesKey
import vn.hoang.leicafujicam.raw.DcpManager
import vn.hoang.leicafujicam.raw.RawNoiseProfileManager
import vn.hoang.leicafujicam.utils.PLog
import java.util.Locale

/** Installs hardware defaults before any preference reader (including camera discovery) runs. */
internal class BuiltInDeviceConfigurationMigration(
    manufacturer: String,
    model: String,
    private val loadConfigurations: () -> List<DeviceConfiguration>,
    private val validateAssets: (DeviceConfiguration) -> Unit = {},
    private val onApplied: (DeviceConfiguration) -> Unit = {},
) : DataMigration<Preferences> {
    private val deviceManufacturer = manufacturer.trim().lowercase(Locale.ROOT)
    private val deviceModel = model.trim().lowercase(Locale.ROOT)
    private val deviceIdentity = "$deviceManufacturer/$deviceModel"
    private var migratedConfiguration: DeviceConfiguration? = null

    private val matchingConfiguration: DeviceConfiguration? by lazy {
        val configurations = loadConfigurations()
        configurations.forEach { configuration ->
            require(!configuration.manufacturer.isNullOrBlank() && configuration.models.isNotEmpty()) {
                "Built-in configuration must declare manufacturer and models: ${configuration.name}"
            }
        }
        val matches = configurations.filter { configuration ->
            configuration.manufacturer?.trim()?.equals(deviceManufacturer, ignoreCase = true) == true &&
                configuration.models.any { it.equals(deviceModel, ignoreCase = true) }
        }
        require(matches.size <= 1) { "Multiple built-in configurations match $deviceIdentity" }
        matches.singleOrNull()
    }

    override suspend fun shouldMigrate(currentData: Preferences): Boolean {
        if (currentData[APPLIED_DEVICE] == deviceIdentity) return false
        return matchingConfiguration != null
    }

    override suspend fun migrate(currentData: Preferences): Preferences {
        if (currentData[APPLIED_DEVICE] == deviceIdentity) return currentData
        val configuration = matchingConfiguration ?: return currentData
        validateAssets(configuration)
        return currentData.toMutablePreferences().apply {
            DeviceConfigurationFields.apply(this, configuration)
            this[APPLIED_DEVICE] = deviceIdentity
        }.also { migratedConfiguration = configuration }
    }

    override suspend fun cleanUp() {
        migratedConfiguration?.let(onApplied)
        migratedConfiguration = null
    }

    companion object {
        const val APPLIED_DEVICE_KEY_NAME = "builtin_device_configuration_applied_v1"
        val APPLIED_DEVICE = stringPreferencesKey(APPLIED_DEVICE_KEY_NAME)
        private const val ASSET_DIRECTORY = "device_configurations"

        fun create(context: Context): BuiltInDeviceConfigurationMigration {
            val appContext = context.applicationContext
            return BuiltInDeviceConfigurationMigration(
                manufacturer = Build.MANUFACTURER,
                model = Build.MODEL,
                loadConfigurations = {
                    appContext.assets.list(ASSET_DIRECTORY).orEmpty()
                        .filter { it.endsWith(".json") }.sorted().map { file ->
                            appContext.assets.open("$ASSET_DIRECTORY/$file").use(DeviceConfiguration::read)
                        }
                },
                validateAssets = { configuration ->
                    val overrides = configuration.overrides
                    fun requireProfiles(single: String, perLens: String, availableIds: () -> Set<String>) {
                        val requested = buildList {
                            overrides[single]?.takeUnless { it.isJsonNull }?.let { add(it.asString) }
                            overrides[perLens]?.takeUnless { it.isJsonNull }?.asJsonObject
                                ?.entrySet()?.forEach { (_, value) ->
                                    if (!value.isJsonNull) add(value.asString)
                                }
                        }
                        if (requested.isNotEmpty()) {
                            val missing = requested.toSet() - availableIds()
                            require(missing.isEmpty()) { "Missing built-in calibration assets: $missing" }
                        }
                    }
                    requireProfiles("raw_dcp_id", "raw_dcp_ids_by_lens") {
                        DcpManager(appContext).getAvailableDcps()
                            .filter { it.isBuiltIn }.map { it.id }.toSet()
                    }
                    requireProfiles("raw_noise_profile_id", "raw_noise_profile_ids_by_lens") {
                        RawNoiseProfileManager(appContext).getAvailableProfiles()
                            .filter { it.isBuiltIn && it.id != RawNoiseProfileManager.ADAPTIVE_PROFILE_ID }
                            .map { it.id }.toSet()
                    }
                },
                onApplied = { configuration ->
                    PLog.i("DeviceConfiguration", "Applied built-in defaults: ${configuration.name} (${Build.MODEL})")
                },
            )
        }
    }
}
