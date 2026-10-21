#include "raw10_unpack.h"

#include <android/hardware_buffer_jni.h>
#include <android/log.h>
#include <jni.h>

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hinnka_mycamera_raw_Raw10Unpacker_validateHardwareBufferNative(
    JNIEnv *env, jobject, jobject hardwareBuffer, jint width, jint height) {
  if (!hardwareBuffer) return JNI_FALSE;
  AHardwareBuffer *buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
  if (!buffer) return JNI_FALSE;
  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(buffer, &desc);

  // RAW10 has no named NDK AHardwareBuffer format constant. Its HAL format
  // (and ImageFormat.RAW10 value) is 0x25. For this packed format, framework
  // getLockedImageInfo interprets the GraphicBuffer stride as bytes and aborts
  // if it is smaller than width * 10 / 8. Inspect the same descriptor without
  // locking pixels or calling Image.getPlanes(), which would hit that abort.
  constexpr uint32_t kRaw10Format = 0x25;
  const uint64_t packedRowBytes = uint64_t(desc.width) * 10 / 8;
  const bool valid = width > 0 && height > 0 && desc.layers == 1 &&
      desc.format == kRaw10Format && desc.width == uint32_t(width) &&
      desc.height == uint32_t(height) && desc.width % 4 == 0 &&
      desc.height % 2 == 0 && desc.stride >= packedRowBytes;
  if (!valid) {
    __android_log_print(ANDROID_LOG_ERROR, "PLog_Raw10Unpacker",
        "Rejecting RAW10 buffer before getPlanes: image=%dx%d buffer=%ux%u "
        "format=0x%x layers=%u stride=%u minRowBytes=%llu usage=0x%llx",
        width, height, desc.width, desc.height, desc.format, desc.layers,
        desc.stride, static_cast<unsigned long long>(packedRowBytes),
        static_cast<unsigned long long>(desc.usage));
  }
  return valid ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hinnka_mycamera_raw_Raw10Unpacker_unpackNative(
    JNIEnv *env, jobject, jobject sourceBuffer, jint sourceLimit,
    jobject destinationBuffer, jint width, jint height, jint rowStride) {
  if (!sourceBuffer || !destinationBuffer || width <= 0 || width % 4 != 0 ||
      height <= 0 || height % 2 != 0 || rowStride <= 0 || sourceLimit < 0) {
    __android_log_print(ANDROID_LOG_ERROR, "Raw10Unpacker",
                        "Invalid RAW10 geometry: %dx%d stride=%d limit=%d",
                        width, height, rowStride, sourceLimit);
    return JNI_FALSE;
  }

  const int64_t packedRowBytes = static_cast<int64_t>(width) / 4 * 5;
  const int64_t sourceBytes = static_cast<int64_t>(height - 1) * rowStride + packedRowBytes;
  const int64_t destinationBytes = static_cast<int64_t>(width) * height * sizeof(uint16_t);
  const auto *source = static_cast<const uint8_t *>(env->GetDirectBufferAddress(sourceBuffer));
  auto *destination = static_cast<uint16_t *>(env->GetDirectBufferAddress(destinationBuffer));
  const jlong sourceCapacity = env->GetDirectBufferCapacity(sourceBuffer);
  const jlong destinationCapacity = env->GetDirectBufferCapacity(destinationBuffer);
  if (!source || !destination || rowStride < packedRowBytes ||
      sourceLimit > sourceCapacity || sourceBytes > sourceLimit ||
      destinationCapacity < destinationBytes ||
      reinterpret_cast<uintptr_t>(destination) % alignof(uint16_t) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, "Raw10Unpacker",
                        "Invalid RAW10 buffers: %dx%d stride=%d limit=%d srcCapacity=%lld dstCapacity=%lld",
                        width, height, rowStride, sourceLimit,
                        static_cast<long long>(sourceCapacity), static_cast<long long>(destinationCapacity));
    return JNI_FALSE;
  }

  photon::UnpackRaw10(source, destination, static_cast<size_t>(width),
                     static_cast<size_t>(height), static_cast<size_t>(rowStride));
  return JNI_TRUE;
}
