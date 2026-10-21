package vn.hoang.leicafujicam.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.expandHorizontally
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkHorizontally
import androidx.compose.animation.core.animate
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sign

/** 越过阈值后的拖动阻尼，用于给出"已到位"的手感。 */
private const val OverDragResistance = 0.35f
private const val CurtainMaxDimAlpha = 0.32f
private const val CommitSweepMillis = 180
private const val CurtainFadeMillis = 260
/** 切换后名称标签在画面中央停留的时长（自幕布扫满起算）。 */
private const val CommittedLabelHoldMillis = 1_200L
private const val CommittedLabelFadeMillis = 250

/**
 * 预览区横向滑动切换 LUT 的拖动状态。
 *
 * 手势层通过 [begin] / [drag] / [release] / [cancel] 驱动，[LutSwipeIndicator] 负责渲染：
 * - 拖动中：从来向边缘拉出一块"幕布"，前沿跟随手指，并展示目标 LUT 名称；
 * - 越过阈值：标签高亮并触发一次触感反馈，松手即切换；
 * - 提交：幕布扫过整个画面后淡出，露出已应用新 LUT 的预览；名称标签随幕布移到画面中央，
 *   停留约 [CommittedLabelHoldMillis] 后淡出，作为切换完成的确认；
 * - 未达阈值：幕布弹回。
 */
@Stable
class LutSwipeState internal constructor(
    private val coroutineScope: CoroutineScope,
) {
    /** 手指原始横向位移（px），向左为负。 */
    private var rawOffset by mutableFloatStateOf(0f)

    /** 渲染用的幕布宽度（px），带符号：负值表示幕布从右边缘拉出（切到下一个）。 */
    var curtainOffset by mutableFloatStateOf(0f)
        private set

    /** 幕布透明度，用于提交后淡出。 */
    var curtainAlpha by mutableFloatStateOf(1f)
        private set

    /** 名称标签透明度，提交后在中央停留结束时淡出。 */
    var labelAlpha by mutableFloatStateOf(1f)
        private set

    var active by mutableStateOf(false)
        private set

    /** 已越过切换阈值，松手即切换。 */
    var armed by mutableStateOf(false)
        private set

    /** 已确认切换，正在播放扫过动画与名称确认。 */
    var committing by mutableStateOf(false)
        private set

    /** 标签与幕布使用"到位"样式。 */
    val highlighted: Boolean
        get() = armed || committing

    var containerWidth by mutableIntStateOf(0)
        private set

    var previousName by mutableStateOf<String?>(null)
        private set

    var nextName by mutableStateOf<String?>(null)
        private set

    var thresholdPx by mutableFloatStateOf(1f)
        private set

    /** 提交时确定的 LUT 名称，不再随幕布位置变化。 */
    private var committedName by mutableStateOf<String?>(null)

    private var settleJob: Job? = null

    /** 当前拖动方向对应的目标 LUT 名称；提交后为已切换的 LUT 名称。 */
    val targetName: String?
        get() = when {
            committing -> committedName
            curtainOffset < 0f -> nextName
            curtainOffset > 0f -> previousName
            else -> null
        }

    /** 当前拖动进度 0..1（相对切换阈值）。 */
    val progress: Float
        get() = (abs(rawOffset) / thresholdPx).coerceIn(0f, 1f)

    fun begin(width: Int, threshold: Float, previousName: String?, nextName: String?) {
        settleJob?.cancel()
        settleJob = null
        containerWidth = width
        thresholdPx = threshold.coerceAtLeast(1f)
        this.previousName = previousName
        this.nextName = nextName
        rawOffset = 0f
        curtainOffset = 0f
        curtainAlpha = 1f
        labelAlpha = 1f
        armed = false
        committing = false
        committedName = null
        active = true
    }

    fun drag(offset: Float) {
        if (!active) return
        rawOffset = offset
        curtainOffset = resistedOffset(offset)
        armed = abs(offset) >= thresholdPx
    }

    /**
     * 松手。满足阈值或快速甩动（同向且已有一定位移）时返回切换方向：
     * -1 切到下一个，1 切到上一个，0 不切换。
     */
    fun release(velocityX: Float, flingVelocity: Float, minFlingDistance: Float): Int {
        if (!active) return 0
        val distance = abs(rawOffset)
        val direction = sign(rawOffset).toInt()
        val flung = abs(velocityX) >= flingVelocity &&
            distance >= minFlingDistance &&
            sign(velocityX).toInt() == direction
        return if (direction != 0 && (armed || flung)) direction else 0
    }

    /**
     * 提交切换：幕布扫过整个画面后淡出；幕布扫满时标签恰好居中，
     * 随后在中央停留并淡出。
     */
    fun commit(direction: Int, name: String) {
        if (!active) return
        val width = containerWidth.toFloat()
        committedName = name
        committing = true
        settleJob?.cancel()
        settleJob = coroutineScope.launch {
            animate(
                initialValue = curtainOffset,
                targetValue = direction * width,
                animationSpec = tween(durationMillis = CommitSweepMillis),
            ) { value, _ -> curtainOffset = value }
            animate(
                initialValue = 1f,
                targetValue = 0f,
                animationSpec = tween(durationMillis = CurtainFadeMillis),
            ) { value, _ -> curtainAlpha = value }
            delay(CommittedLabelHoldMillis - CurtainFadeMillis)
            animate(
                initialValue = 1f,
                targetValue = 0f,
                animationSpec = tween(durationMillis = CommittedLabelFadeMillis),
            ) { value, _ -> labelAlpha = value }
            reset()
        }
    }

    /** 取消：幕布弹回。 */
    fun cancel() {
        if (!active) return
        armed = false
        settleJob?.cancel()
        settleJob = coroutineScope.launch {
            animate(
                initialValue = curtainOffset,
                targetValue = 0f,
                animationSpec = spring(dampingRatio = 0.8f, stiffness = 600f),
            ) { value, _ -> curtainOffset = value }
            reset()
        }
    }

    private fun reset() {
        rawOffset = 0f
        curtainOffset = 0f
        curtainAlpha = 1f
        labelAlpha = 1f
        armed = false
        committing = false
        committedName = null
        active = false
        settleJob = null
    }

    private fun resistedOffset(offset: Float): Float {
        val distance = abs(offset)
        val resisted = if (distance <= thresholdPx) {
            distance
        } else {
            thresholdPx + (distance - thresholdPx) * OverDragResistance
        }
        val limit = if (containerWidth > 0) containerWidth.toFloat() else resisted
        return sign(offset) * min(resisted, limit)
    }
}

@Composable
fun rememberLutSwipeState(): LutSwipeState {
    val coroutineScope = rememberCoroutineScope()
    return remember(coroutineScope) { LutSwipeState(coroutineScope) }
}

@Composable
fun LutSwipeIndicator(
    state: LutSwipeState,
    modifier: Modifier = Modifier,
) {
    val haptic = LocalHapticFeedback.current
    LaunchedEffect(state) {
        snapshotFlow { state.active to state.armed }
            .distinctUntilChanged()
            .drop(1)
            .collect { (active, armed) ->
                // 只在拖动中跨越阈值时反馈；提交后的反馈由切换本身负责。
                if (active && armed) {
                    haptic.performHapticFeedback(HapticFeedbackType.SegmentTick)
                }
            }
    }

    if (!state.active) return

    Box(modifier = modifier.fillMaxSize()) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { alpha = state.curtainAlpha }
        ) {
            val offset = state.curtainOffset
            if (offset == 0f) return@Canvas
            val curtainWidth = min(abs(offset), size.width)
            val fromRight = offset < 0f
            val edgeX = if (fromRight) size.width - curtainWidth else curtainWidth
            val left = if (fromRight) edgeX else 0f
            val dim = CurtainMaxDimAlpha * max(state.progress, if (state.highlighted) 1f else 0f)

            drawRect(
                color = Color.Black.copy(alpha = dim),
                topLeft = Offset(left, 0f),
                size = Size(curtainWidth, size.height),
            )

            // 前沿阴影 + 高光线，让幕布边界清晰可见
            val shadowWidth = 24.dp.toPx()
            val shadowLeft = if (fromRight) edgeX - shadowWidth else edgeX
            drawRect(
                brush = Brush.horizontalGradient(
                    colors = if (fromRight) {
                        listOf(Color.Transparent, Color.Black.copy(alpha = dim * 0.8f))
                    } else {
                        listOf(Color.Black.copy(alpha = dim * 0.8f), Color.Transparent)
                    },
                    startX = shadowLeft,
                    endX = shadowLeft + shadowWidth,
                ),
                topLeft = Offset(shadowLeft, 0f),
                size = Size(shadowWidth, size.height),
            )
            val lineWidth = if (state.highlighted) 2.dp.toPx() else 1.dp.toPx()
            drawRect(
                color = Color.White.copy(alpha = 0.35f + 0.55f * state.progress),
                topLeft = Offset(edgeX - lineWidth / 2f, 0f),
                size = Size(lineWidth, size.height),
            )
        }

        val targetName = state.targetName
        if (targetName != null) {
            LutSwipeTargetLabel(
                name = targetName,
                fromRight = state.curtainOffset < 0f,
                // 方向箭头只在拖动中指示方向；切换后居中的名称确认不再带箭头
                showArrow = !state.committing,
                armed = state.highlighted,
                progress = if (state.committing) 1f else state.progress,
                labelAlpha = state.labelAlpha,
                curtainOffset = state.curtainOffset,
            )
        }
    }
}

@Composable
private fun LutSwipeTargetLabel(
    name: String,
    fromRight: Boolean,
    showArrow: Boolean,
    armed: Boolean,
    progress: Float,
    labelAlpha: Float,
    curtainOffset: Float,
) {
    val containerColor by animateColorAsState(
        targetValue = if (armed) Color.White.copy(alpha = 0.92f) else Color.Black.copy(alpha = 0.45f),
        animationSpec = tween(durationMillis = 120),
        label = "lutSwipeLabelContainer",
    )
    val contentColor by animateColorAsState(
        targetValue = if (armed) Color.Black else Color.White,
        animationSpec = tween(durationMillis = 120),
        label = "lutSwipeLabelContent",
    )
    val startPadding by animateDpAsState(
        targetValue = if (showArrow) 6.dp else 12.dp,
        animationSpec = tween(durationMillis = CommitSweepMillis),
        label = "lutSwipeLabelStartPadding",
    )
    val gap = 12.dp

    // 标签贴着幕布前沿内侧居中，跟随手指移动；幕布太窄时贴边显示。
    Layout(
        modifier = Modifier.fillMaxSize(),
        content = {
            Surface(
                color = containerColor,
                shape = RoundedCornerShape(16.dp),
                shadowElevation = 4.dp,
                modifier = Modifier.graphicsLayer {
                    alpha = (progress * 1.6f).coerceIn(0f, 1f) * labelAlpha
                    val scale = 0.9f + 0.1f * progress + if (armed) 0.04f else 0f
                    scaleX = scale
                    scaleY = scale
                },
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(start = startPadding, end = 12.dp, top = 6.dp, bottom = 6.dp),
                ) {
                    AnimatedVisibility(
                        visible = showArrow,
                        enter = fadeIn(tween(durationMillis = 120)) + expandHorizontally(),
                        exit = fadeOut(tween(durationMillis = 120)) +
                            shrinkHorizontally(tween(durationMillis = CommitSweepMillis)),
                    ) {
                        Icon(
                            imageVector = if (fromRight) {
                                Icons.AutoMirrored.Filled.KeyboardArrowLeft
                            } else {
                                Icons.AutoMirrored.Filled.KeyboardArrowRight
                            },
                            contentDescription = null,
                            tint = contentColor,
                            modifier = Modifier.size(18.dp),
                        )
                    }
                    Text(
                        text = name,
                        color = contentColor,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold,
                        style = if (armed) TextStyle.Default else TextStyle(
                            shadow = vn.hoang.leicafujicam.ui.camera.ViewfinderTextShadow
                        ),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.widthIn(max = 180.dp),
                    )
                }
            }
        },
    ) { measurables, constraints ->
        val placeable = measurables.first().measure(Constraints(maxWidth = constraints.maxWidth))
        layout(constraints.maxWidth, constraints.maxHeight) {
            val width = constraints.maxWidth
            val curtainWidth = min(abs(curtainOffset), width.toFloat())
            val gapPx = gap.roundToPx()
            val centerInCurtain = curtainWidth / 2f
            val x = if (fromRight) {
                val edgeX = width - curtainWidth
                (edgeX + max(centerInCurtain - placeable.width / 2f, gapPx.toFloat()))
                    .coerceAtMost((width - placeable.width - gapPx).toFloat())
            } else {
                (curtainWidth - max(centerInCurtain + placeable.width / 2f, (placeable.width + gapPx).toFloat()))
                    .coerceAtLeast(gapPx.toFloat())
            }
            val y = (constraints.maxHeight - placeable.height) / 2
            placeable.place(x.toInt(), y)
        }
    }
}
