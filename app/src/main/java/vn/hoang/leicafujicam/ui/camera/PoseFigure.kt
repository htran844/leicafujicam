package vn.hoang.leicafujicam.ui.camera

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import vn.hoang.leicafujicam.pose.PoseCatalog
import vn.hoang.leicafujicam.pose.PoseEntry
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/**
 * Draws a pose as a stick figure (bones + head circle) fitted into the
 * canvas bounds. Joint coordinates are normalized; [flipped] mirrors the
 * figure horizontally. Shared by the pose browser grid and the camera
 * overlay.
 */
@Composable
fun PoseFigureCanvas(
    pose: PoseEntry,
    color: Color,
    modifier: Modifier = Modifier,
    strokeWidthPx: Float = 5f,
    flipped: Boolean = false
) {
    Canvas(modifier) {
        val joints = pose.joints
        if (joints.isEmpty()) return@Canvas
        val xs = joints.values.map { it.x }
        val ys = joints.values.map { it.y }
        val minX = xs.min()
        val maxX = xs.max()
        val minY = ys.min()
        val maxY = ys.max()
        val spanX = max(maxX - minX, 0.05f)
        val spanY = max(maxY - minY, 0.05f)
        val pad = size.minDimension * 0.08f
        val scale = min(
            (size.width - 2 * pad) / spanX,
            (size.height - 2 * pad) / spanY
        )
        val drawnW = spanX * scale
        val drawnH = spanY * scale
        val originX = (size.width - drawnW) / 2f
        val originY = (size.height - drawnH) / 2f

        fun map(point: Offset): Offset {
            val nx = if (flipped) (maxX - (point.x - minX)) + minX else point.x
            return Offset(
                originX + (nx - minX) * scale,
                originY + (point.y - minY) * scale
            )
        }

        for ((a, b) in PoseCatalog.BONES) {
            val pa = joints[a] ?: continue
            val pb = joints[b] ?: continue
            drawLine(
                color = color,
                start = map(pa),
                end = map(pb),
                strokeWidth = strokeWidthPx,
                cap = StrokeCap.Round
            )
        }
        // Neck line + head circle around the nose joint.
        val nose = joints["nose"]
        val lShoulder = joints["l_shoulder"]
        val rShoulder = joints["r_shoulder"]
        if (nose != null && lShoulder != null && rShoulder != null) {
            val neck = Offset(
                (lShoulder.x + rShoulder.x) / 2f,
                (lShoulder.y + rShoulder.y) / 2f
            )
            drawLine(
                color = color,
                start = map(nose),
                end = map(neck),
                strokeWidth = strokeWidthPx,
                cap = StrokeCap.Round
            )
            val headRadius = max(
                hypot((nose.x - neck.x).toDouble(), (nose.y - neck.y).toDouble()).toFloat() * scale * 1.25f,
                strokeWidthPx * 1.6f
            )
            drawCircle(
                color = color,
                radius = headRadius,
                center = map(nose),
                style = androidx.compose.ui.graphics.drawscope.Stroke(width = strokeWidthPx)
            )
        }
        for ((_, point) in joints) {
            drawCircle(
                color = color,
                radius = strokeWidthPx * 0.62f,
                center = map(point)
            )
        }
    }
}
