package vn.hoang.leicafujicam.raw.fotufilm

import vn.hoang.leicafujicam.utils.PLog

/**
 * Persistence boundary for [FotufilmDocument]: preferences, presets, photo metadata and the
 * gallery database all store the document as its [FotufilmDocument.toJson] string.
 */
object FotufilmDocumentStorage {
    private const val TAG = "FotufilmDocumentStorage"

    /** The document an engine switch to Fotufilm starts from when nothing is stored. */
    const val DEFAULT_STOCK_ID = "portra400"

    fun defaultDocument(): FotufilmDocument = FotufilmDocument(stockId = DEFAULT_STOCK_ID)

    /**
     * Decodes a stored document. A blank or absent value is unset; an unreadable value is logged
     * with its [source] and also treated as unset.
     */
    fun decode(json: String?, source: String): FotufilmDocument? {
        if (json.isNullOrBlank()) return null
        return try {
            FotufilmDocument.fromJson(json)
        } catch (e: Exception) {
            PLog.e(TAG, "Unreadable Fotufilm document from $source: $json", e)
            null
        }
    }

    fun encode(document: FotufilmDocument?): String? = document?.toJson()
}
