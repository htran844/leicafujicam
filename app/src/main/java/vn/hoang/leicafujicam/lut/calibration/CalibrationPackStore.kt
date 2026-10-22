package vn.hoang.leicafujicam.lut.calibration

import android.content.Context
import android.os.Build
import org.json.JSONArray
import org.json.JSONObject
import vn.hoang.leicafujicam.utils.PLog
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Persists an on-device calibration as a bake-ready device pack.
 *
 * Layout on disk mirrors the assets layout the app consumes:
 *   files/device_calibration/
 *     manifest.json          (device info, saved custom LUT ids, Delta E report)
 *     correction.plut        (the device correction alone, used for verification)
 *     <device-key>/          (the pack itself)
 *       *.plut               (calibrated style LUTs, built-in file names)
 *       config.json          (built-in config entries for those styles)
 *       report.json          (Delta E before/after summary)
 *
 * The export ZIP wraps the pack as "devices/<key>/..." plus a
 * "devices/index.json", so it can be dropped into app assets verbatim when
 * baking the calibration into a release build for all users.
 */
object CalibrationPackStore {
    private const val TAG = "CalibrationPackStore"
    private const val ROOT_DIR = "device_calibration"
    private const val MANIFEST_FILE = "manifest.json"
    private const val CORRECTION_FILE = "correction.plut"

    data class SavedStyle(
        val styleId: String,
        val fileName: String,
        val customLutId: String
    )

    fun deviceKey(
        manufacturer: String = Build.MANUFACTURER ?: "",
        model: String = Build.MODEL ?: ""
    ): String {
        val raw = (manufacturer + "_" + model).lowercase()
        return raw.replace(Regex("[^a-z0-9]+"), "_").trim('_').ifBlank { "unknown_device" }
    }

    private fun rootDir(context: Context): File = File(context.filesDir, ROOT_DIR)

    fun packDir(context: Context): File = File(rootDir(context), deviceKey())

    fun manifestFile(context: Context): File = File(rootDir(context), MANIFEST_FILE)

    fun loadManifest(context: Context): JSONObject? {
        val file = manifestFile(context)
        if (!file.exists()) return null
        return try {
            JSONObject(file.readText())
        } catch (e: Exception) {
            PLog.w(TAG, "Cannot read calibration manifest", e)
            null
        }
    }

    fun savedStyles(manifest: JSONObject): List<SavedStyle> {
        val array = manifest.optJSONArray("styles") ?: return emptyList()
        return (0 until array.length()).mapNotNull { i ->
            val item = array.optJSONObject(i) ?: return@mapNotNull null
            SavedStyle(
                styleId = item.optString("styleId"),
                fileName = item.optString("fileName"),
                customLutId = item.optString("customLutId")
            )
        }
    }

    /**
     * Write the pack, the standalone correction and the manifest.
     * [plutByFileName] keys are bare file names (e.g. "leica.plut").
     */
    fun savePack(
        context: Context,
        plutByFileName: Map<String, ByteArray>,
        configEntries: JSONArray,
        report: JSONObject,
        correctionBytes: ByteArray,
        savedStyles: List<SavedStyle>
    ): JSONObject {
        val root = rootDir(context)
        val pack = packDir(context)
        pack.deleteRecursively()
        pack.mkdirs()
        for ((fileName, bytes) in plutByFileName) {
            File(pack, fileName).writeBytes(bytes)
        }
        File(pack, "config.json").writeText(
            JSONObject().put("luts", configEntries).toString(2)
        )
        File(pack, "report.json").writeText(report.toString(2))
        File(root, CORRECTION_FILE).writeBytes(correctionBytes)

        val stylesJson = JSONArray()
        savedStyles.forEach { style ->
            stylesJson.put(
                JSONObject()
                    .put("styleId", style.styleId)
                    .put("fileName", style.fileName)
                    .put("customLutId", style.customLutId)
            )
        }
        val manifest = JSONObject()
            .put("deviceKey", deviceKey())
            .put("manufacturer", Build.MANUFACTURER ?: "")
            .put("model", Build.MODEL ?: "")
            .put("createdAt", System.currentTimeMillis())
            .put("meanBefore", report.optDouble("meanBefore", 0.0))
            .put("maxBefore", report.optDouble("maxBefore", 0.0))
            .put("meanAfter", report.optDouble("meanAfter", 0.0))
            .put("maxAfter", report.optDouble("maxAfter", 0.0))
            .put("styles", stylesJson)
        manifestFile(context).writeText(manifest.toString(2))
        return manifest
    }

    /** Load the standalone correction LUT data for the verification flow. */
    fun loadCorrectionData(context: Context): FloatArray? {
        val file = File(rootDir(context), CORRECTION_FILE)
        if (!file.exists()) return null
        return PlutIO.readPlut(file.readBytes())?.data
    }

    /**
     * Build the shareable ZIP in cache/shared (covered by the app's
     * FileProvider) with the exact assets layout: a "devices" folder holding
     * index.json and the device-key folder with its .plut files, config.json
     * and report.json.
     */
    fun buildExportZip(context: Context): File? {
        val pack = packDir(context)
        if (!pack.exists()) return null
        val key = deviceKey()
        val index = JSONObject().put(
            "packs",
            JSONArray().put(
                JSONObject()
                    .put("key", key)
                    .put("manufacturer", Build.MANUFACTURER ?: "")
                    .put("models", JSONArray().put(Build.MODEL ?: ""))
                    .put("folder", key)
            )
        )
        val sharedDir = File(context.cacheDir, "shared").apply { mkdirs() }
        val zipFile = File(sharedDir, "leicafujicam-calibration-$key.zip")
        return try {
            ZipOutputStream(FileOutputStream(zipFile)).use { zip ->
                zip.putNextEntry(ZipEntry("devices/index.json"))
                zip.write(index.toString(2).toByteArray())
                zip.closeEntry()
                pack.listFiles()?.sortedBy { it.name }?.forEach { file ->
                    if (file.isFile) {
                        zip.putNextEntry(ZipEntry("devices/$key/${file.name}"))
                        zip.write(file.readBytes())
                        zip.closeEntry()
                    }
                }
            }
            zipFile
        } catch (e: Exception) {
            PLog.e(TAG, "Failed to build calibration export zip", e)
            null
        }
    }

    fun clear(context: Context) {
        rootDir(context).deleteRecursively()
    }
}
