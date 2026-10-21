package vn.hoang.leicafujicam.raw.fotufilm

import android.opengl.GLES30

/**
 * Whole-frame quantities a tile of the frame cannot measure on its own, built by
 * [FotufilmRenderer.buildFrameFields] — upstream's FIELDS_IN road (`FotufilmHalideMetal.cpp`
 * `fotufilm_halide_metal_halation_fields`): the halation pyramid's three blurred grids of the
 * whole frame's post-MTF light, in frame cells, plus the veiling-glare frame mean when the frame
 * measures it on device (FOTUFILM_FRAME_FLARE_MEASURE).
 *
 * The textures belong to the EGL context that built them; [release] must run on that context.
 */
class FotufilmFrameFields internal constructor(
    val frameWidth: Int,
    val frameHeight: Int,
    /** 1×1 RGBA32F frame mean of the widened lens light (alpha: record neutral); 0 when not measured. */
    internal val flareMeanTexture: Int,
    /** Three RGBA32F blurred halation grids, or zeros when the frame has no halation. */
    internal val halationGrids: IntArray,
    internal val gridWidths: IntArray,
    internal val gridHeights: IntArray,
    internal val strides: IntArray,
) {
    var released: Boolean = false
        private set

    val hasFlareMean: Boolean get() = flareMeanTexture != 0
    val hasHalation: Boolean get() = halationGrids.all { it != 0 }

    fun release() {
        if (released) return
        val textures = (halationGrids.toList() + flareMeanTexture).filter { it != 0 }.toIntArray()
        if (textures.isNotEmpty()) GLES30.glDeleteTextures(textures.size, textures, 0)
        forget()
    }

    /** Drops the handles without deleting them, after the owning EGL context was lost. */
    fun forget() {
        released = true
    }
}
