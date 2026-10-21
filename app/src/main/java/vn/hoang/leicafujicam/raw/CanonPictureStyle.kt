package vn.hoang.leicafujicam.raw

/** Built-in Canon color/tone styles; persisted IDs are independent of display labels. */
enum class CanonPictureStyle(val persistedValue: String, val dppStyleId: Int) {
    Standard("standard", 0x81),
    Portrait("portrait", 0x82),
    Landscape("landscape", 0x83),
    Neutral("neutral", 0x84),
    Faithful("faithful", 0x85),
    Monochrome("monochrome", 0x86);

    companion object {
        fun fromPersistedValue(value: String?): CanonPictureStyle =
            entries.firstOrNull {
                it.persistedValue.equals(value, ignoreCase = true) ||
                    it.name.equals(value, ignoreCase = true)
            } ?: Standard
    }
}
