#include "spectral_generator.h"
#include <jni.h>
#include <android/log.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {
using namespace spektrafilm;
struct RuntimeModel {
    std::string sharedKey, filmKey;
    DataSet shared, filmData;
    std::unique_ptr<Filming> film;
    DensityWire wire{};
};
RuntimeModel model;
std::mutex modelMutex;

std::string string(JNIEnv* env, jstring value) {
    if (!value) throw std::invalid_argument("Missing spectral model key");
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) throw std::runtime_error("Unable to read spectral model key");
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}
DataSet data(JNIEnv* env, jbyteArray input) {
    if (!input) throw std::invalid_argument("Missing spectral model data");
    const jsize size = env->GetArrayLength(input);
    if (size <= 0 || size > 64 * 1024 * 1024) throw std::invalid_argument("Invalid spectral model byte count");
    jbyte* bytes = env->GetByteArrayElements(input, nullptr);
    if (!bytes) throw std::runtime_error("Unable to access spectral model data");
    try {
        auto result = readDataSet(reinterpret_cast<uint8_t*>(bytes), size);
        env->ReleaseByteArrayElements(input, bytes, JNI_ABORT);
        return result;
    } catch (...) {
        env->ReleaseByteArrayElements(input, bytes, JNI_ABORT);
        throw;
    }
}
jfloatArray output(JNIEnv* env, const std::vector<float>& values) {
    jfloatArray array = env->NewFloatArray(static_cast<jsize>(values.size()));
    if (!array) throw std::runtime_error("Unable to allocate spectral lattice");
    if (!values.empty()) env->SetFloatArrayRegion(array, 0, static_cast<jsize>(values.size()), values.data());
    if (env->ExceptionCheck()) throw std::runtime_error("Unable to copy spectral lattice");
    return array;
}
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_hinnka_mycamera_raw_SpektrafilmNativeBridge_generate(
    JNIEnv* env, jobject, jstring sharedKeyValue, jstring filmKeyValue,
    jbyteArray sharedData, jbyteArray filmData, jstring paperNameValue,
    jbyteArray paperData, jboolean positive, jboolean buildFilm, jboolean buildPrint) {
    std::lock_guard<std::mutex> lock(modelMutex);
    try {
        const auto started = std::chrono::steady_clock::now();
        const std::string sharedKey = string(env, sharedKeyValue);
        const std::string filmKey = string(env, filmKeyValue);
        const std::string paperName = string(env, paperNameValue);
        if (model.sharedKey != sharedKey) {
            model = RuntimeModel{};
            model.shared = data(env, sharedData);
            model.sharedKey = sharedKey;
        }
        if (model.filmKey != filmKey) {
            model.film.reset();
            model.filmData.clear();
            model.filmKey.clear();
            model.filmData = data(env, filmData);
            model.film = std::make_unique<Filming>(model.shared, model.filmData);
            model.wire = measureDensityWire(*model.film);
            model.filmKey = filmKey;
        }
        if (!model.film || model.film->positive() != bool(positive) || (positive && buildPrint))
            throw std::invalid_argument("Spectral model topology mismatch");
        static const OutputGamut gamut;
        std::vector<float> filmTable, printTable;
        if (buildFilm) {
            if (positive) {
                const Scanner scanner(model.shared, model.filmData, gamut);
                filmTable = generateFilmLut(*model.film, model.wire, &scanner, 65);
            } else {
                filmTable = generateFilmLut(*model.film, model.wire, nullptr, 65);
            }
        }
        if (buildPrint) {
            const DataSet paper = data(env, paperData);
            const Printing printing(model.shared, model.filmData, paper, paperName, model.film->midgrayDensity());
            const Scanner scanner(model.shared, paper, gamut);
            printTable = generatePrintLut(printing, scanner, model.wire, 65);
        }
        jclass floatArrayClass = env->FindClass("[F");
        if (!floatArrayClass) throw std::runtime_error("Missing float array class");
        jobjectArray result = env->NewObjectArray(2, floatArrayClass, nullptr);
        env->DeleteLocalRef(floatArrayClass);
        if (!result) throw std::runtime_error("Unable to allocate spectral result");
        jfloatArray filmOutput = output(env, filmTable);
        env->SetObjectArrayElement(result, 0, filmOutput);
        env->DeleteLocalRef(filmOutput);
        // Release native arrays before constructing the next Java result.
        std::vector<float>().swap(filmTable);
        jfloatArray printOutput = output(env, printTable);
        env->SetObjectArrayElement(result, 1, printOutput);
        env->DeleteLocalRef(printOutput);
        if (env->ExceptionCheck()) throw std::runtime_error("Unable to return spectral result");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        __android_log_print(ANDROID_LOG_DEBUG, "SpektrafilmNative", "Generated on demand: film=%d print=%d size=65 elapsed=%lldms model=%s",
                            int(buildFilm), int(buildPrint), static_cast<long long>(elapsed), filmKey.c_str());
        return result;
    } catch (const std::exception& e) {
        // Kotlin invalidates its model-key handshake after any failed call.
        model = RuntimeModel{};
        if (!env->ExceptionCheck()) {
            jclass exception = env->FindClass("java/lang/IllegalStateException");
            if (exception) env->ThrowNew(exception, e.what());
        }
        return nullptr;
    } catch (...) {
        model = RuntimeModel{};
        if (!env->ExceptionCheck()) {
            jclass exception = env->FindClass("java/lang/IllegalStateException");
            if (exception) env->ThrowNew(exception, "Native spectral generation failed");
        }
        return nullptr;
    }
}
