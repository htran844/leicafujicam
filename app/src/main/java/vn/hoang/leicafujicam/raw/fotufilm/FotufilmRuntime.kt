package vn.hoang.leicafujicam.raw.fotufilm

import android.content.Context
import vn.hoang.leicafujicam.utils.PLog
import java.io.File
import java.nio.ByteBuffer

/**
 * The process-wide Fotufilm core: installs the stock packs, the reflectance prior and the film
 * grain bank cache once, then answers stock listings, control catalogues and frame builds.
 * Calls are synchronous and may take seconds (spectral tables, a film grain bank's first
 * preparation); call them off the main thread.
 */
object FotufilmRuntime {
    private const val TAG = "FotufilmRuntime"
    private const val ASSET_ROOT = "fotufilm"
    private const val STOCK_ASSETS = "$ASSET_ROOT/stocks"
    private const val REFLECTANCE_PRIOR_ASSET = "$ASSET_ROOT/rec2020-reflectance-prior.coeff"
    private const val GRAIN_CACHE_DIR = "fotufilm-grain-v1"
    private const val FRAME_CACHE_SIZE = 4

    private val installLock = Any()
    @Volatile
    private var installed = false
    @Volatile
    private var stockList: List<FotufilmStockInfo>? = null

    private val cacheLock = Any()
    private val frameCache = object : LinkedHashMap<FrameKey, FotufilmBuildResult>(FRAME_CACHE_SIZE, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<FrameKey, FotufilmBuildResult>?): Boolean =
            size > FRAME_CACHE_SIZE
    }
    private var lastFilmTilesKey: String? = null
    private var lastFilmTiles: FloatArray? = null

    private data class FrameKey(
        val documentJson: String,
        val width: Int,
        val height: Int,
        val frameIndex: Long,
        val sourceIdentity: String?,
        val metering: FotufilmMetering?,
        val measureFlareOnDevice: Boolean,
    )

    /** Installs the resources on first use; later calls return at once. */
    fun ensureInstalled(context: Context) {
        if (installed) return
        synchronized(installLock) {
            if (installed) return
            val started = System.nanoTime()
            val assets = context.applicationContext.assets
            val stockFiles = assets.list(STOCK_ASSETS).orEmpty().filter { it.endsWith(".json") }.sorted()
            check(stockFiles.isNotEmpty()) { "No Fotufilm stocks under assets/$STOCK_ASSETS" }
            val stockJsons = stockFiles.map { name ->
                assets.open("$STOCK_ASSETS/$name").use { it.readBytes().toString(Charsets.UTF_8) }
            }.toTypedArray()
            val prior = assets.open(REFLECTANCE_PRIOR_ASSET).use { it.readBytes() }
            val grainDir = File(context.applicationContext.cacheDir, GRAIN_CACHE_DIR)
            if (!grainDir.isDirectory && !grainDir.mkdirs()) {
                throw IllegalStateException("Unable to create ${grainDir.absolutePath}")
            }
            FotufilmNativeBridge.install(stockJsons, prior, grainDir.absolutePath)
            installed = true
            PLog.i(
                TAG,
                "Installed ${stockJsons.size} stocks in ${(System.nanoTime() - started) / 1_000_000} ms, " +
                    "grain cache ${grainDir.absolutePath}",
            )
        }
    }

    /** The films to offer, in display order. */
    fun stocks(context: Context): List<FotufilmStockInfo> {
        stockList?.let { return it }
        ensureInstalled(context)
        return FotufilmStockInfo.parseList(FotufilmNativeBridge.stocks()).also { stockList = it }
    }

    /** The Android controls for [document]'s film, their current values and availability. */
    fun catalogue(context: Context, document: FotufilmDocument): FotufilmCatalogue {
        ensureInstalled(context)
        return FotufilmCatalogue.parse(FotufilmNativeBridge.catalogue(document.stockId, document.toJson()))
    }

    /**
     * Builds the renderer's input for [document] on a [width]×[height] frame.
     *
     * A frame that meters the scene (Auto Levels, Lab Scan levels, active regional tone) or lays
     * veiling glare reads [meteringBuffer] (direct, linear Rec.2020 RGBA floats of the whole frame
     * or a same-aspect downsample). Hand the returned [FotufilmBuildResult.metering] back as
     * [metering] for later frames of the same photograph to develop on the same levels; without a
     * buffer the glare mean is left to the renderer when [measureFlareOnDevice] is set.
     *
     * Results are cached by document, size, frame index and metering. A build that reads a buffer
     * is cached only under a [sourceIdentity] naming the buffer's content.
     */
    fun buildFrame(
        context: Context,
        document: FotufilmDocument,
        width: Int,
        height: Int,
        frameIndex: Long = 0,
        meteringBuffer: ByteBuffer? = null,
        meteringWidth: Int = 0,
        meteringHeight: Int = 0,
        measureFlareOnDevice: Boolean = true,
        metering: FotufilmMetering? = null,
        sourceIdentity: String? = null,
    ): FotufilmBuildResult {
        require(meteringBuffer == null || meteringBuffer.isDirect) { "Fotufilm metering buffer must be direct" }
        ensureInstalled(context)
        val documentJson = document.toJson()
        val cacheable = meteringBuffer == null || sourceIdentity != null
        val key = FrameKey(
            documentJson, width, height, frameIndex,
            sourceIdentity.takeIf { meteringBuffer != null }, metering, measureFlareOnDevice,
        )
        val tilesKey: String?
        val tiles: FloatArray?
        synchronized(cacheLock) {
            if (cacheable) frameCache[key]?.let { return it }
            tilesKey = lastFilmTilesKey
            tiles = lastFilmTiles
        }
        val started = System.nanoTime()
        val result = FotufilmNativeBridge.buildFrame(
            document.stockId, documentJson, width, height, frameIndex,
            meteringBuffer, meteringWidth, meteringHeight, measureFlareOnDevice,
            metering?.toArray(), tilesKey, tiles,
        )
        PLog.d(
            TAG,
            "Built ${document.stockId} ${width}x$height in ${(System.nanoTime() - started) / 1_000_000} ms " +
                "metered=${result.metering != null} filmTiles=${result.frame.filmTilesKey}",
        )
        synchronized(cacheLock) {
            result.frame.filmTiles?.let {
                lastFilmTilesKey = result.frame.filmTilesKey
                lastFilmTiles = it
            }
            if (cacheable) frameCache[key] = result
        }
        return result
    }

    /** Drops cached frames and the retained film grain bank (the disk cache stays). */
    fun clearCache() {
        synchronized(cacheLock) {
            frameCache.clear()
            lastFilmTilesKey = null
            lastFilmTiles = null
        }
    }
}
