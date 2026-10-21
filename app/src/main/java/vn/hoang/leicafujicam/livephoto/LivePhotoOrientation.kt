package vn.hoang.leicafujicam.livephoto

internal fun resolveLivePhotoRotationDegrees(
    deviceRotationDegrees: Int,
    calibrationOffsetDegrees: Int,
): Int {
    return Math.floorMod(deviceRotationDegrees + calibrationOffsetDegrees, 360)
}
