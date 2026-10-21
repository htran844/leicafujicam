package vn.hoang.leicafujicam.frame

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF

/** Per-render editor decoration. Never stored in a template or used by photo export. */
class FramePreviewSelection(private val element: FrameElement, private val color: Int) {
    private val outlines = mutableListOf<Path>()

    internal fun matches(candidate: FrameElement): Boolean = candidate === element

    @Suppress("DEPRECATION") // Software bitmap Canvas; its matrix is the exact element transform.
    internal fun record(canvas: Canvas, candidate: FrameElement, bounds: RectF) {
        if (!matches(candidate) || bounds.isEmpty) return
        outlines += Path().apply {
            addRect(bounds, Path.Direction.CW)
            transform(canvas.matrix)
        }
    }

    internal fun draw(canvas: Canvas) {
        val stroke = (minOf(canvas.width, canvas.height) / 220f).coerceAtLeast(2f)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeJoin = Paint.Join.ROUND }
        for (outline in outlines) {
            paint.style = Paint.Style.FILL
            paint.color = (color and 0x00FFFFFF) or (28 shl 24)
            canvas.drawPath(outline, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = stroke * 2f
            paint.color = Color.WHITE
            canvas.drawPath(outline, paint)
            paint.strokeWidth = stroke
            paint.color = color
            canvas.drawPath(outline, paint)
        }
    }
}
