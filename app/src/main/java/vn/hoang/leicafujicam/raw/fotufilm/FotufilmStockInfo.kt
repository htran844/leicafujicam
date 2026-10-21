package vn.hoang.leicafujicam.raw.fotufilm

import org.json.JSONArray

/** What a film picker shows for one registered Fotufilm stock. */
data class FotufilmStockInfo(
    val id: String,
    val name: String,
    val subtitle: String?,
    val isMonochrome: Boolean,
    val isReversal: Boolean,
    /** An integral print (instant film) rather than a transparency. */
    val isReflectionPrint: Boolean,
    val isExample: Boolean,
    /** The gauge the film comes in (`FilmFormat.nativeID`). */
    val nativeFormat: String,
    /** The medium an edit takes when it chooses none. */
    val defaultPaper: String,
    /** The media the film can be finished on (`PrintPaper.choices(for:)`). */
    val papers: List<String>,
) {
    companion object {
        fun parseList(json: String): List<FotufilmStockInfo> {
            val array = JSONArray(json)
            return (0 until array.length()).map { index ->
                val item = array.getJSONObject(index)
                val papers = item.getJSONArray("papers")
                FotufilmStockInfo(
                    id = item.getString("id"),
                    name = item.getString("name"),
                    subtitle = if (item.isNull("subtitle")) null else item.getString("subtitle"),
                    isMonochrome = item.getBoolean("isMonochrome"),
                    isReversal = item.getBoolean("isReversal"),
                    isReflectionPrint = item.getBoolean("isReflectionPrint"),
                    isExample = item.getBoolean("isExample"),
                    nativeFormat = item.getString("nativeFormat"),
                    defaultPaper = item.getString("defaultPaper"),
                    papers = (0 until papers.length()).map { papers.getString(it) },
                )
            }
        }
    }
}
