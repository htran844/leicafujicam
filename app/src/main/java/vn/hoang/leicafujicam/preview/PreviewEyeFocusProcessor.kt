package vn.hoang.leicafujicam.preview

import android.content.Context
import android.graphics.Bitmap
import android.os.SystemClock
import vn.hoang.leicafujicam.ml.BlazeFaceDetector
import vn.hoang.leicafujicam.utils.PLog
import java.io.Closeable
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.sqrt

class PreviewEyeFocusProcessor(context: Context) : Closeable {

    data class EyeTarget(
        val x: Float,
        val y: Float,
        val confidence: Float,
    )

    private data class FaceCandidate(
        val faceCenterX: Float,
        val faceCenterY: Float,
        val boxWidth: Float,
        val boxHeight: Float,
        val area: Float,
        val confidence: Float,
        val leftEyeX: Float,
        val leftEyeY: Float,
        val rightEyeX: Float,
        val rightEyeY: Float,
    )

    companion object {
        private const val TAG = "PreviewEyeFocus"
        private const val FACE_MASK_WIDTH = 64
        private const val FACE_MASK_HEIGHT = 64
        private const val FACE_MASK_CORE_RADIUS = 0.58f
        private const val MIN_DETECTION_CONFIDENCE = 0.65f
        private const val MIN_TARGET_CONFIDENCE = 0.70f
        private const val REQUIRED_STABLE_RESULTS = 3
        private const val TARGET_CONFIRMATION_DURATION_NS = 200_000_000L
        private const val TARGET_LOST_MIN_RESULTS = 2
        private const val TARGET_LOST_DURATION_NS = 200_000_000L
        private const val MIN_FACE_WIDTH_FRACTION = 0.05f
        private const val MIN_FACE_HEIGHT_FRACTION = 0.05f
        private const val MIN_FACE_ASPECT_RATIO = 0.50f
        private const val MAX_FACE_ASPECT_RATIO = 1.80f
        private const val MIN_INTER_EYE_FACE_WIDTH_RATIO = 0.15f
        private const val MAX_INTER_EYE_FACE_WIDTH_RATIO = 0.75f
        private const val MAX_EYE_VERTICAL_FACE_HEIGHT_RATIO = 0.35f
        private const val MIN_EYE_MIDPOINT_X_RATIO = 0.10f
        private const val MAX_EYE_MIDPOINT_X_RATIO = 0.90f
        private const val MIN_EYE_MIDPOINT_Y_RATIO = 0.05f
        private const val MAX_EYE_MIDPOINT_Y_RATIO = 0.70f
        private const val FACE_CONTINUITY_DISTANCE = 0.22f
        private const val EYE_DISCONTINUITY_DISTANCE = 0.18f
        private const val POSITION_SMOOTHING_ALPHA = 0.42f
    }

    private val appContext = context.applicationContext
    private val stateLock = Any()
    private val inferenceExecutor: ExecutorService = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "BlazeFaceEyeFocus").apply {
            priority = Thread.NORM_PRIORITY - 1
        }
    }
    private val frameInFlight = AtomicBoolean(false)
    private val released = AtomicBoolean(false)
    private val trackingGeneration = AtomicInteger(0)

    @Volatile
    private var activeSampleStartedAtNanos = 0L
    @Volatile
    private var activeCaptureDurationNanos = 0L
    @Volatile
    private var activeProcessingStartedAtNanos = 0L

    private var faceDetector: BlazeFaceDetector? = null
    @Volatile
    private var detectorInitializationFailed = false

    private var lastFaceCenterX: Float? = null
    private var lastFaceCenterY: Float? = null
    private var smoothedEyeX: Float? = null
    private var smoothedEyeY: Float? = null
    private var stableResultCount = 0
    private var stableTargetSinceNanos = 0L
    private var missingResultCount = 0
    private var missingTargetSinceNanos = 0L
    private var hasConfirmedTarget = false

    var onEyeTarget: ((EyeTarget) -> Unit)? = null
    var onPortraitMask: ((PortraitMaskSnapshot) -> Unit)? = null
    var onTargetLost: (() -> Unit)? = null
    var onBusyStateChanged: ((Boolean) -> Unit)? = null
    var onFrameProcessed: ((EyeFocusProcessingTiming) -> Unit)? = null
    var onInitializationError: ((Throwable) -> Unit)? = null
    var onError: ((Throwable) -> Unit)? = null

    fun prewarm() {
        if (released.get()) return
        try {
            inferenceExecutor.execute {
                if (!released.get()) {
                    getOrCreateDetector()
                }
            }
        } catch (_: RejectedExecutionException) {
            // The processor is being released.
        }
    }

    fun processFrame(
        frame: EyeFocusPreviewFrame,
        detectEyeTarget: Boolean,
        createFaceMask: Boolean,
    ): Boolean {
        val detectorPathAvailable =
            (detectEyeTarget || createFaceMask) && !detectorInitializationFailed
        if (
            released.get() ||
            !detectorPathAvailable ||
            !frameInFlight.compareAndSet(false, true)
        ) {
            return false
        }

        activeSampleStartedAtNanos = frame.sampleStartedAtNanos
        activeCaptureDurationNanos = frame.captureDurationNanos
        activeProcessingStartedAtNanos = SystemClock.elapsedRealtimeNanos()
        val generation = trackingGeneration.get()
        onBusyStateChanged?.invoke(true)
        try {
            inferenceExecutor.execute {
                submitBitmap(
                    bitmap = frame.bitmap,
                    generation = generation,
                    detectEyeTarget = detectEyeTarget,
                    createFaceMask = createFaceMask,
                )
            }
            return true
        } catch (error: RejectedExecutionException) {
            finishFrame()
            return false
        }
    }

    fun resetTracking() {
        trackingGeneration.incrementAndGet()
        synchronized(stateLock) {
            resetTrackingStateLocked()
        }
    }

    private fun submitBitmap(
        bitmap: Bitmap,
        generation: Int,
        detectEyeTarget: Boolean,
        createFaceMask: Boolean,
    ) {
        var frameFinished = false
        try {
            if (released.get()) return
            val detector = getOrCreateDetector() ?: return
            val detections = try {
                detector.detect(bitmap)
            } catch (error: RuntimeException) {
                PLog.e(TAG, "BlazeFace eye focus inference failed", error)
                onError?.invoke(error)
                return
            }
            frameFinished = true
            handleResult(
                detections = detections,
                inputWidth = bitmap.width,
                inputHeight = bitmap.height,
                generation = generation,
                eyeTargetRequested = detectEyeTarget,
                faceMaskRequested = createFaceMask,
            )
        } finally {
            if (!bitmap.isRecycled) bitmap.recycle()
            if (!frameFinished) finishFrame()
        }
    }

    private fun getOrCreateDetector(): BlazeFaceDetector? {
        faceDetector?.let { return it }
        if (detectorInitializationFailed || released.get()) return null

        return try {
            BlazeFaceDetector(appContext, MIN_DETECTION_CONFIDENCE).also {
                faceDetector = it
                PLog.d(TAG, "BlazeFace face detector initialized")
            }
        } catch (error: Exception) {
            detectorInitializationFailed = true
            PLog.e(TAG, "Failed to initialize BlazeFace face detector", error)
            onInitializationError?.invoke(error)
            null
        }
    }

    private fun handleResult(
        detections: List<BlazeFaceDetector.Detection>,
        inputWidth: Int,
        inputHeight: Int,
        generation: Int,
        eyeTargetRequested: Boolean,
        faceMaskRequested: Boolean,
    ) {
        val target: EyeTarget?
        val faceMask: PortraitMaskSnapshot?
        var targetLost = false
        var targetConfirmed = false
        synchronized(stateLock) {
            if (generation != trackingGeneration.get() || released.get()) {
                target = null
                faceMask = null
            } else {
                val candidate = selectCandidateLocked(detections, inputWidth, inputHeight)
                faceMask = if (faceMaskRequested && candidate != null) {
                    createFaceMask(candidate)
                } else {
                    null
                }
                if (!eyeTargetRequested) {
                    target = null
                } else if (candidate == null) {
                    val nowNanos = SystemClock.elapsedRealtimeNanos()
                    if (missingResultCount == 0) {
                        missingTargetSinceNanos = nowNanos
                    }
                    missingResultCount++
                    if (
                        hasConfirmedTarget &&
                        missingResultCount >= TARGET_LOST_MIN_RESULTS &&
                        nowNanos - missingTargetSinceNanos >= TARGET_LOST_DURATION_NS
                    ) {
                        hasConfirmedTarget = false
                        targetLost = true
                        resetSelectedTargetLocked()
                    } else if (!hasConfirmedTarget) {
                        // 未确认候选必须连续出现，空帧不能累计到确认窗口。
                        resetSelectedTargetLocked()
                    }
                    target = null
                } else {
                    missingResultCount = 0
                    missingTargetSinceNanos = 0L
                    val previouslyConfirmed = hasConfirmedTarget
                    val stabilizedTarget = stabilizeCandidateLocked(candidate)
                    if (previouslyConfirmed && stableResultCount == 1) {
                        // 目标发生不连续跳变，先撤销旧目标，再重新确认新候选。
                        hasConfirmedTarget = false
                        targetLost = true
                    }
                    if (stabilizedTarget != null) {
                        targetConfirmed = !hasConfirmedTarget
                        hasConfirmedTarget = true
                    }
                    target = stabilizedTarget
                }
            }
        }

        finishFrame()
        faceMask?.let { onPortraitMask?.invoke(it) }
        if (targetConfirmed) {
            PLog.d(TAG, "Human face confirmed: confidence=${target?.confidence}")
        }
        target?.let { onEyeTarget?.invoke(it) }
        if (targetLost) {
            PLog.d(TAG, "Human face target lost")
            onTargetLost?.invoke()
        }
    }

    private fun selectCandidateLocked(
        detections: List<BlazeFaceDetector.Detection>,
        inputWidth: Int,
        inputHeight: Int,
    ): FaceCandidate? {
        if (inputWidth <= 0 || inputHeight <= 0) return null
        val previousFaceX = lastFaceCenterX
        val previousFaceY = lastFaceCenterY

        return detections.mapNotNull { detection ->
            val keypoints = detection.keypoints
            if (keypoints.size < 4) return@mapNotNull null
            val eyeCoordinates = floatArrayOf(
                keypoints[0], keypoints[1], keypoints[2], keypoints[3]
            )
            if (eyeCoordinates.any { !it.isFinite() || it !in 0f..1f }) return@mapNotNull null

            val box = detection.box
            val boxLeft = box.left
            val boxTop = box.top
            val boxWidth = box.width()
            val boxHeight = box.height()
            val confidence = detection.score
            if (
                confidence < MIN_TARGET_CONFIDENCE ||
                !isValidFaceGeometry(
                    boxLeft = boxLeft,
                    boxTop = boxTop,
                    boxWidth = boxWidth,
                    boxHeight = boxHeight,
                    leftEyeX = eyeCoordinates[0],
                    leftEyeY = eyeCoordinates[1],
                    rightEyeX = eyeCoordinates[2],
                    rightEyeY = eyeCoordinates[3],
                )
            ) {
                return@mapNotNull null
            }
            val centerX = box.centerX().coerceIn(0f, 1f)
            val centerY = box.centerY().coerceIn(0f, 1f)
            val area = (boxWidth * boxHeight).coerceAtLeast(0f)
            FaceCandidate(
                faceCenterX = centerX,
                faceCenterY = centerY,
                boxWidth = boxWidth,
                boxHeight = boxHeight,
                area = area,
                confidence = confidence,
                leftEyeX = eyeCoordinates[0],
                leftEyeY = eyeCoordinates[1],
                rightEyeX = eyeCoordinates[2],
                rightEyeY = eyeCoordinates[3],
            )
        }.maxByOrNull { candidate ->
            val centerDistance = distance(candidate.faceCenterX, candidate.faceCenterY, 0.5f, 0.5f)
            val centerWeight = 1f - (centerDistance / 0.71f).coerceIn(0f, 1f) * 0.2f
            val confidenceWeight = 0.75f + candidate.confidence.coerceIn(0f, 1f) * 0.25f
            val continuityWeight = if (
                previousFaceX != null && previousFaceY != null &&
                distance(candidate.faceCenterX, candidate.faceCenterY, previousFaceX, previousFaceY) <=
                FACE_CONTINUITY_DISTANCE
            ) {
                1.8f
            } else {
                1f
            }
            candidate.area * centerWeight * confidenceWeight * continuityWeight
        }
    }

    /**
     * Converts a validated BlazeFace box into a center-weighted facial ellipse. The mask contains
     * no preview luminance and therefore cannot carry tone-mapped brightness into RAW metering.
     */
    private fun createFaceMask(candidate: FaceCandidate): PortraitMaskSnapshot {
        val confidence = FloatArray(FACE_MASK_WIDTH * FACE_MASK_HEIGHT)
        val radiusX = (candidate.boxWidth * 0.5f).coerceAtLeast(1f / FACE_MASK_WIDTH)
        val radiusY = (candidate.boxHeight * 0.5f).coerceAtLeast(1f / FACE_MASK_HEIGHT)
        for (y in 0 until FACE_MASK_HEIGHT) {
            val normalizedY = (y + 0.5f) / FACE_MASK_HEIGHT.toFloat()
            val dy = (normalizedY - candidate.faceCenterY) / radiusY
            for (x in 0 until FACE_MASK_WIDTH) {
                val normalizedX = (x + 0.5f) / FACE_MASK_WIDTH.toFloat()
                val dx = (normalizedX - candidate.faceCenterX) / radiusX
                val radius = sqrt(dx * dx + dy * dy)
                val weight = when {
                    radius <= FACE_MASK_CORE_RADIUS -> 1f
                    radius >= 1f -> 0f
                    else -> {
                        val t = ((1f - radius) / (1f - FACE_MASK_CORE_RADIUS))
                            .coerceIn(0f, 1f)
                        t * t * (3f - 2f * t)
                    }
                }
                confidence[y * FACE_MASK_WIDTH + x] = weight
            }
        }
        return PortraitMaskSnapshot(
            width = FACE_MASK_WIDTH,
            height = FACE_MASK_HEIGHT,
            confidence = confidence,
            sampleElapsedRealtimeNanos = activeSampleStartedAtNanos,
        )
    }

    private fun isValidFaceGeometry(
        boxLeft: Float,
        boxTop: Float,
        boxWidth: Float,
        boxHeight: Float,
        leftEyeX: Float,
        leftEyeY: Float,
        rightEyeX: Float,
        rightEyeY: Float,
    ): Boolean {
        if (
            !boxLeft.isFinite() ||
            !boxTop.isFinite() ||
            !boxWidth.isFinite() ||
            !boxHeight.isFinite() ||
            boxWidth < MIN_FACE_WIDTH_FRACTION ||
            boxHeight < MIN_FACE_HEIGHT_FRACTION
        ) {
            return false
        }
        val aspectRatio = boxWidth / boxHeight
        if (aspectRatio !in MIN_FACE_ASPECT_RATIO..MAX_FACE_ASPECT_RATIO) return false

        val interEyeDistance = distance(leftEyeX, leftEyeY, rightEyeX, rightEyeY)
        val interEyeWidthRatio = interEyeDistance / boxWidth
        if (interEyeWidthRatio !in MIN_INTER_EYE_FACE_WIDTH_RATIO..MAX_INTER_EYE_FACE_WIDTH_RATIO) {
            return false
        }
        if (abs(leftEyeY - rightEyeY) / boxHeight > MAX_EYE_VERTICAL_FACE_HEIGHT_RATIO) {
            return false
        }

        val midpointXRatio = ((leftEyeX + rightEyeX) * 0.5f - boxLeft) / boxWidth
        val midpointYRatio = ((leftEyeY + rightEyeY) * 0.5f - boxTop) / boxHeight
        return midpointXRatio in MIN_EYE_MIDPOINT_X_RATIO..MAX_EYE_MIDPOINT_X_RATIO &&
            midpointYRatio in MIN_EYE_MIDPOINT_Y_RATIO..MAX_EYE_MIDPOINT_Y_RATIO
    }

    private fun stabilizeCandidateLocked(candidate: FaceCandidate): EyeTarget? {
        val previousFaceX = lastFaceCenterX
        val previousFaceY = lastFaceCenterY
        val sameFace = previousFaceX != null && previousFaceY != null &&
            distance(candidate.faceCenterX, candidate.faceCenterY, previousFaceX, previousFaceY) <=
            FACE_CONTINUITY_DISTANCE

        val referenceX = if (sameFace) smoothedEyeX ?: 0.5f else 0.5f
        val referenceY = if (sameFace) smoothedEyeY ?: 0.5f else 0.5f
        val leftDistance = distance(candidate.leftEyeX, candidate.leftEyeY, referenceX, referenceY)
        val rightDistance = distance(candidate.rightEyeX, candidate.rightEyeY, referenceX, referenceY)
        val rawEyeX = if (leftDistance <= rightDistance) candidate.leftEyeX else candidate.rightEyeX
        val rawEyeY = if (leftDistance <= rightDistance) candidate.leftEyeY else candidate.rightEyeY

        val previousEyeX = smoothedEyeX
        val previousEyeY = smoothedEyeY
        val eyeDiscontinuous = !sameFace || previousEyeX == null || previousEyeY == null ||
            distance(rawEyeX, rawEyeY, previousEyeX, previousEyeY) > EYE_DISCONTINUITY_DISTANCE
        if (eyeDiscontinuous) {
            smoothedEyeX = rawEyeX
            smoothedEyeY = rawEyeY
            stableResultCount = 1
            stableTargetSinceNanos = SystemClock.elapsedRealtimeNanos()
        } else {
            smoothedEyeX = previousEyeX + (rawEyeX - previousEyeX) * POSITION_SMOOTHING_ALPHA
            smoothedEyeY = previousEyeY + (rawEyeY - previousEyeY) * POSITION_SMOOTHING_ALPHA
            stableResultCount++
        }
        lastFaceCenterX = candidate.faceCenterX
        lastFaceCenterY = candidate.faceCenterY

        val stableDurationNanos =
            SystemClock.elapsedRealtimeNanos() - stableTargetSinceNanos
        if (
            stableResultCount < REQUIRED_STABLE_RESULTS ||
            stableDurationNanos < TARGET_CONFIRMATION_DURATION_NS
        ) {
            return null
        }
        return EyeTarget(
            x = smoothedEyeX?.coerceIn(0f, 1f) ?: return null,
            y = smoothedEyeY?.coerceIn(0f, 1f) ?: return null,
            confidence = candidate.confidence,
        )
    }

    private fun finishFrame() {
        val completedAtNanos = SystemClock.elapsedRealtimeNanos()
        val sampleStartedAtNanos = activeSampleStartedAtNanos
        val processingStartedAtNanos = activeProcessingStartedAtNanos
        val captureDurationNanos = activeCaptureDurationNanos
        activeSampleStartedAtNanos = 0L
        activeCaptureDurationNanos = 0L
        activeProcessingStartedAtNanos = 0L
        frameInFlight.set(false)
        if (sampleStartedAtNanos > 0L && processingStartedAtNanos > 0L) {
            onFrameProcessed?.invoke(
                EyeFocusProcessingTiming(
                    captureDurationNanos = captureDurationNanos.coerceAtLeast(0L),
                    processingDurationNanos =
                        (completedAtNanos - processingStartedAtNanos).coerceAtLeast(0L),
                    endToEndDurationNanos =
                        (completedAtNanos - sampleStartedAtNanos).coerceAtLeast(0L),
                )
            )
        }
        onBusyStateChanged?.invoke(false)
    }

    private fun resetTrackingStateLocked() {
        missingResultCount = 0
        missingTargetSinceNanos = 0L
        hasConfirmedTarget = false
        resetSelectedTargetLocked()
    }

    private fun resetSelectedTargetLocked() {
        lastFaceCenterX = null
        lastFaceCenterY = null
        smoothedEyeX = null
        smoothedEyeY = null
        stableResultCount = 0
        stableTargetSinceNanos = 0L
    }

    override fun close() {
        if (!released.compareAndSet(false, true)) return
        trackingGeneration.incrementAndGet()
        try {
            inferenceExecutor.execute {
                faceDetector?.close()
                faceDetector = null
                frameInFlight.set(false)
            }
        } catch (_: RejectedExecutionException) {
            faceDetector?.close()
            faceDetector = null
            frameInFlight.set(false)
        } finally {
            inferenceExecutor.shutdown()
        }
    }

    private fun distance(x1: Float, y1: Float, x2: Float, y2: Float): Float =
        hypot(x1 - x2, y1 - y2)
}
