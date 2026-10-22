package vn.hoang.leicafujicam.pose

import android.content.Context
import androidx.compose.ui.geometry.Offset
import org.json.JSONObject
import vn.hoang.leicafujicam.utils.PLog
import java.util.Locale

/**
 * Bundled pose-guide catalog (assets/poses/poses.json).
 *
 * Data model and starter content adapted from the PoseGuide open dataset
 * (MIT, Copyright (c) 2026 MergeOS / ThanhTrucSolutions — full license text in
 * assets/poses/POSEGUIDE-LICENSE.txt): each pose is a normalized 13-joint
 * skeleton with tags, model tips and camera cues; scenes rank poses per
 * shooting environment.
 */
data class PoseEntry(
    val id: String,
    val nameMap: Map<String, String>,
    val gender: String = "unisex",
    val standing: Boolean,
    val tags: List<String>,
    val tipsMap: Map<String, List<String>>,
    val cameraCuesMap: Map<String, List<String>>,
    val joints: Map<String, Offset>,
    /** Example photo asset path relative to assets/poses/, if bundled. */
    val photo: String? = null,
    val sourcePlatform: String? = null,
    val sourceCreator: String? = null,
    val sourceUrl: String? = null
) {
    fun getName(locale: Locale = Locale.getDefault()): String =
        nameMap[locale.language] ?: nameMap["en"] ?: id

    fun getTips(locale: Locale = Locale.getDefault()): List<String> =
        tipsMap[locale.language] ?: tipsMap["en"] ?: emptyList()

    fun getCameraCues(locale: Locale = Locale.getDefault()): List<String> =
        cameraCuesMap[locale.language] ?: cameraCuesMap["en"] ?: emptyList()
}

data class PoseScene(
    val id: String,
    val nameMap: Map<String, String>,
    val tags: List<String>,
    val mood: List<String>,
    val expectedPoses: List<String>,
    val composition: Map<String, String>
) {
    fun getName(locale: Locale = Locale.getDefault()): String =
        nameMap[locale.language] ?: nameMap["en"] ?: id
}

data class PoseCatalogData(
    val poses: List<PoseEntry>,
    val scenes: List<PoseScene>
) {
    val posesById: Map<String, PoseEntry> = poses.associateBy { it.id }
}

object PoseCatalog {
    private const val TAG = "PoseCatalog"
    private const val ASSET_PATH = "poses/poses.json"

    /** Bone segments of the 13-joint skeleton, drawn in order. */
    val BONES: List<Pair<String, String>> = listOf(
        "l_shoulder" to "r_shoulder",
        "l_shoulder" to "l_elbow",
        "l_elbow" to "l_wrist",
        "r_shoulder" to "r_elbow",
        "r_elbow" to "r_wrist",
        "l_shoulder" to "l_hip",
        "r_shoulder" to "r_hip",
        "l_hip" to "r_hip",
        "l_hip" to "l_knee",
        "l_knee" to "l_ankle",
        "r_hip" to "r_knee",
        "r_knee" to "r_ankle"
    )

    fun load(context: Context): PoseCatalogData {
        return try {
            val text = context.assets.open(ASSET_PATH).bufferedReader().readText()
            val root = JSONObject(text)
            val poses = mutableListOf<PoseEntry>()
            val posesArray = root.getJSONArray("poses")
            for (i in 0 until posesArray.length()) {
                val obj = posesArray.getJSONObject(i)
                val jointsObj = obj.getJSONObject("joints")
                val joints = mutableMapOf<String, Offset>()
                jointsObj.keys().forEach { key ->
                    val arr = jointsObj.getJSONArray(key)
                    joints[key] = Offset(
                        arr.getDouble(0).toFloat(),
                        arr.getDouble(1).toFloat()
                    )
                }
                val sourceObj = obj.optJSONObject("source")
                poses += PoseEntry(
                    id = obj.getString("id"),
                    nameMap = readNameMap(obj.getJSONObject("name")),
                    gender = obj.optString("gender", "unisex"),
                    standing = obj.optBoolean("standing", true),
                    tags = readStringList(obj.optJSONArray("tags")),
                    tipsMap = readTextMap(obj.optJSONObject("tips")),
                    cameraCuesMap = readTextMap(obj.optJSONObject("cameraCues")),
                    joints = joints,
                    photo = obj.optString("photo").ifEmpty { null },
                    sourcePlatform = sourceObj?.optString("platform")?.ifEmpty { null },
                    sourceCreator = sourceObj?.optString("creator")?.ifEmpty { null },
                    sourceUrl = sourceObj?.optString("url")?.ifEmpty { null }
                )
            }
            val scenes = mutableListOf<PoseScene>()
            val scenesArray = root.getJSONArray("scenes")
            for (i in 0 until scenesArray.length()) {
                val obj = scenesArray.getJSONObject(i)
                val compositionObj = obj.optJSONObject("composition")
                val composition = mutableMapOf<String, String>()
                compositionObj?.keys()?.forEach { key ->
                    composition[key] = compositionObj.optString(key)
                }
                scenes += PoseScene(
                    id = obj.getString("id"),
                    nameMap = readNameMap(obj.getJSONObject("name")),
                    tags = readStringList(obj.optJSONArray("tags")),
                    mood = readStringList(obj.optJSONArray("mood")),
                    expectedPoses = readStringList(obj.optJSONArray("expectedPoses")),
                    composition = composition
                )
            }
            PoseCatalogData(poses, scenes)
        } catch (e: Exception) {
            PLog.e(TAG, "Failed to load pose catalog", e)
            PoseCatalogData(emptyList(), emptyList())
        }
    }

    private fun readNameMap(obj: JSONObject): Map<String, String> {
        val map = mutableMapOf<String, String>()
        obj.keys().forEach { key -> map[key] = obj.getString(key) }
        return map
    }

    private fun readTextMap(obj: JSONObject?): Map<String, List<String>> {
        if (obj == null) return emptyMap()
        val map = mutableMapOf<String, List<String>>()
        obj.keys().forEach { key -> map[key] = readStringList(obj.optJSONArray(key)) }
        return map
    }

    private fun readStringList(array: org.json.JSONArray?): List<String> {
        if (array == null) return emptyList()
        return (0 until array.length()).map { array.optString(it) }
    }
}
