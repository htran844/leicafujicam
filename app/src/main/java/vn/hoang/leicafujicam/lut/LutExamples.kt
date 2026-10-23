package vn.hoang.leicafujicam.lut

import android.content.Context
import vn.hoang.leicafujicam.utils.PLog

/**
 * Example photos bundled per LUT under `assets/luts/examples/<lutId>/`.
 *
 * Convention: every bundled LUT ships at least one example photo so the LUT
 * selector can show real reference colors. Hand-picked originals are used
 * where a LUT is modeled on a specific photographer's set; the rest are
 * generated offline by `tools/make_lut_examples.py` (the LUT applied to one
 * shared reference photo). Custom/imported LUTs simply have no folder and
 * therefore no examples.
 */
object LutExamples {

    private const val TAG = "LutExamples"
    private const val ROOT = "luts/examples"

    /** Sorted asset paths of the example photos for [lutId]; empty when none. Blocking IO. */
    fun examplePaths(context: Context, lutId: String): List<String> {
        if (lutId.isBlank() || lutId == "none") return emptyList()
        return try {
            val dir = "$ROOT/$lutId"
            context.assets.list(dir)
                ?.filter { it.endsWith(".jpg", ignoreCase = true) || it.endsWith(".png", ignoreCase = true) }
                ?.sorted()
                ?.map { "$dir/$it" }
                ?: emptyList()
        } catch (e: Exception) {
            PLog.w(TAG, "No examples for $lutId", e)
            emptyList()
        }
    }
}
