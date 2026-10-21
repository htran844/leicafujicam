package vn.hoang.leicafujicam.frame

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import androidx.core.graphics.createBitmap
import vn.hoang.leicafujicam.utils.GaussianBlur
import kotlin.math.roundToInt

/** Photo materials shared by preview and export, including Android 11 bitmap canvases. */
internal object FrameBackgroundRenderer {
    private const val MAX_WORKING_SIZE = 512

    fun draw(
        context: Context,
        canvas: Canvas,
        photo: Bitmap,
        layout: FrameLayout,
        width: Int,
        height: Int,
        sigma: Float,
        materialBounds: RectF,
        unitScale: Float,
    ) {
        if (layout.effectiveBackgroundType == FrameBackgroundType.LIQUID_GLASS) {
            FrameLiquidGlass.draw(context, canvas, photo, layout, width, height, materialBounds, unitScale)
            return
        }
        // Bound Gaussian convolution cost independently of the full-resolution export size.
        val workingScale = minOf(1f, MAX_WORKING_SIZE.toFloat() / maxOf(width, height))
        val workingWidth = (width * workingScale).roundToInt().coerceAtLeast(1)
        val workingHeight = (height * workingScale).roundToInt().coerceAtLeast(1)
        val background = createBitmap(workingWidth, workingHeight)
        try {
            val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
            val workingCanvas = Canvas(background)
            // Flatten transparent source pixels before filtering RGB.
            workingCanvas.drawColor(Color.BLACK)
            workingCanvas.scale(workingWidth.toFloat() / width, workingHeight.toFloat() / height)
            workingCanvas.translate(width / 2f, height / 2f)
            val fillScale = maxOf(width.toFloat() / photo.width, height.toFloat() / photo.height)
            workingCanvas.scale(fillScale, fillScale)
            workingCanvas.drawBitmap(
                photo, null,
                RectF(-photo.width / 2f, -photo.height / 2f, photo.width / 2f, photo.height / 2f),
                paint
            )

            if (sigma > 0f) {
                GaussianBlur.blur(background, sigma * workingWidth / width, sigma * workingHeight / height)
            }
            canvas.drawBitmap(background, null, RectF(0f, 0f, width.toFloat(), height.toFloat()), paint)
        } finally {
            background.recycle()
        }
    }
}
