package vn.hoang.leicafujicam.raw

object DcpNativeBridge {
    init {
        System.loadLibrary("my-native-lib")
    }

    external fun parseDcpToJson(filePath: String): String
}
