package vn.hoang.leicafujicam.raw.fotufilm

import androidx.annotation.Keep

/**
 * A whole-frame measurement in upstream `Options.scene*` form: the brightest content
 * ([highlightStops]), the median, darker tenth and brighter twentieth ([toneStops]) and the
 * per-record medians ([channelMedians]). A photograph metered once hands these to its later
 * frames (previews, re-renders) instead of metering again.
 */
@Keep
class FotufilmMetering(
    val highlightStops: Float,
    val toneStops: FloatArray,
    val channelMedians: FloatArray,
) {
    init {
        require(toneStops.size == 3 && channelMedians.size == 3) { "Fotufilm metering holds three-channel readings" }
    }

    /** The 7-float form [FotufilmNativeBridge.buildFrame] takes. */
    fun toArray(): FloatArray = floatArrayOf(highlightStops) + toneStops + channelMedians

    override fun equals(other: Any?): Boolean =
        other is FotufilmMetering && highlightStops == other.highlightStops &&
            toneStops.contentEquals(other.toneStops) && channelMedians.contentEquals(other.channelMedians)

    override fun hashCode(): Int =
        (highlightStops.hashCode() * 31 + toneStops.contentHashCode()) * 31 + channelMedians.contentHashCode()

    companion object {
        fun fromArray(values: FloatArray): FotufilmMetering {
            require(values.size == 7) { "Fotufilm metering holds 7 floats" }
            return FotufilmMetering(values[0], values.copyOfRange(1, 4), values.copyOfRange(4, 7))
        }
    }
}

/**
 * One native frame build: the renderer's [frame], the readings it metered (null when it read
 * none or was handed them), and whether the frame asked for a scene measurement or a veiling-glare
 * mean, so a caller knows whether a metering buffer mattered.
 */
@Keep
class FotufilmBuildResult(
    val frame: FotufilmFrame,
    val metering: FotufilmMetering?,
    val wantsSceneMetering: Boolean,
    val wantsFlareMean: Boolean,
)
