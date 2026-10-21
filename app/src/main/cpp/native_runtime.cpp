#include <android/log.h>
#include <jni.h>
#include <omp.h>

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *, void *) {
  // Configure libomp before any JNI entry point can start a parallel region.
  // Android controls the CPUs available to app threads; libomp must not restore
  // a cached affinity mask when creating/reusing a team. A rejected affinity
  // syscall is fatal in libomp, even with its default KMP_AFFINITY=none policy.
  // "disabled" turns off those interfaces while retaining parallel execution.
  kmp_set_defaults("KMP_AFFINITY=disabled");
  __android_log_print(ANDROID_LOG_INFO, "PLog_NativeRuntime",
                      "OpenMP initialized: affinity=disabled, scheduling=system");
  return JNI_VERSION_1_6;
}
