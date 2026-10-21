package vn.hoang.leicafujicam.lut

object LutProcessor {
    init {
        try {
            System.loadLibrary("my-native-lib")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    external fun resampleLutNative(srcData: ShortArray, size: Int, curveType: Int): ShortArray?
    external fun resampleSizeNative(srcData: ShortArray, srcSize: Int, targetSize: Int): ShortArray?
}
