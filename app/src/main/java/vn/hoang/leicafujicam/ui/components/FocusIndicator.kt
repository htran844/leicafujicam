package vn.hoang.leicafujicam.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitTouchSlopOrCancellation
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Icon
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.camera.FocusPointSource
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * 对焦指示器
 */
@Composable
fun FocusIndicator(
    position: Pair<Float, Float>?,
    meteringPosition: Pair<Float, Float>? = null,
    canMoveMeteringPoint: Boolean = false,
    onMeteringPointMove: (Float, Float, Float) -> Unit = { _, _, _ -> },
    onFocusPointMove: (Float, Float, Float, Boolean) -> Unit = { _, _, _, _ -> },
    onMeteringPointMerge: () -> Unit = {},
    source: FocusPointSource = FocusPointSource.MANUAL,
    isFocusLocked: Boolean = false,
    isFocusing: Boolean,
    focusSuccess: Boolean?,
    modifier: Modifier = Modifier
) {
    var visible by remember { mutableStateOf(position != null) }
    val isSeparated = source == FocusPointSource.MANUAL && meteringPosition != null
    val currentFocusPosition by rememberUpdatedState(position)
    val currentIsSeparated by rememberUpdatedState(isSeparated)
    val currentMeteringPosition by rememberUpdatedState(meteringPosition ?: position)
    val currentOnMeteringPointMove by rememberUpdatedState(onMeteringPointMove)
    val currentOnFocusPointMove by rememberUpdatedState(onFocusPointMove)
    val currentOnMeteringPointMerge by rememberUpdatedState(onMeteringPointMerge)
    val textMeasurer = rememberTextMeasurer()
    val labelStyle = TextStyle(
        color = Color.White,
        fontSize = 12.sp,
        shadow = Shadow(Color.Black, Offset(0f, 1f), 3f),
    )
    val focusLabel = textMeasurer.measure(stringResource(R.string.camera_focus_point_label), labelStyle)
    val meteringLabel = textMeasurer.measure(stringResource(R.string.camera_metering_point_label), labelStyle)
    
    // 透明度动画
    val alpha by animateFloatAsState(
        targetValue = when {
            isSeparated -> 1f
            isFocusing -> 1f
            source == FocusPointSource.EYE && position != null -> 0.9f
            isFocusLocked && focusSuccess != false -> 0.9f
            focusSuccess == true -> 0.8f
            isFocusLocked -> 0.6f
            focusSuccess == false -> 0.5f
            else -> 0f
        },
        animationSpec = tween(300),
        label = "focusAlpha"
    )

    // 聚焦动画
    val scale by animateFloatAsState(
        targetValue = if (focusSuccess == true) 0.8f else 1f,
        animationSpec = tween(300),
        label = "focusScale"
    )
    
    // 颜色
    val color = when (focusSuccess) {
        true -> Color.Green
        false -> Color.Red
        else -> if (source == FocusPointSource.EYE) Color(0xFF64D8FF) else Color.White
    }

    LaunchedEffect(position) {
        if (position != null) {
            visible = true
        } else {
            delay(200)
            visible = false
        }
    }

    val displayPosition = position ?: Pair(0f, 0f)
    val density = LocalDensity.current

    AnimatedVisibility(visible = visible, modifier = modifier.fillMaxSize()) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            Canvas(
                modifier = Modifier.fillMaxSize()
                    .pointerInput(canMoveMeteringPoint, source, position != null) {
                        if (!canMoveMeteringPoint || source != FocusPointSource.MANUAL || position == null) {
                            return@pointerInput
                        }
                        val hitRadius = 24.dp.toPx()
                        val circleRadius = 11.dp.toPx()
                        val focusHalfSize = 30.dp.toPx()
                        val focusHitHalfSize = 38.dp.toPx()
                        val mergeRadius = 16.dp.toPx()
                        awaitEachGesture {
                            val down = awaitFirstDown(requireUnconsumed = false)
                            val point = currentMeteringPosition ?: return@awaitEachGesture
                            val focus = currentFocusPosition ?: return@awaitEachGesture
                            val meteringCenter = Offset(point.first * size.width, point.second * size.height)
                            val focusCenter = Offset(focus.first * size.width, focus.second * size.height)
                            val meteringDistance = (down.position - meteringCenter).getDistance()
                            val focusDelta = down.position - focusCenter
                            val hitsMetering = meteringDistance <= hitRadius
                            val hitsFocus = currentIsSeparated &&
                                abs(focusDelta.x) <= focusHitHalfSize && abs(focusDelta.y) <= focusHitHalfSize
                            if (!hitsMetering && !hitsFocus) return@awaitEachGesture
                            // 命中范围重叠时优先圈本身，其余选择较近的标记；一次手势只拖动一个目标。
                            val draggingFocus = hitsFocus && (!hitsMetering ||
                                (meteringDistance > circleRadius && focusDelta.getDistance() < meteringDistance))
                            val center = if (draggingFocus) focusCenter else meteringCenter
                            // 只在越过拖动阈值后消费事件，保留原有点按与长按锁焦。
                            val start = awaitTouchSlopOrCancellation(down.id) { change, _ ->
                                if (currentEvent.changes.count { it.pressed } == 1) change.consume()
                            }
                                ?: return@awaitEachGesture
                            val grabOffset = center - down.position
                            var separatedDuringDrag = currentIsSeparated
                            var mergedDuringDrag = false
                            var lastFocusTarget: Offset? = null
                            var lastFocusAspectRatio = 1f
                            fun move(pointer: Offset) {
                                if (mergedDuringDrag) return
                                val width = size.width.toFloat()
                                val height = size.height.toFloat()
                                if (width <= 0f || height <= 0f) return
                                val radius = if (draggingFocus) focusHalfSize else circleRadius
                                val insetX = radius.coerceAtMost(width / 2f)
                                val insetY = radius.coerceAtMost(height / 2f)
                                val target = pointer + grabOffset
                                val normalized = Offset(
                                    target.x.coerceIn(insetX, width - insetX) / width,
                                    target.y.coerceIn(insetY, height - insetY) / height,
                                )
                                if (draggingFocus) {
                                    lastFocusTarget = normalized
                                    lastFocusAspectRatio = width / height
                                    currentOnFocusPointMove(normalized.x, normalized.y, width / height, false)
                                } else {
                                    val distanceToFocus = (target - focusCenter).getDistance()
                                    if (separatedDuringDrag && distanceToFocus <= mergeRadius) {
                                        mergedDuringDrag = true
                                        currentOnMeteringPointMerge()
                                    } else if (separatedDuringDrag || distanceToFocus > hitRadius) {
                                        // 拉出距离大于吸附距离，防止中心附近反复分离、合并。
                                        separatedDuringDrag = true
                                        currentOnMeteringPointMove(normalized.x, normalized.y, width / height)
                                    }
                                }
                            }
                            try {
                                move(start.position)
                                drag(start.id) { change ->
                                    if (currentEvent.changes.count { it.pressed } == 1) {
                                        change.consume()
                                        move(change.position)
                                    }
                                }
                            } finally {
                                // 包括多指接管或手势取消，均以最后接受的位置结束，避免留下未完成的 AF 状态。
                                lastFocusTarget?.let { target ->
                                    currentOnFocusPointMove(target.x, target.y, lastFocusAspectRatio, true)
                                }
                            }
                        }
                    }
            ) {
                val x = displayPosition.first * size.width
                val y = displayPosition.second * size.height
                val boxSize = (if (source == FocusPointSource.EYE) 24.dp else 60.dp).toPx() * scale
                val halfSize = boxSize / 2
                val cornerLength = (if (source == FocusPointSource.EYE) 6.dp else 15.dp).toPx()
                val strokeWidth = (if (source == FocusPointSource.EYE) 1.5.dp else 2.dp).toPx()

                val drawColor = color.copy(alpha = alpha)

                // 左上角
                drawLine(
                    color = drawColor,
                    start = Offset(x - halfSize, y - halfSize),
                    end = Offset(x - halfSize + cornerLength, y - halfSize),
                    strokeWidth = strokeWidth
                )
                drawLine(
                    color = drawColor,
                    start = Offset(x - halfSize, y - halfSize),
                    end = Offset(x - halfSize, y - halfSize + cornerLength),
                    strokeWidth = strokeWidth
                )

                // 右上角
                drawLine(
                    color = drawColor,
                    start = Offset(x + halfSize, y - halfSize),
                    end = Offset(x + halfSize - cornerLength, y - halfSize),
                    strokeWidth = strokeWidth
                )
                drawLine(
                    color = drawColor,
                    start = Offset(x + halfSize, y - halfSize),
                    end = Offset(x + halfSize, y - halfSize + cornerLength),
                    strokeWidth = strokeWidth
                )

                // 左下角
                drawLine(
                    color = drawColor,
                    start = Offset(x - halfSize, y + halfSize),
                    end = Offset(x - halfSize + cornerLength, y + halfSize),
                    strokeWidth = strokeWidth
                )
                drawLine(
                    color = drawColor,
                    start = Offset(x - halfSize, y + halfSize),
                    end = Offset(x - halfSize, y + halfSize - cornerLength),
                    strokeWidth = strokeWidth
                )

                // 右下角
                drawLine(
                    color = drawColor,
                    start = Offset(x + halfSize, y + halfSize),
                    end = Offset(x + halfSize - cornerLength, y + halfSize),
                    strokeWidth = strokeWidth
                )
                drawLine(
                    color = drawColor,
                    start = Offset(x + halfSize, y + halfSize),
                    end = Offset(x + halfSize, y + halfSize - cornerLength),
                    strokeWidth = strokeWidth
                )

                if (source == FocusPointSource.MANUAL && (canMoveMeteringPoint || isSeparated)) {
                    val point = meteringPosition ?: displayPosition
                    val center = Offset(point.first * size.width, point.second * size.height)
                    val radius = 11.dp.toPx()
                    drawCircle(
                        color = Color.White.copy(alpha = alpha),
                        radius = radius,
                        center = center,
                        style = Stroke(width = 1.5.dp.toPx()),
                    )
                    if (isSeparated) {
                        fun labelTopLeft(center: Offset, radius: Float, width: Int, height: Int): Offset {
                            val below = center.y + radius + 5.dp.toPx()
                            val top = if (below + height <= size.height) below
                                else center.y - radius - 5.dp.toPx() - height
                            return Offset(
                                (center.x - width / 2f).coerceIn(0f, (size.width - width).coerceAtLeast(0f)),
                                top.coerceIn(0f, (size.height - height).coerceAtLeast(0f)),
                            )
                        }
                        drawText(
                            focusLabel,
                            topLeft = labelTopLeft(Offset(x, y), halfSize, focusLabel.size.width, focusLabel.size.height),
                        )
                        drawText(
                            meteringLabel,
                            topLeft = labelTopLeft(center, radius, meteringLabel.size.width, meteringLabel.size.height),
                        )
                    }
                }
            }

            if (source == FocusPointSource.MANUAL && isFocusLocked && position != null) {
                val badgeSize = 24.dp
                val badgeSizePx = with(density) { badgeSize.toPx() }
                val focusHalfSizePx = with(density) { 30.dp.toPx() }
                val maxWidthPx = constraints.maxWidth.toFloat()
                val maxHeightPx = constraints.maxHeight.toFloat()
                val offsetX = (displayPosition.first * maxWidthPx + focusHalfSizePx - badgeSizePx * 0.55f)
                    .coerceIn(0f, (maxWidthPx - badgeSizePx).coerceAtLeast(0f))
                val offsetY = (displayPosition.second * maxHeightPx - focusHalfSizePx - badgeSizePx * 0.45f)
                    .coerceIn(0f, (maxHeightPx - badgeSizePx).coerceAtLeast(0f))
                val badgeColor = color.copy(alpha = alpha.coerceAtLeast(0.75f))

                Box(
                    modifier = Modifier
                        .offset { IntOffset(offsetX.roundToInt(), offsetY.roundToInt()) }
                        .size(badgeSize)
                        .clip(CircleShape)
                        .background(Color.Black.copy(alpha = 0.55f))
                        .border(1.dp, badgeColor, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Filled.Lock,
                        contentDescription = null,
                        tint = badgeColor,
                        modifier = Modifier.size(14.dp)
                    )
                }
            }
        }
    }
}
