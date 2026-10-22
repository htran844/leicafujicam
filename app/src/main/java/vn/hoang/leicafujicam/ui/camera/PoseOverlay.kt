package vn.hoang.leicafujicam.ui.camera

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowLeft
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.pose.PoseEntry
import vn.hoang.leicafujicam.ui.icons.AppIcons
import vn.hoang.leicafujicam.ui.theme.PremiumGold
import vn.hoang.leicafujicam.viewmodel.PoseGuideState

/**
 * Live pose-guide overlay on the camera preview: the selected pose as a
 * stick figure the subject can align with. The figure can be dragged,
 * resized, faded and mirrored; a compact panel cycles poses.
 */
@Composable
fun PoseGuideOverlay(
    pose: PoseEntry,
    state: PoseGuideState,
    aspectRatio: Float,
    onUpdate: ((PoseGuideState) -> PoseGuideState) -> Unit,
    onCycle: (Int) -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier
) {
    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        val totalW = maxWidth
        val totalH = maxHeight
        val ar = if (aspectRatio.isFinite() && aspectRatio > 0f) aspectRatio else 3f / 4f
        val previewW = if (totalW <= totalH * ar) totalW else totalH * ar
        val previewH = if (totalH <= totalW / ar) totalH else totalW / ar
        val previewLeft = (totalW - previewW) / 2
        val previewTop = (totalH - previewH) / 2

        val figureH = previewH * state.scale
        val figureW = figureH * 0.58f
        val centerX = previewLeft + previewW * state.centerX
        val centerY = previewTop + previewH * state.centerY

        Box(
            modifier = Modifier
                .offset(x = centerX - figureW / 2, y = centerY - figureH / 2)
                .width(figureW)
                .height(figureH)
                .pointerInput(pose.id) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        onUpdate { current ->
                            current.copy(
                                centerX = (current.centerX + dragAmount.x / size.width.coerceAtLeast(1) *
                                    (figureW / previewW)).coerceIn(0.05f, 0.95f),
                                centerY = (current.centerY + dragAmount.y / size.height.coerceAtLeast(1) *
                                    (figureH / previewH)).coerceIn(0.05f, 0.95f)
                            )
                        }
                    }
                }
        ) {
            PoseFigureCanvas(
                pose = pose,
                color = Color.White,
                modifier = Modifier
                    .fillMaxSize()
                    .alpha(state.opacity),
                strokeWidthPx = 7f,
                flipped = state.flipped
            )
        }

        // Control panel.
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 20.dp)
                .background(Color.Black.copy(alpha = 0.55f), RoundedCornerShape(16.dp))
                .padding(horizontal = 10.dp, vertical = 6.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                IconButton(onClick = { onCycle(-1) }) {
                    Icon(
                        Icons.Filled.KeyboardArrowLeft,
                        contentDescription = stringResource(R.string.pose_guide_prev),
                        tint = Color.White
                    )
                }
                Text(
                    text = pose.getName(),
                    color = PremiumGold,
                    fontSize = 13.sp,
                    maxLines = 1
                )
                IconButton(onClick = { onCycle(1) }) {
                    Icon(
                        Icons.Filled.KeyboardArrowRight,
                        contentDescription = stringResource(R.string.pose_guide_next),
                        tint = Color.White
                    )
                }
                IconButton(onClick = {
                    onUpdate { it.copy(flipped = !it.flipped) }
                }) {
                    Icon(
                        AppIcons.SwapHoriz,
                        contentDescription = stringResource(R.string.pose_guide_flip),
                        tint = Color.White
                    )
                }
                IconButton(onClick = onClose) {
                    Icon(
                        Icons.Filled.Close,
                        contentDescription = stringResource(R.string.pose_guide_close),
                        tint = Color.White
                    )
                }
            }
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = stringResource(R.string.pose_guide_opacity),
                    color = Color.White,
                    fontSize = 11.sp
                )
                Slider(
                    value = state.opacity,
                    onValueChange = { value -> onUpdate { it.copy(opacity = value) } },
                    valueRange = 0.25f..1f,
                    modifier = Modifier.width(96.dp),
                    colors = SliderDefaults.colors(
                        thumbColor = PremiumGold,
                        activeTrackColor = PremiumGold
                    )
                )
                Text(
                    text = stringResource(R.string.pose_guide_size),
                    color = Color.White,
                    fontSize = 11.sp
                )
                Slider(
                    value = state.scale,
                    onValueChange = { value -> onUpdate { it.copy(scale = value) } },
                    valueRange = 0.3f..0.95f,
                    modifier = Modifier.width(96.dp),
                    colors = SliderDefaults.colors(
                        thumbColor = PremiumGold,
                        activeTrackColor = PremiumGold
                    )
                )
            }
        }
    }
}
