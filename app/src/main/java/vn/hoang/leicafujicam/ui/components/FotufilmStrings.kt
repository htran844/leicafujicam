package vn.hoang.leicafujicam.ui.components

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.raw.fotufilm.FotufilmCatalogue
import java.util.Locale
import kotlin.math.roundToInt

/**
 * Localised text for the Fotufilm catalogue. Upstream reports English titles and labels; every
 * control and fixed choice the panel shows has a translation. Proper names — films, print papers
 * and print films — are the makers' own and stay as upstream states them.
 */
internal object FotufilmStrings {
    @Composable
    fun section(section: String): String =
        FotufilmStringRes.section(section)?.let { stringResource(it) } ?: section

    @Composable
    fun title(field: String): String =
        FotufilmStringRes.title(field)?.let { stringResource(it) } ?: field

    @Composable
    fun description(field: String): String? =
        FotufilmStringRes.description(field)?.let { stringResource(it) }

    @Composable
    fun choice(field: String, menu: String, choice: FotufilmCatalogue.Choice): String {
        // The viewing-light menu reuses its ids (reference, d65) for different lamps on different
        // media; its label names the lamp.
        val identity = if (menu == VIEWING_LIGHTS_MENU) choice.label else choice.id
        FotufilmStringRes.choice(choiceKey(field, identity))?.let { return stringResource(it) }
        if (menu == SHUTTER_MENU) {
            choice.id.toIntOrNull()?.let { seconds -> return shutterTime(LocalContext.current, seconds) }
        }
        return choice.label
    }

    @Composable
    fun chip(field: String, chip: FotufilmCatalogue.Chip): String =
        FotufilmStringRes.choice(choiceKey(field, number(chip.value)))?.let { stringResource(it) } ?: chip.label

    /** A slider value in the unit its scale declares. */
    fun formatValue(context: Context, scale: FotufilmCatalogue.Scale, value: Float): String = when (scale.unit) {
        "multiplier" -> String.format(Locale.getDefault(), "×%.2f", value)
        "percent" -> "${(value * 100f).roundToInt()}%"
        "stops" -> String.format(Locale.getDefault(), "%+.1f EV", value)
        "stopsFromOff" -> if (value <= scale.min + OFF_TOLERANCE) {
            context.getString(R.string.fotufilm_value_off)
        } else {
            String.format(Locale.getDefault(), "%+.1f EV", value)
        }
        "signed" -> String.format(Locale.getDefault(), "%+.2f", value)
        "years" -> context.getString(R.string.fotufilm_value_years, number(value.toDouble(), 1))
        "micrometers" -> "${value.roundToInt()} µm"
        "printerPoints" -> value.roundToInt().toString()
        else -> String.format(Locale.getDefault(), "%.2f", value)
    }

    private fun shutterTime(context: Context, seconds: Int): String =
        if (seconds >= 60 && seconds % 60 == 0) {
            context.getString(R.string.fotufilm_value_minutes, (seconds / 60).toString())
        } else {
            context.getString(R.string.fotufilm_value_seconds, seconds.toString())
        }

    private fun choiceKey(field: String, id: String): String =
        "${field}_${id.replace(Regex("[^A-Za-z0-9]"), "_")}"

    /** A number as a choice id or a value label: no trailing zeros, at most [places] decimals. */
    private fun number(value: Double, places: Int = 3): String =
        java.math.BigDecimal(value).setScale(places, java.math.RoundingMode.HALF_EVEN)
            .stripTrailingZeros().toPlainString()

    private const val SHUTTER_MENU = "shutterTimes"
    private const val VIEWING_LIGHTS_MENU = "viewingLights"
    private const val OFF_TOLERANCE = 1e-4f
}
