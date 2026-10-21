package vn.hoang.leicafujicam.data

import androidx.datastore.preferences.core.MutablePreferences
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.floatPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import com.google.gson.GsonBuilder
import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.google.gson.JsonPrimitive
import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import vn.hoang.leicafujicam.camera.CustomVendorKey
import vn.hoang.leicafujicam.camera.CustomFocalLengthValue
import vn.hoang.leicafujicam.camera.CustomVendorKeyTarget
import vn.hoang.leicafujicam.camera.CustomVendorKeyValueType
import vn.hoang.leicafujicam.camera.IszLensConfig
import vn.hoang.leicafujicam.camera.MultiFrameConfig
import vn.hoang.leicafujicam.camera.VendorCaptureKey
import vn.hoang.leicafujicam.camera.VendorCaptureSettings
import vn.hoang.leicafujicam.raw.RawCfaCorrection
import vn.hoang.leicafujicam.raw.RawRenderingEngine
import java.io.ByteArrayOutputStream
import java.io.InputStream

/** A versioned, partial hardware configuration, independent of app backups and personal settings. */
class DeviceConfiguration private constructor(private val document: JsonObject) {
    val name: String get() = document["name"].asString
    val manufacturer: String? get() = document["manufacturer"]?.asString
    val models: List<String> get() = document["models"]?.asJsonArray?.map { it.asString }.orEmpty()
    val overrides: JsonObject get() = document["overrides"].asJsonObject.deepCopy()

    fun serialize(): String = GsonBuilder().setPrettyPrinting().serializeNulls().create().toJson(document)

    companion object {
        const val MAX_FILE_BYTES = 1024 * 1024
        const val FORMAT = "photon_device_configuration"

        fun read(input: InputStream): DeviceConfiguration {
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                require(output.size() + count <= MAX_FILE_BYTES) { "Device configuration exceeds size limit" }
                output.write(buffer, 0, count)
            }
            return parse(output.toString(Charsets.UTF_8.name()))
        }

        fun parse(json: String): DeviceConfiguration {
            val root = JsonReader(json.reader()).use { reader ->
                reader.strictness = Strictness.STRICT
                JsonParser.parseReader(reader).asJsonObject.also {
                    require(reader.peek() == JsonToken.END_DOCUMENT)
                }
            }
            require(root.keySet().all { it in setOf("format", "version", "name", "manufacturer", "models", "model", "overrides") })
            require(root["format"].asString == FORMAT)
            require(root["version"].isJsonPrimitive && root["version"].asJsonPrimitive.isNumber)
            require(root["version"].asBigDecimal.intValueExact() == 1)
            require(root["name"].isJsonPrimitive && root["name"].asJsonPrimitive.isString && root["name"].asString.isNotBlank())
            for (key in listOf("manufacturer", "model")) {
                root[key]?.let { require(it.isJsonPrimitive && it.asJsonPrimitive.isString && it.asString.isNotBlank()) }
            }
            require(!(root.has("model") && root.has("models"))) { "Use either model or models, not both" }
            // Accept existing single-model files, but keep one representation for built-in model matching.
            root.remove("model")?.let { model ->
                root.add("models", JsonArray().apply { add(model.asString.trim()) })
            }
            root["models"]?.let { models ->
                require(models.isJsonArray && models.asJsonArray.size() > 0)
                models.asJsonArray.forEach { model ->
                    require(model.isJsonPrimitive && model.asJsonPrimitive.isString)
                    require(model.asString.isNotBlank() && model.asString == model.asString.trim())
                }
                require(models.asJsonArray.map { it.asString }.distinct().size == models.asJsonArray.size())
            }
            DeviceConfigurationFields.validate(root["overrides"].asJsonObject)
            return DeviceConfiguration(root.deepCopy())
        }
    }
}

/** Each field owns its validation, persisted representation and partial-override semantics. */
internal object DeviceConfigurationFields {
    private enum class Storage { STRING, BOOLEAN, INT, FLOAT, CSV_LIST, CSV_MAP, JSON }

    private class Field(
        val name: String,
        val storage: Storage,
        val validateValue: (JsonElement) -> Unit,
        val merge: (JsonElement?, JsonElement) -> JsonElement = { _, incoming -> incoming },
    ) {
        fun read(preferences: Preferences): JsonElement? = when (storage) {
            Storage.BOOLEAN -> preferences[booleanPreferencesKey(name)]?.let(::JsonPrimitive)
            Storage.INT -> preferences[intPreferencesKey(name)]?.let(::JsonPrimitive)
            Storage.FLOAT -> preferences[floatPreferencesKey(name)]?.let(::JsonPrimitive)
            else -> preferences[stringPreferencesKey(name)]?.let { stored ->
                when (storage) {
                    Storage.CSV_LIST -> JsonArray().apply {
                        stored.split(',').filter(String::isNotEmpty).forEach {
                            add(when (name) {
                                "custom_focal_lengths" -> JsonPrimitive(requireNotNull(CustomFocalLengthValue.parsePersisted(it)))
                                "hidden_focal_lengths" -> JsonPrimitive(it.toFloat())
                                else -> JsonPrimitive(it)
                            })
                        }
                    }
                    Storage.CSV_MAP -> JsonObject().apply {
                        stored.split(',').filter(String::isNotEmpty).forEach {
                            val separator = it.lastIndexOf(':')
                            require(separator > 0)
                            val value = it.substring(separator + 1)
                            add(it.substring(0, separator), when (name) {
                                "camera_orientation_offsets" -> JsonPrimitive(value.toInt())
                                "raw_custom_black_levels", "raw_custom_white_levels" -> JsonPrimitive(value.toFloat())
                                else -> JsonPrimitive(value)
                            })
                        }
                    }
                    Storage.JSON -> JsonParser.parseString(stored)
                    else -> JsonPrimitive(stored)
                }
            }
        }

        fun write(preferences: MutablePreferences, incoming: JsonElement) {
            if (incoming.isJsonNull) {
                when (storage) {
                    Storage.BOOLEAN -> preferences.remove(booleanPreferencesKey(name))
                    Storage.INT -> preferences.remove(intPreferencesKey(name))
                    Storage.FLOAT -> preferences.remove(floatPreferencesKey(name))
                    else -> preferences.remove(stringPreferencesKey(name))
                }
                return
            }
            val value = merge(read(preferences), incoming)
            when (storage) {
                Storage.BOOLEAN -> preferences[booleanPreferencesKey(name)] = value.asBoolean
                Storage.INT -> preferences[intPreferencesKey(name)] = value.asInt
                Storage.FLOAT -> preferences[floatPreferencesKey(name)] = value.asFloat
                else -> preferences[stringPreferencesKey(name)] = when (storage) {
                    Storage.CSV_LIST -> value.asJsonArray.joinToString(",") { it.asString }
                    Storage.CSV_MAP -> value.asJsonObject.entrySet().joinToString(",") { "${it.key}:${it.value.asString}" }
                    Storage.JSON -> value.toString()
                    else -> value.asString
                }
            }
        }
    }

    private fun text(value: JsonElement) {
        require(value.isJsonPrimitive && value.asJsonPrimitive.isString && value.asString.isNotBlank())
        require(value.asString == value.asString.trim() && value.asString.none { it.isISOControl() || it == ',' })
    }

    private fun number(value: JsonElement, min: Float = 0f, max: Float = Float.MAX_VALUE) {
        require(value.isJsonPrimitive && value.asJsonPrimitive.isNumber)
        require(value.asFloat.isFinite() && value.asFloat in min..max)
    }

    private fun integer(value: JsonElement, min: Int = Int.MIN_VALUE, max: Int = Int.MAX_VALUE): Int {
        require(value.isJsonPrimitive && value.asJsonPrimitive.isNumber)
        return value.asBigDecimal.intValueExact().also { require(it in min..max) }
    }

    private fun choice(vararg values: String): (JsonElement) -> Unit = { value ->
        text(value)
        require(value.asString in values)
    }

    private fun mergeMap(current: JsonElement?, incoming: JsonElement): JsonElement =
        (current?.asJsonObject?.deepCopy() ?: JsonObject()).apply {
            incoming.asJsonObject.entrySet().forEach { (key, value) ->
                if (value.isJsonNull) remove(key) else add(key, value.deepCopy())
            }
        }

    private fun mapField(name: String, json: Boolean = false, nullableValues: Boolean = false,
                         validateEntry: (JsonElement) -> Unit): Field = Field(
        name, if (json) Storage.JSON else Storage.CSV_MAP,
        validateValue = { value ->
            require(value.isJsonObject)
            value.asJsonObject.entrySet().forEach { (key, entry) ->
                text(JsonPrimitive(key))
                if (!entry.isJsonNull) validateEntry(entry)
            }
        },
        merge = if (nullableValues) { current, incoming ->
            (current?.asJsonObject?.deepCopy() ?: JsonObject()).apply {
                incoming.asJsonObject.entrySet().forEach { (key, value) -> add(key, value.deepCopy()) }
            }
        } else ::mergeMap,
    )

    private fun stringField(name: String, validate: (JsonElement) -> Unit = ::text) =
        Field(name, Storage.STRING, validate)
    private fun booleanField(name: String) = Field(name, Storage.BOOLEAN, {
        require(it.isJsonPrimitive && it.asJsonPrimitive.isBoolean)
    })
    private fun listField(name: String, validateEntry: (JsonElement) -> Unit = ::text) =
        Field(name, Storage.CSV_LIST, { value ->
            require(value.isJsonArray)
            value.asJsonArray.forEach(validateEntry)
            require(value.asJsonArray.toList().distinct().size == value.asJsonArray.size())
        })

    private fun mergeById(current: JsonElement?, incoming: JsonElement, id: (JsonElement) -> String): JsonElement {
        val entries = current?.asJsonArray?.associateByTo(linkedMapOf(), id) ?: linkedMapOf()
        incoming.asJsonArray.forEach { entries[id(it)] = it }
        return JsonArray().apply { entries.values.forEach { add(it.deepCopy()) } }
    }

    private val fields = listOf(
        stringField("preferred_main_camera_id"),
        Field("hdr_plus_frame_count", Storage.INT, {
            integer(it, MultiFrameConfig.MIN_HDR_PLUS_FRAME_COUNT, MultiFrameConfig.MAX_FRAME_COUNT)
        }),
        stringField("preferred_macro_camera_id"),
        listField("custom_lens_ids"),
        listField("lens_id_blacklist"),
        booleanField("enable_logical_multi_camera_discovery"),
        listField("logical_camera_binding_whitelist") {
            text(it)
            require(it.asString.split('/').let { parts -> parts.size == 2 && parts.all(String::isNotBlank) && parts[0] != parts[1] })
        },
        Field("default_focal_length", Storage.FLOAT, { number(it, -Float.MAX_VALUE) }),
        listField("custom_focal_lengths") { number(it, -Float.MAX_VALUE); require(it.asFloat != 0f) },
        listField("hidden_focal_lengths") { number(it); require(it.asFloat > 0f) },
        mapField("camera_orientation_offsets") { require(integer(it) in listOf(0, 90, 180, 270)) },
        mapField("raw_black_level_modes", validateEntry = choice("Default", "Custom")),
        mapField("raw_custom_black_levels") { number(it, 0f, 65535f) },
        mapField("raw_white_level_modes",
            validateEntry = choice("Default", "RAW10", "RAW12", "RAW14", "RAW_SENSOR_65535", "Custom")),
        mapField("raw_custom_white_levels") { number(it, 0f, 65535f) },
        mapField("raw_cfa_correction_modes", validateEntry = choice(*RawCfaCorrection.allModes.toTypedArray())),
        booleanField("raw_lens_shading_correction_enabled"),
        stringField("raw_dcp_id"),
        mapField("raw_dcp_ids_by_lens", json = true, nullableValues = true, validateEntry = ::text),
        stringField("raw_noise_profile_id"),
        mapField("raw_noise_profile_ids_by_lens", json = true, validateEntry = ::text),
        stringField("raw_color_engine", choice(*RawRenderingEngine.entries.map { it.name }.toTypedArray())),
        booleanField("use_p010"),
        booleanField("use_p3_color_space"),
        booleanField("oppo_super_stabilization_enabled"),
        stringField("video_audio_input_id"),
        Field("nr_level", Storage.INT, { integer(it, 0, 4) }),
        Field("edge_level", Storage.INT, { integer(it, 0, 3) }),
        Field("video_nr_level", Storage.INT, { integer(it, 0, 4) }),
        Field("video_edge_level", Storage.INT, { integer(it, 0, 3) }),
        Field("isz_lens_configs", Storage.JSON, ::validateIsz,
            merge = { current, incoming -> mergeById(current, incoming, ::iszId) }),
        mapField("vendor_capture_settings", json = true) { value ->
            require(value.isJsonObject)
            value.asJsonObject.entrySet().forEach { (name, entry) ->
                val key = requireNotNull(VendorCaptureKey.fromPersistedName(name))
                val int = integer(entry)
                require(key.normalizeValue(int) == int)
            }
        },
        Field("custom_vendor_key_settings", Storage.JSON, ::validateCustomVendorKeys,
            merge = { current, incoming -> mergeById(current, incoming) { it.asJsonObject["id"].asString } }),
    ).associateBy { it.name }

    fun validate(overrides: JsonObject) {
        require(overrides.size() > 0)
        overrides.entrySet().forEach { (key, value) ->
            val field = requireNotNull(fields[key]) { "Unknown device setting: $key" }
            if (!value.isJsonNull) {
                try { field.validateValue(value) } catch (error: Exception) {
                    throw IllegalArgumentException("Invalid device setting: $key", error)
                }
            }
        }
    }

    fun apply(preferences: MutablePreferences, configuration: DeviceConfiguration) {
        val overrides = configuration.overrides
        validate(overrides)
        overrides.entrySet().forEach { (name, value) -> fields.getValue(name).write(preferences, value) }
        // Validate the resulting linked settings inside the same transaction; failures roll back every write.
        if (overrides.has("custom_vendor_key_settings")) {
            preferences[stringPreferencesKey("custom_vendor_key_settings")]?.let {
                validateCustomVendorKeys(JsonParser.parseString(it))
            }
        }
        val lenses = overrides["isz_lens_configs"]?.takeUnless { it.isJsonNull }?.let {
            IszLensConfig.deserializeList(it.toString())
        }.orEmpty()
        val vendorSettings = preferences[stringPreferencesKey("vendor_capture_settings")]
            ?.let { JsonParser.parseString(it).asJsonObject }
        lenses.filter { it.vendorCaptureProfileId != null }.forEach { lens ->
            val values = vendorSettings?.getAsJsonObject(lens.virtualCameraId)?.entrySet()
                ?.associate { requireNotNull(VendorCaptureKey.fromPersistedName(it.key)) to it.value.asInt }
                .orEmpty()
            require(lens.vendorCaptureProfileId == VendorCaptureSettings(values).toVirtualLensProfileId()) {
                "ISZ vendor profile does not match lens ${lens.virtualCameraId}"
            }
        }
    }

    private fun iszId(value: JsonElement): String = IszLensConfig.deserializeList("[$value]").single().virtualCameraId

    private fun validateIsz(value: JsonElement) {
        require(value.isJsonArray)
        val ids = mutableSetOf<String>()
        value.asJsonArray.forEach { element ->
            val lens = element.asJsonObject
            val common = setOf("base_camera_id", "isz_zoom_ratio", "is_macro", "vendor_capture_profile_id")
            val portrait = setOf("raw_black_border_crop_left_px", "raw_black_border_crop_top_px", "raw_black_border_crop_right_px", "raw_black_border_crop_bottom_px")
            val sensor = setOf("raw_black_border_crop_sensor_left_px", "raw_black_border_crop_sensor_top_px", "raw_black_border_crop_sensor_right_px", "raw_black_border_crop_sensor_bottom_px")
            require(lens.keySet().all { it in common || it in portrait || it in sensor })
            require(!(lens.keySet().any { it in portrait } && lens.keySet().any { it in sensor }))
            text(lens["base_camera_id"])
            number(lens["isz_zoom_ratio"], 1f, 100f)
            lens["is_macro"]?.let { require(it.isJsonPrimitive && it.asJsonPrimitive.isBoolean) }
            lens["vendor_capture_profile_id"]?.let {
                text(it)
                require(IszLensConfig.sanitizeVendorCaptureProfileId(it.asString) == it.asString)
            }
            (portrait + sensor).forEach { key -> lens[key]?.let { integer(it, 0, 4096) } }
            require(ids.add(iszId(element)))
        }
    }

    private fun validateCustomVendorKeys(value: JsonElement) {
        require(value.isJsonArray)
        val ids = mutableSetOf<String>()
        val validated = mutableListOf<JsonObject>()
        value.asJsonArray.forEach { element ->
            val key = element.asJsonObject
            require(key.keySet().all { it in setOf("id", "key_name", "target", "value_type", "value", "lens_id") })
            text(key["id"])
            require(ids.add(key["id"].asString))
            text(key["key_name"])
            require(CustomVendorKey.isValidKeyName(key["key_name"].asString))
            choice(*CustomVendorKeyTarget.entries.map { it.name }.toTypedArray())(key["target"])
            choice(*CustomVendorKeyValueType.entries.map { it.name }.toTypedArray())(key["value_type"])
            val type = CustomVendorKeyValueType.valueOf(key["value_type"].asString)
            require(type.isValid(integer(key["value"])))
            key["lens_id"]?.takeUnless { it.isJsonNull }?.let(::text)
            val lensId = key["lens_id"]?.takeUnless { it.isJsonNull }?.asString
            require(validated.none { existing ->
                val existingLensId = existing["lens_id"]?.takeUnless { it.isJsonNull }?.asString
                existing["key_name"] == key["key_name"] && existing["target"] == key["target"] &&
                    (existingLensId == lensId ||
                        ((existingLensId == null || lensId == null) && existing["value_type"] != key["value_type"]))
            }) { "Conflicting custom vendor keys" }
            validated.add(key)
        }
    }
}
