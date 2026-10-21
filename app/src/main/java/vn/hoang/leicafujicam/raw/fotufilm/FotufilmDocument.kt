package vn.hoang.leicafujicam.raw.fotufilm

import org.json.JSONArray
import org.json.JSONObject

/**
 * One photograph's Fotufilm edit: the film and each control by its upstream field id
 * (`EditorControlField` raw value) in the units the catalogue declares, absent where it rests.
 *
 * The JSON form is upstream `EditDocument`'s Codable encoding with the film under `"stock"`:
 * `{"stock": "portra400", "exposure": 0.8, "paper": "screen", "grainModel": "film"}`. Values are
 * a number, a flag, a choice id, a list of choice ids, or a list of numbers (a curve).
 */
class FotufilmDocument(
    val stockId: String,
    val values: Map<String, Value> = emptyMap(),
) {
    sealed class Value {
        data class Number(val value: Double) : Value()
        data class Flag(val value: Boolean) : Value()
        data class Choice(val id: String) : Value()
        data class Choices(val ids: List<String>) : Value()
        data class Curve(val points: List<Double>) : Value()
    }

    operator fun get(field: String): Value? = values[field]

    fun number(field: String): Double? = (values[field] as? Value.Number)?.value
    fun flag(field: String): Boolean? = (values[field] as? Value.Flag)?.value
    fun choice(field: String): String? = (values[field] as? Value.Choice)?.id

    fun with(field: String, value: Value): FotufilmDocument {
        require(field != STOCK_FIELD) { "Change the film with withStock" }
        return FotufilmDocument(stockId, values + (field to value))
    }

    fun without(field: String): FotufilmDocument = FotufilmDocument(stockId, values - field)

    fun withStock(id: String): FotufilmDocument = FotufilmDocument(id, values)

    fun toJson(): String {
        val json = JSONObject()
        json.put(STOCK_FIELD, stockId)
        for ((field, value) in values.toSortedMap()) {
            json.put(
                field,
                when (value) {
                    is Value.Number -> {
                        require(value.value.isFinite()) { "Fotufilm $field is not finite" }
                        value.value
                    }
                    is Value.Flag -> value.value
                    is Value.Choice -> value.id
                    is Value.Choices -> JSONArray(value.ids)
                    is Value.Curve -> JSONArray().apply { value.points.forEach { put(it) } }
                },
            )
        }
        return json.toString()
    }

    override fun equals(other: Any?): Boolean =
        other is FotufilmDocument && stockId == other.stockId && values == other.values

    override fun hashCode(): Int = stockId.hashCode() * 31 + values.hashCode()

    override fun toString(): String = toJson()

    companion object {
        const val STOCK_FIELD = "stock"

        /**
         * Reads [toJson]'s form, decoding each value as upstream `EditDocument.Value` does: a flag,
         * a number, a string, a list of strings, else a list of numbers. [fallbackStockId] names
         * the film when the JSON carries none.
         */
        fun fromJson(json: String, fallbackStockId: String? = null): FotufilmDocument {
            val objectValue = JSONObject(json)
            val stock = objectValue.optString(STOCK_FIELD).takeIf { objectValue.has(STOCK_FIELD) && it.isNotEmpty() }
                ?: fallbackStockId
                ?: throw IllegalArgumentException("Fotufilm document names no film")
            val values = LinkedHashMap<String, Value>()
            val keys = objectValue.keys()
            while (keys.hasNext()) {
                val field = keys.next()
                if (field == STOCK_FIELD) continue
                values[field] = decodeValue(field, objectValue.get(field))
            }
            return FotufilmDocument(stock, values)
        }

        private fun decodeValue(field: String, raw: Any): Value = when (raw) {
            is Boolean -> Value.Flag(raw)
            is Number -> {
                val number = raw.toDouble()
                require(number.isFinite()) { "Fotufilm $field is not finite" }
                Value.Number(number)
            }
            is String -> Value.Choice(raw)
            is JSONArray -> {
                val items = (0 until raw.length()).map { raw.get(it) }
                if (items.all { it is String }) {
                    Value.Choices(items.map { it as String })
                } else {
                    Value.Curve(items.map {
                        require(it is Number) { "Fotufilm $field holds a value that is neither choice nor number" }
                        it.toDouble()
                    })
                }
            }
            else -> throw IllegalArgumentException("Invalid Fotufilm value for $field")
        }
    }
}
