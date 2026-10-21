package vn.hoang.leicafujicam.utils

import android.text.TextUtils

object SystemPropertiesUtil {

    // 获取属性，如果不存在返回默认值
    fun get(key: String): String? {
        val value = try {
            val clazz = Class.forName("android.os.SystemProperties")
            val getMethod = clazz.getMethod("get", String::class.java, String::class.java)
            getMethod.invoke(clazz, key, null) as? String
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
        if (TextUtils.isEmpty(value)) return null
        return value
    }
}