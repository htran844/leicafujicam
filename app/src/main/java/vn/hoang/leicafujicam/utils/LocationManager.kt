package vn.hoang.leicafujicam.utils

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import androidx.core.location.LocationListenerCompat
import android.location.LocationManager
import android.os.CancellationSignal
import android.os.SystemClock
import androidx.core.location.LocationManagerCompat
import androidx.core.location.LocationRequestCompat

/**
 * 拍摄位置来源。
 *
 * 输出始终是系统定位给出的 WGS-84 坐标（EXIF GPS 标准坐标系），不做任何坐标系转换。
 *
 * 功耗分级：
 * - 被动定位：复用系统/其他应用产生的定位结果，无额外功耗
 * - 低功耗持续定位：fused（平衡功耗精度）或 network 定位源，长间隔 + 最小位移，不开启 GNSS
 * - 高精度单次定位：仅在开始定位时和拍摄时当前定位过旧/过粗才请求一次 GNSS 定位，得到结果即释放，
 *   并限制请求频率，避免 GNSS 长时间工作
 */
class LocationManager(private val context: Context) {
    private val locationManager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager

    private var currentLocation: Location? = null
    private var listening = false
    private var preciseRequestSignal: CancellationSignal? = null
    private var lastPreciseRequestNanos = 0L

    private val listener = LocationListenerCompat { location -> updateCurrentLocation(location) }

    @SuppressLint("MissingPermission")
    @Synchronized
    fun startUpdates() {
        if (listening || !hasLocationPermission()) {
            return
        }
        listening = true

        for (provider in enabledProviders()) {
            try {
                locationManager.getLastKnownLocation(provider)?.let { updateCurrentLocationLocked(it) }
            } catch (e: Exception) {
                PLog.e(TAG, "Failed to get last known location from $provider", e)
            }
        }

        requestUpdates(LocationManager.PASSIVE_PROVIDER, passiveRequest)
        lowPowerProvider()?.let { requestUpdates(it, lowPowerRequest) }
        requestPreciseLocationLocked()
    }

    @Synchronized
    fun stopUpdates() {
        if (!listening) {
            return
        }
        listening = false
        preciseRequestSignal?.cancel()
        preciseRequestSignal = null
        try {
            LocationManagerCompat.removeUpdates(locationManager, listener)
        } catch (e: Exception) {
            PLog.e(TAG, "Failed to remove location updates", e)
        }
        PLog.d(TAG, "Location updates stopped")
    }

    /**
     * 返回当前最优定位（WGS-84）。超过 [MAX_LOCATION_AGE_NANOS] 的定位视为失效，返回 null，
     * 避免把过期位置写入照片。当前定位不够新或不够精确时，触发一次高精度单次定位供后续拍摄使用。
     */
    @Synchronized
    fun getCurrentLocation(): Location? {
        val location = currentLocation
        if (location == null || !isPrecise(location)) {
            requestPreciseLocationLocked()
        }
        location ?: return null
        val age = ageNanos(location)
        if (age > MAX_LOCATION_AGE_NANOS) {
            PLog.d(TAG, "Discard stale location: provider=${location.provider}, age=${age / 1_000_000}ms")
            return null
        }
        return Location(location)
    }

    @SuppressLint("MissingPermission")
    private fun requestUpdates(provider: String, request: LocationRequestCompat) {
        try {
            LocationManagerCompat.requestLocationUpdates(
                locationManager,
                provider,
                request,
                context.mainExecutor,
                listener
            )
            PLog.d(TAG, "Location updates requested: $provider")
        } catch (e: Exception) {
            PLog.e(TAG, "Failed to request location updates from $provider", e)
        }
    }

    @SuppressLint("MissingPermission")
    private fun requestPreciseLocationLocked() {
        if (!listening || preciseRequestSignal != null) {
            return
        }
        val now = SystemClock.elapsedRealtimeNanos()
        if (lastPreciseRequestNanos != 0L && now - lastPreciseRequestNanos < PRECISE_REQUEST_MIN_INTERVAL_NANOS) {
            return
        }
        val provider = preciseProvider() ?: return
        val signal = CancellationSignal()
        preciseRequestSignal = signal
        lastPreciseRequestNanos = now
        try {
            LocationManagerCompat.getCurrentLocation(
                locationManager,
                provider,
                signal,
                context.mainExecutor
            ) { location ->
                synchronized(this) {
                    if (preciseRequestSignal === signal) {
                        preciseRequestSignal = null
                    }
                    location?.let { updateCurrentLocationLocked(it) }
                }
                PLog.d(TAG, "Precise location result: provider=$provider, accuracy=${location?.accuracy}")
            }
        } catch (e: Exception) {
            preciseRequestSignal = null
            PLog.e(TAG, "Failed to request precise location from $provider", e)
        }
    }

    private fun enabledProviders(): List<String> = try {
        locationManager.getProviders(true)
    } catch (e: Exception) {
        PLog.e(TAG, "Failed to query location providers", e)
        emptyList()
    }

    private fun lowPowerProvider(): String? {
        val providers = enabledProviders()
        return LOW_POWER_PROVIDERS.firstOrNull { it in providers }
    }

    private fun preciseProvider(): String? {
        val providers = enabledProviders()
        return PRECISE_PROVIDERS.firstOrNull { it in providers }
    }

    @Synchronized
    private fun updateCurrentLocation(location: Location) {
        updateCurrentLocationLocked(location)
    }

    private fun updateCurrentLocationLocked(location: Location) {
        val previous = currentLocation
        if (previous == null || isBetterLocation(location, previous)) {
            currentLocation = Location(location)
        }
    }

    private fun isPrecise(location: Location): Boolean {
        return ageNanos(location) <= PRECISE_LOCATION_MAX_AGE_NANOS &&
            location.hasAccuracy() &&
            location.accuracy <= PRECISE_LOCATION_MAX_ACCURACY_METERS
    }

    private fun ageNanos(location: Location): Long {
        return SystemClock.elapsedRealtimeNanos() - location.elapsedRealtimeNanos
    }

    /**
     * 综合时间与精度判断新定位是否优于当前定位：
     * - 明显更新（超过有效期）的定位总是替换旧定位；明显更旧的定位总是丢弃
     * - 有效期内优先精度：更精确的替换；更新且精度不变差的替换；
     *   同一定位源更新且精度未显著变差的替换
     */
    private fun isBetterLocation(location: Location, currentBest: Location): Boolean {
        val timeDelta = location.elapsedRealtimeNanos - currentBest.elapsedRealtimeNanos
        if (timeDelta > MAX_LOCATION_AGE_NANOS) return true
        if (timeDelta < -MAX_LOCATION_AGE_NANOS) return false
        val isNewer = timeDelta > 0

        val newAccuracy = if (location.hasAccuracy()) location.accuracy else Float.MAX_VALUE
        val bestAccuracy = if (currentBest.hasAccuracy()) currentBest.accuracy else Float.MAX_VALUE
        val accuracyDelta = newAccuracy - bestAccuracy
        val isMoreAccurate = accuracyDelta < 0
        val isLessAccurate = accuracyDelta > 0
        val isSignificantlyLessAccurate = accuracyDelta > SIGNIFICANT_ACCURACY_DELTA_METERS

        return when {
            isMoreAccurate -> true
            isNewer && !isLessAccurate -> true
            isNewer && !isSignificantlyLessAccurate && location.provider == currentBest.provider -> true
            else -> false
        }
    }

    private fun hasLocationPermission(): Boolean {
        val fineLocation = androidx.core.content.ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.ACCESS_FINE_LOCATION
        ) == android.content.pm.PackageManager.PERMISSION_GRANTED
        val coarseLocation = androidx.core.content.ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.ACCESS_COARSE_LOCATION
        ) == android.content.pm.PackageManager.PERMISSION_GRANTED
        return fineLocation || coarseLocation
    }

    companion object {
        private const val TAG = "LocationManager"
        private const val FUSED_PROVIDER = "fused"

        /** 低功耗持续定位源优先级：fused（平衡功耗精度下不开 GNSS）> network */
        private val LOW_POWER_PROVIDERS = listOf(FUSED_PROVIDER, LocationManager.NETWORK_PROVIDER)

        /** 高精度单次定位源优先级 */
        private val PRECISE_PROVIDERS = listOf(
            LocationManager.GPS_PROVIDER,
            FUSED_PROVIDER,
            LocationManager.NETWORK_PROVIDER
        )

        private const val SECOND_NANOS = 1_000_000_000L
        private const val MAX_LOCATION_AGE_NANOS = 2L * 60 * SECOND_NANOS
        private const val PRECISE_LOCATION_MAX_AGE_NANOS = 30L * SECOND_NANOS
        private const val PRECISE_LOCATION_MAX_ACCURACY_METERS = 50f
        private const val PRECISE_REQUEST_MIN_INTERVAL_NANOS = 30L * SECOND_NANOS
        private const val SIGNIFICANT_ACCURACY_DELTA_METERS = 200f

        private val passiveRequest = LocationRequestCompat.Builder(LocationRequestCompat.PASSIVE_INTERVAL)
            .setQuality(LocationRequestCompat.QUALITY_LOW_POWER)
            .setMinUpdateIntervalMillis(0)
            .build()

        private val lowPowerRequest = LocationRequestCompat.Builder(30_000L)
            .setQuality(LocationRequestCompat.QUALITY_BALANCED_POWER_ACCURACY)
            .setMinUpdateIntervalMillis(10_000L)
            .setMinUpdateDistanceMeters(10f)
            .build()
    }
}
