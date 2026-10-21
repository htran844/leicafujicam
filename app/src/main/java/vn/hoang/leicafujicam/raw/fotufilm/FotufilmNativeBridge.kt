package vn.hoang.leicafujicam.raw.fotufilm

import java.nio.ByteBuffer

/**
 * JNI entry points of the native Fotufilm core (`cpp/fotufilm/fotufilm_jni.cpp`). Every call
 * throws [IllegalStateException] on failure. Use [FotufilmRuntime] rather than calling these
 * directly: it installs the resources once and caches frames.
 */
internal object FotufilmNativeBridge {
    init {
        System.loadLibrary("my-native-lib")
    }

    /**
     * Registers every stock pack, installs the measured reflectance prior and sets the film grain
     * bank's disk provider under [grainCacheDir]. Replaces an earlier install.
     */
    external fun install(stockJsons: Array<String>, reflectancePrior: ByteArray, grainCacheDir: String)

    /** The films to offer, as a JSON array (see [FotufilmStockInfo.parseList]). */
    external fun stocks(): String

    /** The Android control catalogue for [stockId] under [documentJson] (see [FotufilmCatalogue.parse]). */
    external fun catalogue(stockId: String, documentJson: String): String

    /**
     * Builds one frame's engine input.
     *
     * @param meteringBuffer direct buffer of linear Rec.2020 RGBA floats (native order) holding
     *   the whole frame or a downsampled copy with the same aspect; read only when the frame
     *   meters the scene or lays veiling glare.
     * @param measureFlareOnDevice leave the veiling-glare mean to the renderer when no buffer is
     *   metered.
     * @param sceneMetering readings of an earlier metered frame of the same photograph, as
     *   [FotufilmMetering.toArray] (7 floats), or null to meter.
     * @param cachedFilmTilesKey key of [cachedFilmTiles]; when it names this frame's bank the
     *   array is reused instead of copied.
     */
    external fun buildFrame(
        stockId: String,
        documentJson: String,
        width: Int,
        height: Int,
        frameIndex: Long,
        meteringBuffer: ByteBuffer?,
        meteringWidth: Int,
        meteringHeight: Int,
        measureFlareOnDevice: Boolean,
        sceneMetering: FloatArray?,
        cachedFilmTilesKey: String?,
        cachedFilmTiles: FloatArray?,
    ): FotufilmBuildResult
}
