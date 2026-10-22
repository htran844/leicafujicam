package vn.hoang.leicafujicam.lut

import android.content.Context
import android.os.Build
import org.json.JSONObject
import vn.hoang.leicafujicam.utils.PLog

/**
 * Offline per-device LUT packs bundled in assets/luts/devices/.
 *
 * index.json lists packs with their manufacturer + Build.MODEL codes. When the
 * current device matches a pack, that pack's LUTs replace the generic built-in
 * LUTs with the same id (device-calibrated versions win), following the same
 * override pattern the app already uses for custom LUTs. Devices without a
 * pack simply use the generic set.
 */
object DeviceLutPack {
    private const val TAG = "DeviceLutPack"
    private const val INDEX_PATH = "luts/devices/index.json"

    fun loadForCurrentDevice(context: Context): List<LutInfo> {
        return try {
            val indexText = context.assets.open(INDEX_PATH).use { it.bufferedReader().readText() }
            val packs = JSONObject(indexText).optJSONArray("packs") ?: return emptyList()
            val manufacturer = Build.MANUFACTURER?.trim().orEmpty()
            val model = Build.MODEL?.trim().orEmpty()
            if (model.isEmpty()) return emptyList()
            for (i in 0 until packs.length()) {
                val pack = packs.optJSONObject(i) ?: continue
                val packManufacturer = pack.optString("manufacturer")
                val models = pack.optJSONArray("models") ?: continue
                val modelMatches = (0 until models.length()).any { idx ->
                    models.optString(idx).equals(model, ignoreCase = true)
                }
                if (packManufacturer.equals(manufacturer, ignoreCase = true) && modelMatches) {
                    val folder = pack.optString("folder")
                    if (folder.isBlank()) continue
                    val luts = LutParser.listAvailableLuts(context, "luts/devices/$folder")
                    PLog.i(
                        TAG,
                        "Device LUT pack applied: $folder (${luts.size} LUTs) for $manufacturer $model"
                    )
                    return luts
                }
            }
            emptyList()
        } catch (e: Exception) {
            PLog.w(TAG, "No device LUT pack loaded", e)
            emptyList()
        }
    }
}
