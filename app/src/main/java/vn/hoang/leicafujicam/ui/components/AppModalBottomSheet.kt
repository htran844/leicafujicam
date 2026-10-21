package vn.hoang.leicafujicam.ui.components

import android.os.Build
import android.view.View
import android.view.ViewParent
import android.view.Window
import android.view.WindowManager
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.areStatusBarsVisible
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SheetState
import androidx.compose.material3.SheetValue
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.util.function.Consumer
import kotlin.math.roundToInt

/** Panel material shared by sheets and in-window camera panels. */
object AppPanelMaterial {
    val Tint = Color(0xFF1C1C1E)

    /** Tint alpha of a translucent panel that keeps the content behind it visible. */
    const val TranslucentAlpha = 0.78f

    /** Hairline that separates a translucent panel from bright content behind it. */
    val EdgeHighlight = Color.White.copy(alpha = 0.10f)

    val TranslucentColor: Color get() = Tint.copy(alpha = TranslucentAlpha)
}

/** Background material of an [AppModalBottomSheet]. */
enum class AppSheetStyle {
    /** Modal sheet: the whole screen behind it is dimmed and blurred, the panel is translucent. */
    Frosted,

    /**
     * Live-adjustment sheet: no scrim and no blur, so the content behind stays visible while the
     * panel is used; the panel itself is translucent.
     */
    Translucent,
}

private val SheetCornerRadius = 28.dp
private val SheetShape = RoundedCornerShape(topStart = SheetCornerRadius, topEnd = SheetCornerRadius)
private const val FrostedTintAlpha = 0.6f
private const val FrostedTintAlphaWithoutBlur = 0.92f
private val FrostedScrimColor = Color.Black.copy(alpha = 0.28f)
private val FrostedBlurRadius = 36.dp
private const val BlurAnimationMillis = 280

/**
 * Sheet state used by [AppModalBottomSheet]. The half-expanded anchor is skipped so the anchor set
 * never changes when content height crosses half of the window.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun rememberAppModalBottomSheetState(): SheetState =
    rememberModalBottomSheetState(skipPartiallyExpanded = true)

/**
 * Project-wide [ModalBottomSheet] owning the sheet material and a sheet geometry that does not
 * change while it is dragged.
 *
 * Material: callers may pin [containerColor], [scrimColor] and [shape] for scenes with a dedicated
 * look; otherwise [AppSheetStyle.Frosted] blurs the screen behind the dialog window through cross-window
 * blur (Android 12+) and animates the blur radius with the sheet visibility. When cross-window blur
 * is unavailable or disabled at runtime (e.g. battery saver), the panel tint becomes nearly opaque
 * to keep content legible.
 *
 * Geometry: Material3's sheet drives drags coming from nested scrolling through `dispatchRawDelta`,
 * which does not hold the drag mutex. Whenever the sheet height or the available height changes
 * during such a drag, the anchors are rebuilt and the offset snaps back to an anchor, so the sheet
 * can't be pulled down or twitches every frame. This wrapper removes every source of such changes:
 * - The top inset is not part of [ModalBottomSheet]'s `contentWindowInsets`, because the sheet
 *   consumes the top inset by its current offset and the content height would follow the drag.
 * - The top gap is reserved inside the dialog from insets that ignore visibility, so status bar
 *   show/hide animations can't move the anchors.
 * - The dialog window mirrors the host's status bar visibility. A dialog window takes the insets
 *   control when it gains focus and would otherwise bring back a status bar the host has hidden.
 * - The half-expanded anchor is skipped by the default [sheetState].
 *
 * Content contract: keep headers outside the scrolling container so they drag the sheet directly,
 * and bound the single vertical scrolling container with `Modifier.weight(1f, fill = false)` (or
 * `weight(1f)` for a sheet that always fills the available height). Bottom system bar insets are
 * already applied, so content must not add `navigationBarsPadding()`. Content must not paint an
 * opaque background over the sheet material.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun AppModalBottomSheet(
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    sheetState: SheetState = rememberAppModalBottomSheetState(),
    style: AppSheetStyle = AppSheetStyle.Frosted,
    containerColor: Color? = null,
    scrimColor: Color? = null,
    shape: Shape? = null,
    contentColor: Color = Color.White,
    dragHandle: @Composable (() -> Unit)? = {
        BottomSheetDefaults.DragHandle(color = Color.White.copy(alpha = 0.28f))
    },
    content: @Composable ColumnScope.() -> Unit,
) {
    // Read in the host composition, where the insets belong to the host window.
    val hostStatusBarsVisible = WindowInsets.areStatusBarsVisible
    // A caller-specified container is a deliberate material of its own: it keeps its color and gets
    // no backdrop blur, and a frosted sheet falls back to Material's scrim unless one is specified.
    val usesSheetMaterial = containerColor == null
    val blurSupported = usesSheetMaterial && style == AppSheetStyle.Frosted && rememberCrossWindowBlurEnabled()
    val tintAlpha = when {
        style == AppSheetStyle.Translucent -> AppPanelMaterial.TranslucentAlpha
        blurSupported -> FrostedTintAlpha
        else -> FrostedTintAlphaWithoutBlur
    }
    val blurProgress by animateFloatAsState(
        targetValue = if (blurSupported && sheetState.targetValue != SheetValue.Hidden) 1f else 0f,
        animationSpec = tween(BlurAnimationMillis),
        label = "sheetBackdropBlur"
    )
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        modifier = modifier,
        sheetState = sheetState,
        shape = shape ?: SheetShape,
        containerColor = containerColor ?: AppPanelMaterial.Tint.copy(alpha = tintAlpha),
        contentColor = contentColor,
        tonalElevation = 0.dp,
        scrimColor = scrimColor ?: when {
            style == AppSheetStyle.Translucent -> Color.Transparent
            usesSheetMaterial -> FrostedScrimColor
            else -> BottomSheetDefaults.ScrimColor
        },
        dragHandle = dragHandle,
        contentWindowInsets = {
            WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal)
        },
    ) {
        // Everything below runs in the dialog composition, where the insets belong to the dialog.
        val window = LocalView.current.findDialogWindowProvider()?.window
        DisposableEffect(window, hostStatusBarsVisible) {
            if (window != null) {
                WindowCompat.getInsetsController(window, window.decorView).apply {
                    systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    if (hostStatusBarsVisible) {
                        show(WindowInsetsCompat.Type.statusBars())
                    } else {
                        hide(WindowInsetsCompat.Type.statusBars())
                    }
                }
            }
            onDispose { }
        }
        if (window != null && usesSheetMaterial && style == AppSheetStyle.Frosted && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val maxBlurRadiusPx = with(LocalDensity.current) { FrostedBlurRadius.toPx() }
            LaunchedEffect(window, maxBlurRadiusPx) {
                window.addFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND)
                snapshotFlow { (maxBlurRadiusPx * blurProgress).roundToInt() }
                    .collect { radius -> window.setBlurBehindRadius(radius) }
            }
        }
        val topGapPx = WindowInsets.statusBarsIgnoringVisibility
            .union(WindowInsets.displayCutout)
            .getTop(LocalDensity.current)
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .layout { measurable, constraints ->
                    val maxHeight = if (constraints.hasBoundedHeight) {
                        (constraints.maxHeight - topGapPx).coerceAtLeast(0)
                    } else {
                        constraints.maxHeight
                    }
                    val placeable = measurable.measure(
                        constraints.copy(
                            minHeight = constraints.minHeight.coerceAtMost(maxHeight),
                            maxHeight = maxHeight
                        )
                    )
                    layout(placeable.width, placeable.height) { placeable.place(0, 0) }
                },
            content = content
        )
    }
}

/**
 * Whether the system currently renders cross-window blur. It can change at runtime, e.g. when
 * battery saver or tunnelled video playback disables it.
 */
@Composable
private fun rememberCrossWindowBlurEnabled(): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return false
    val windowManager = LocalContext.current.getSystemService(WindowManager::class.java)
        ?: return false
    var enabled by remember(windowManager) { mutableStateOf(windowManager.isCrossWindowBlurEnabled) }
    DisposableEffect(windowManager) {
        val listener = Consumer<Boolean> { enabled = it }
        windowManager.addCrossWindowBlurEnabledListener(listener)
        onDispose { windowManager.removeCrossWindowBlurEnabledListener(listener) }
    }
    return enabled
}

private fun Window.setBlurBehindRadius(radius: Int) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return
    if (attributes.blurBehindRadius == radius) return
    attributes = attributes.apply { blurBehindRadius = radius }
}

private fun View.findDialogWindowProvider(): DialogWindowProvider? {
    if (this is DialogWindowProvider) return this
    var parent: ViewParent? = parent
    while (parent != null) {
        if (parent is DialogWindowProvider) return parent
        parent = parent.parent
    }
    return null
}
