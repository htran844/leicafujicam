package vn.hoang.leicafujicam.hdr

interface GainmapProducer {
    suspend fun build(
        source: GainmapSourceSet,
        strength: Float = HdrGainmapStrength.DEFAULT
    ): GainmapResult?
}
