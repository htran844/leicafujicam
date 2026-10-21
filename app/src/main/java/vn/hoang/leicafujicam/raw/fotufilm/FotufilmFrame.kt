package vn.hoang.leicafujicam.raw.fotufilm

import androidx.annotation.Keep

/**
 * One frame's engine input, produced by the native core (upstream `FilmEngineInvocation`).
 *
 * The three spectral tables are 33³ RGBA-interleaved cubes laid out as upstream `SpectralLUT`:
 * value `((z * 33 + y) * 33 + x) * 4 + channel`. `seed` carries the uint32 bit pattern.
 * `frameWidth`/`frameHeight` describe the virtual whole frame every absolute coordinate refers
 * to; the supports are the upstream reaches in pixels (`spatialSupport` for a tile developed
 * without whole-frame fields, `spatialSupportSansHalation` for one developed against
 * [FotufilmFrameFields], `lightSupport` for the light chain the fields are built from).
 *
 * `filmTiles` are the film grain model's running-sum tiles (grain mode 1): `(257², 17, 3)`
 * floats, entry fastest, as upstream `fotufilm_halide_set_film_tiles`. Null when the frame does
 * not lay film grain; `filmTilesKey` identifies them for the texture cache.
 */
@Keep
data class FotufilmFrame(
    val configuration: FloatArray,
    val lutDimension: Int = LUT_DIMENSION,
    val exposureLut: FloatArray,
    val filmOutputLut: FloatArray,
    val paperOutputLut: FloatArray?,
    val featureMask: Int,
    val seed: Int,
    val frameWidth: Int,
    val frameHeight: Int,
    val spatialSupport: Int,
    val spatialSupportSansHalation: Int,
    val lightSupport: Int,
    val halationSupport: Int,
    val halationPixelRadii: IntArray,
    val sourceKey: String,
    val filmTiles: FloatArray? = null,
    val filmTilesKey: String? = null,
) {
    init {
        require(configuration.size >= FotufilmConfigLayout.FRAME_CONFIGURATION_COUNT) {
            "Fotufilm configuration holds ${configuration.size} floats, " +
                "expected ${FotufilmConfigLayout.FRAME_CONFIGURATION_COUNT}"
        }
        require(lutDimension == LUT_DIMENSION) { "Fotufilm spectral LUT dimension must be 33" }
        require(exposureLut.size == LUT_VALUE_COUNT) { "Invalid Fotufilm exposure LUT size" }
        require(filmOutputLut.size == LUT_VALUE_COUNT) { "Invalid Fotufilm film output LUT size" }
        require(paperOutputLut == null || paperOutputLut.size == LUT_VALUE_COUNT) {
            "Invalid Fotufilm paper output LUT size"
        }
        require(frameWidth > 0 && frameHeight > 0) { "Invalid Fotufilm frame size" }
        require(halationPixelRadii.size == 3) { "Fotufilm halation radii must hold three scales" }
        require(filmTiles == null || filmTiles.size.toLong() == FILM_TILE_FLOATS) {
            "Fotufilm film tiles hold ${filmTiles?.size} floats, expected $FILM_TILE_FLOATS"
        }
    }

    /** The configuration's grain mode names the film grain model and its tiles are present. */
    val filmGrainOn: Boolean
        get() = filmTiles != null &&
            configuration[FotufilmConfigLayout.CONFIG_GRAIN_MODE].toInt() == 1

    companion object {
        const val LUT_DIMENSION = 33
        const val LUT_VALUE_COUNT = LUT_DIMENSION * LUT_DIMENSION * LUT_DIMENSION * 4
        const val FILM_TILE_ENTRIES_SIDE = FotufilmConfigLayout.FILM_TILE_SIDE + 1
        const val FILM_TILE_FLOATS: Long = FILM_TILE_ENTRIES_SIDE.toLong() * FILM_TILE_ENTRIES_SIDE *
            FotufilmConfigLayout.FILM_TILE_LEVELS * 3
    }
}
