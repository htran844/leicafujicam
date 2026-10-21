package vn.hoang.leicafujicam.raw.fotufilm

import org.json.JSONArray
import org.json.JSONObject

/**
 * The Android surface's Fotufilm controls for one film under one document, as the native
 * `fotufilmCatalogueJSON` reports them (upstream `EditorControlCatalogue` resolved for the stock
 * and the current output medium). Titles and labels are upstream's English; a UI localises them
 * by [Control.field], [Control.section] and [Choice.id].
 */
data class FotufilmCatalogue(
    val stockId: String,
    /** The gauge the document develops on (its choice, else the film's own). */
    val formatId: String,
    /** The medium the document resolves to for this film. */
    val paperId: String,
    /** The Screen Conversion style the medium gates read. */
    val digitalReference: String,
    /** Why the native options mapping would reject the document, or null when it accepts it. */
    val documentError: String?,
    val controls: List<Control>,
) {
    data class Scale(
        val min: Double,
        val max: Double,
        val neutral: Double,
        /** `EditorControlUnit` raw value: multiplier, stops, stopsFromOff, signed, percent, years, … */
        val unit: String,
        /** Detents on the track; empty for a continuous one. */
        val stops: List<Double>,
        val admittedMin: Double,
        val admittedMax: Double,
    )

    data class Chip(val value: Double, val label: String)

    data class Choice(val id: String, val label: String, val detail: String, val value: Double?)

    sealed class Kind {
        data class Slider(val scale: Scale) : Kind()
        data class Chips(val scale: Scale, val chips: List<Chip>) : Kind()
        data class Toggle(val restingOn: Boolean) : Kind()
        /** [menu] is "fixed" or the upstream dynamic menu name (stocks, gauges, papers, viewingLights, shutterTimes). */
        data class Menu(val menu: String, val choices: List<Choice>, val defaultChoice: String) : Kind()
        object Other : Kind()
    }

    data class Control(
        val field: String,
        val title: String,
        val detail: String,
        val section: String,
        val sectionTitle: String,
        val group: String,
        val kind: Kind,
        val availability: String,
        val foldsUnder: String?,
        /** key, bespoke or none. */
        val persistence: String,
        val storedKey: String?,
        val storedEncoding: String?,
        /** The document's value, else the resting value or default choice. */
        val value: FotufilmDocument.Value?,
        /** Whether the document carries the field. */
        val isSet: Boolean,
        val moved: Boolean,
        /** The film admits the control (`EditorControlAvailability.admits`). */
        val admitted: Boolean,
        /** The current output medium offers it (`EditorControlCatalogue.medium(_:offers:)`). */
        val offered: Boolean,
        val available: Boolean,
    )

    fun control(field: String): Control? = controls.firstOrNull { it.field == field }

    companion object {
        fun parse(json: String): FotufilmCatalogue {
            val root = JSONObject(json)
            val controls = root.getJSONArray("controls").objects().map(::parseControl)
            return FotufilmCatalogue(
                stockId = root.getString("stock"),
                formatId = root.getString("format"),
                paperId = root.getString("paper"),
                digitalReference = root.getString("digitalReference"),
                documentError = root.optStringOrNull("documentError"),
                controls = controls,
            )
        }

        private fun parseControl(json: JSONObject): Control {
            val kind = when (val name = json.getString("kind")) {
                "slider" -> Kind.Slider(parseScale(json.getJSONObject("scale")))
                "chips" -> Kind.Chips(
                    parseScale(json.getJSONObject("scale")),
                    json.getJSONArray("chips").objects().map { Chip(it.getDouble("value"), it.getString("label")) },
                )
                "toggle" -> Kind.Toggle(json.getBoolean("restingOn"))
                "menu" -> Kind.Menu(
                    menu = json.getString("menu"),
                    choices = json.getJSONArray("choices").objects().map {
                        Choice(
                            id = it.getString("id"),
                            label = it.getString("label"),
                            detail = it.getString("detail"),
                            value = if (it.isNull("value")) null else it.getDouble("value"),
                        )
                    },
                    defaultChoice = json.getString("defaultChoice"),
                )
                else -> {
                    check(name == "takeover" || name == "curve") { "Unknown Fotufilm control kind $name" }
                    Kind.Other
                }
            }
            return Control(
                field = json.getString("field"),
                title = json.getString("title"),
                detail = json.getString("detail"),
                section = json.getString("section"),
                sectionTitle = json.getString("sectionTitle"),
                group = json.getString("group"),
                kind = kind,
                availability = json.getString("availability"),
                foldsUnder = json.optStringOrNull("foldsUnder"),
                persistence = json.getString("persistence"),
                storedKey = json.optStringOrNull("storedKey"),
                storedEncoding = json.optStringOrNull("storedEncoding"),
                value = if (json.isNull("value")) null else parseValue(json.get("value")),
                isSet = json.getBoolean("isSet"),
                moved = json.optBoolean("moved", false),
                admitted = json.getBoolean("admitted"),
                offered = json.getBoolean("offered"),
                available = json.getBoolean("available"),
            )
        }

        private fun parseScale(json: JSONObject): Scale = Scale(
            min = json.getDouble("min"),
            max = json.getDouble("max"),
            neutral = json.getDouble("neutral"),
            unit = json.getString("unit"),
            stops = json.getJSONArray("stops").let { array -> (0 until array.length()).map { array.getDouble(it) } },
            admittedMin = json.getDouble("admittedMin"),
            admittedMax = json.getDouble("admittedMax"),
        )

        private fun parseValue(raw: Any): FotufilmDocument.Value = when (raw) {
            is Boolean -> FotufilmDocument.Value.Flag(raw)
            is Number -> FotufilmDocument.Value.Number(raw.toDouble())
            is String -> FotufilmDocument.Value.Choice(raw)
            is JSONArray -> {
                val items = (0 until raw.length()).map { raw.get(it) }
                if (items.all { it is String }) {
                    FotufilmDocument.Value.Choices(items.map { it as String })
                } else {
                    FotufilmDocument.Value.Curve(items.map { (it as Number).toDouble() })
                }
            }
            else -> throw IllegalArgumentException("Invalid Fotufilm catalogue value")
        }

        private fun JSONArray.objects(): List<JSONObject> = (0 until length()).map { getJSONObject(it) }

        private fun JSONObject.optStringOrNull(name: String): String? =
            if (!has(name) || isNull(name)) null else getString(name)
    }
}
