package vn.hoang.leicafujicam.frame

import android.graphics.Canvas
import android.graphics.Bitmap
import android.graphics.BlurMaskFilter
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import androidx.core.graphics.withSave
import kotlin.math.ceil

/** A single layout supplies both measurement and drawing, including explicit and automatic wraps. */
internal class FrameTextLayout(
    text: String,
    private val element: FrameElement.Text,
    typeface: Typeface,
    private val dimensions: FrameDimensions,
    width: Float? = null,
) {
    private val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
        textSize = dimensions.toPixels(element.fontSizePx)
        this.typeface = if (element.style.italic) {
            Typeface.create(typeface, typeface.style or Typeface.ITALIC)
        } else typeface
        letterSpacing = element.style.letterSpacingEm
        color = element.color
    }
    private val layout = StaticLayout.Builder.obtain(
        text, 0, text.length, paint,
        ceil((width ?: Layout.getDesiredWidth(text, paint)).toDouble()).toInt().coerceAtLeast(1),
    ).setAlignment(
        when (element.alignment) {
            ElementAlignment.START -> Layout.Alignment.ALIGN_NORMAL
            ElementAlignment.CENTER -> Layout.Alignment.ALIGN_CENTER
            ElementAlignment.END -> Layout.Alignment.ALIGN_OPPOSITE
        }
    ).setIncludePad(true)
        .setLineSpacing(0f, element.style.lineSpacingMultiplier)
        .setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE)
        .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
        .build()

    val width: Float get() = layout.width.toFloat()
    val height: Float get() = layout.height.toFloat()

    fun draw(canvas: Canvas, x: Float, y: Float) = canvas.withSave {
        translate(x, y)
        val style = element.style
        val shadowRadius = dimensions.toPixels(style.shadowRadiusPx)
        if (shadowRadius > 0f) {
            drawShadow(this, shadowRadius)
        }
        if (style.strokeWidthPx > 0f) {
            paint.style = Paint.Style.STROKE
            paint.strokeJoin = Paint.Join.ROUND
            paint.strokeWidth = dimensions.toPixels(style.strokeWidthPx)
            paint.color = style.strokeColor
            layout.draw(this)
        }
        paint.style = Paint.Style.FILL
        paint.color = element.color
        style.gradientEndColor?.let { endColor ->
            // Shader carries the endpoint alpha; do not multiply it by the first color's alpha.
            paint.color = Color.WHITE
            paint.shader = LinearGradient(
                0f, 0f, 0f, this@FrameTextLayout.height.coerceAtLeast(1f),
                element.color, endColor, Shader.TileMode.CLAMP,
            )
        }
        layout.draw(this)
        paint.shader = null
    }

    private fun drawShadow(canvas: Canvas, radius: Float) {
        val style = element.style
        val stroke = dimensions.toPixels(style.strokeWidthPx)
        // Include overhanging glyphs and the complete outline in the shadow silhouette.
        val padding = ceil((paint.textSize + stroke).toDouble()).toInt()
        val silhouette = Bitmap.createBitmap(
            layout.width + padding * 2, layout.height.coerceAtLeast(1) + padding * 2,
            Bitmap.Config.ALPHA_8,
        )
        try {
            paint.color = Color.WHITE
            paint.style = if (stroke > 0f) Paint.Style.FILL_AND_STROKE else Paint.Style.FILL
            paint.strokeJoin = Paint.Join.ROUND
            paint.strokeWidth = stroke
            Canvas(silhouette).withSave {
                translate(padding.toFloat(), padding.toFloat())
                layout.draw(this)
            }
            val offsets = IntArray(2)
            val blurPaint = Paint().apply { maskFilter = BlurMaskFilter(radius, BlurMaskFilter.Blur.NORMAL) }
            val shadow = silhouette.extractAlpha(blurPaint, offsets)
            try {
                canvas.drawBitmap(
                    shadow,
                    offsets[0] - padding + dimensions.toPixels(style.shadowOffsetXPx),
                    offsets[1] - padding + dimensions.toPixels(style.shadowOffsetYPx),
                    Paint(Paint.ANTI_ALIAS_FLAG).apply { color = style.shadowColor },
                )
            } finally {
                shadow.recycle()
            }
        } finally {
            silhouette.recycle()
        }
    }
}
