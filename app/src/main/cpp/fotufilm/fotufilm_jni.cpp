// JNI bridge for vn.hoang.leicafujicam.raw.fotufilm.FotufilmNativeBridge: installs the stock packs,
// the reflectance prior and the film grain bank's disk provider, reads the control catalogue, and
// builds frames for the GLES renderer.
#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "core/FilmGrainAsset.h"
#include "core/FilmStockPack.h"
#include "core/FotufilmControls.h"
#include "core/FotufilmFrameBuilder.h"
#include "core/SpectralModel.h"

namespace {

constexpr const char* kTag = "FotufilmNative";

/// Install replaces the registry and the providers; every other entry reads them.
std::shared_mutex runtimeMutex;
bool installed = false;

// ---- Film grain bank persistence -------------------------------------------------------------

std::mutex grainMutex;
std::string grainDirectory;
std::set<std::string> persistedGrain;

/// SHA-256 of the asset identity: the provider's file key (upstream leaves the asset key to the
/// application, offering `FilmGrainAsset.identity` as the bits to hash).
std::string sha256Hex(const std::string& data) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::vector<uint8_t> message(data.begin(), data.end());
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int i = 7; i >= 0; --i) message.push_back(static_cast<uint8_t>(bitLength >> (i * 8)));
    for (size_t chunk = 0; chunk < message.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = &message[chunk + i * 4];
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

std::string grainPath(const std::string& directory, const std::string& identity) {
    return directory + "/" + sha256Hex(identity) + ".ffgrain";
}

std::optional<std::string> readFile(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return std::nullopt;
    std::string data;
    char buffer[1 << 16];
    size_t read;
    while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        data.append(buffer, read);
        if (data.size() > fotufilm::FilmGrainAsset::maximumByteCount) {
            std::fclose(file);
            return std::nullopt;
        }
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) return std::nullopt;
    return data;
}

/// Writes through a temporary file renamed into place, so a reader never sees a partial bank.
bool writeFileAtomically(const std::string& path, const std::string& data) {
    const std::string temporary = path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

/// Stores a bank the frame's binding prepared rather than read from disk.
void persistGrain(const fotufilm::FilmEngineInvocation& invocation) {
    const auto& binding = invocation.filmTileBinding;
    if (!binding || binding->usedAsset() || !invocation.filmGrainPopulation) return;
    const std::string identity = invocation.filmGrainPopulation->identity();
    std::string directory;
    {
        std::lock_guard<std::mutex> lock(grainMutex);
        if (grainDirectory.empty() || persistedGrain.count(identity)) return;
        persistedGrain.insert(identity);
        directory = grainDirectory;
    }
    const std::string path = grainPath(directory, identity);
    struct stat existing {};
    if (::stat(path.c_str(), &existing) == 0) return;
    try {
        const std::string data = fotufilm::FilmGrainAsset::encode(*binding->grain, identity, binding->tiles().get());
        if (writeFileAtomically(path, data)) {
            __android_log_print(ANDROID_LOG_INFO, kTag, "Stored film grain bank %s (%zu bytes)", path.c_str(), data.size());
        } else {
            __android_log_print(ANDROID_LOG_WARN, kTag, "Unable to store film grain bank %s (errno %d)", path.c_str(), errno);
            std::lock_guard<std::mutex> lock(grainMutex);
            persistedGrain.erase(identity);
        }
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "Unable to encode film grain bank: %s", error.what());
        std::lock_guard<std::mutex> lock(grainMutex);
        persistedGrain.erase(identity);
    }
}

// ---- JNI helpers ------------------------------------------------------------------------------

std::string string(JNIEnv* env, jstring value, const char* what) {
    if (!value) throw std::invalid_argument(std::string("Missing ") + what);
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) throw std::runtime_error(std::string("Unable to read ") + what);
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

/// Java strings as modified UTF-8 can't carry every byte sequence; JSON from the core is UTF-8,
/// which NewStringUTF accepts for the BMP text the catalogue holds.
jstring javaString(JNIEnv* env, const std::string& value) {
    jstring result = env->NewStringUTF(value.c_str());
    if (!result) throw std::runtime_error("Unable to allocate a Java string");
    return result;
}

jfloatArray floatArray(JNIEnv* env, const float* values, size_t count) {
    jfloatArray array = env->NewFloatArray(static_cast<jsize>(count));
    if (!array) throw std::runtime_error("Unable to allocate a float array");
    if (count) env->SetFloatArrayRegion(array, 0, static_cast<jsize>(count), values);
    if (env->ExceptionCheck()) throw std::runtime_error("Unable to copy a float array");
    return array;
}

void throwJava(JNIEnv* env, const char* message) {
    if (env->ExceptionCheck()) return;
    jclass exception = env->FindClass("java/lang/IllegalStateException");
    if (exception) env->ThrowNew(exception, message);
}

std::string hex64(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

/// The renderer's cache key for a frame's tables and configuration: the tables' cache identity
/// and an FNV-1a digest of the configuration's bits.
std::string sourceKey(const fotufilm::FotufilmFrame& frame) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (float value : frame.configuration) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        for (int i = 0; i < 4; ++i) hash = (hash ^ ((bits >> (i * 8)) & 0xff)) * 0x100000001b3ull;
    }
    return "fotufilm-" + hex64(frame.tablesKey) + "-" + hex64(hash);
}

jobject frameObject(JNIEnv* env, const fotufilm::FotufilmFrame& frame, jstring cachedTilesKey, jfloatArray cachedTiles) {
    jclass frameClass = env->FindClass("com/hinnka/mycamera/raw/fotufilm/FotufilmFrame");
    if (!frameClass) throw std::runtime_error("Missing FotufilmFrame class");
    jmethodID constructor = env->GetMethodID(
        frameClass, "<init>", "([FI[F[F[FIIIIIIII[ILjava/lang/String;[FLjava/lang/String;)V");
    if (!constructor) throw std::runtime_error("Missing FotufilmFrame constructor");

    jfloatArray configuration = floatArray(env, frame.configuration.data(), frame.configuration.size());
    jfloatArray exposure = floatArray(env, frame.exposure.data(), frame.exposure.values().size());
    jfloatArray filmOutput = floatArray(env, frame.filmOutput.data(), frame.filmOutput.values().size());
    jfloatArray paperOutput =
        frame.paperOutput ? floatArray(env, frame.paperOutput->data(), frame.paperOutput->values().size()) : nullptr;
    jintArray radii = env->NewIntArray(3);
    if (!radii) throw std::runtime_error("Unable to allocate halation radii");
    const jint radiiValues[3] = {frame.halationPixelRadii[0], frame.halationPixelRadii[1], frame.halationPixelRadii[2]};
    env->SetIntArrayRegion(radii, 0, 3, radiiValues);
    jstring key = javaString(env, sourceKey(frame));

    jfloatArray tiles = nullptr;
    jstring tilesKey = nullptr;
    if (frame.filmTiles) {
        tilesKey = javaString(env, frame.filmTilesKey);
        // The bank is the same for every frame of the population: reuse the caller's copy.
        const bool reuse = cachedTiles && cachedTilesKey &&
                           string(env, cachedTilesKey, "film tiles key") == frame.filmTilesKey &&
                           static_cast<size_t>(env->GetArrayLength(cachedTiles)) == frame.filmTiles->size();
        tiles = reuse ? cachedTiles : floatArray(env, frame.filmTiles->data(), frame.filmTiles->size());
    }

    jobject result = env->NewObject(frameClass, constructor, configuration, static_cast<jint>(frame.exposure.dimension()),
                                    exposure, filmOutput, paperOutput, static_cast<jint>(frame.featureMask),
                                    static_cast<jint>(frame.seed), static_cast<jint>(frame.frameWidth),
                                    static_cast<jint>(frame.frameHeight), static_cast<jint>(frame.spatialSupport),
                                    static_cast<jint>(frame.spatialSupportSansHalation),
                                    static_cast<jint>(frame.lightSupport), static_cast<jint>(frame.halationSupport),
                                    radii, key, tiles, tilesKey);
    if (env->ExceptionCheck() || !result) throw std::runtime_error("Unable to construct FotufilmFrame");
    env->DeleteLocalRef(configuration);
    env->DeleteLocalRef(exposure);
    env->DeleteLocalRef(filmOutput);
    if (paperOutput) env->DeleteLocalRef(paperOutput);
    env->DeleteLocalRef(radii);
    env->DeleteLocalRef(key);
    if (tiles && tiles != cachedTiles) env->DeleteLocalRef(tiles);
    if (tilesKey) env->DeleteLocalRef(tilesKey);
    env->DeleteLocalRef(frameClass);
    return result;
}

jobject meteringObject(JNIEnv* env, const fotufilm::FotufilmFrame& frame) {
    if (!frame.metered || !frame.sceneHighlightStops || !frame.sceneToneStops || !frame.sceneChannelMedians) {
        return nullptr;
    }
    jclass meteringClass = env->FindClass("com/hinnka/mycamera/raw/fotufilm/FotufilmMetering");
    if (!meteringClass) throw std::runtime_error("Missing FotufilmMetering class");
    jmethodID constructor = env->GetMethodID(meteringClass, "<init>", "(F[F[F)V");
    if (!constructor) throw std::runtime_error("Missing FotufilmMetering constructor");
    const fotufilm::Float3 tone = *frame.sceneToneStops;
    const fotufilm::Float3 medians = *frame.sceneChannelMedians;
    const float toneValues[3] = {tone.x, tone.y, tone.z};
    const float medianValues[3] = {medians.x, medians.y, medians.z};
    jfloatArray toneArray = floatArray(env, toneValues, 3);
    jfloatArray medianArray = floatArray(env, medianValues, 3);
    jobject result = env->NewObject(meteringClass, constructor, static_cast<jfloat>(*frame.sceneHighlightStops),
                                    toneArray, medianArray);
    if (env->ExceptionCheck() || !result) throw std::runtime_error("Unable to construct FotufilmMetering");
    env->DeleteLocalRef(toneArray);
    env->DeleteLocalRef(medianArray);
    env->DeleteLocalRef(meteringClass);
    return result;
}

void requireInstalled() {
    if (!installed) throw std::runtime_error("Fotufilm runtime is not installed");
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_hinnka_mycamera_raw_fotufilm_FotufilmNativeBridge_install(JNIEnv* env, jobject, jobjectArray stockJsons,
                                                                    jbyteArray reflectancePrior, jstring grainCacheDir) {
    std::unique_lock<std::shared_mutex> lock(runtimeMutex);
    try {
        if (!stockJsons || !reflectancePrior) throw std::invalid_argument("Missing Fotufilm resources");
        const std::string directory = string(env, grainCacheDir, "film grain cache directory");

        const jsize priorSize = env->GetArrayLength(reflectancePrior);
        std::vector<uint8_t> prior(static_cast<size_t>(priorSize));
        if (priorSize > 0) env->GetByteArrayRegion(reflectancePrior, 0, priorSize, reinterpret_cast<jbyte*>(prior.data()));
        if (env->ExceptionCheck()) throw std::runtime_error("Unable to read the reflectance prior");
        if (!fotufilm::MeasuredReflectanceTable::install(prior.data(), prior.size())) {
            throw std::runtime_error("Invalid reflectance prior resource");
        }

        fotufilm::FilmStockRegistry& registry = fotufilm::FilmStockRegistry::shared();
        registry.clear();
        const jsize count = env->GetArrayLength(stockJsons);
        for (jsize i = 0; i < count; ++i) {
            auto element = static_cast<jstring>(env->GetObjectArrayElement(stockJsons, i));
            const std::string json = string(env, element, "stock JSON");
            env->DeleteLocalRef(element);
            registry.registerJSON(json);
        }

        {
            std::lock_guard<std::mutex> grainLock(grainMutex);
            grainDirectory = directory;
            persistedGrain.clear();
        }
        fotufilm::FilmGrainAsset::install([directory](const std::string& identity) -> std::optional<std::string> {
            return readFile(grainPath(directory, identity));
        });
        installed = true;
        __android_log_print(ANDROID_LOG_INFO, kTag, "Installed %d stocks, prior %d bytes, grain cache %s",
                            static_cast<int>(count), static_cast<int>(priorSize), directory.c_str());
    } catch (const std::exception& error) {
        installed = false;
        throwJava(env, error.what());
    } catch (...) {
        installed = false;
        throwJava(env, "Fotufilm install failed");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_hinnka_mycamera_raw_fotufilm_FotufilmNativeBridge_stocks(JNIEnv* env, jobject) {
    std::shared_lock<std::shared_mutex> lock(runtimeMutex);
    try {
        requireInstalled();
        return javaString(env, fotufilm::fotufilmStocksJSON());
    } catch (const std::exception& error) {
        throwJava(env, error.what());
    } catch (...) {
        throwJava(env, "Fotufilm stock listing failed");
    }
    return nullptr;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_hinnka_mycamera_raw_fotufilm_FotufilmNativeBridge_catalogue(JNIEnv* env, jobject, jstring stockId,
                                                                      jstring documentJson) {
    std::shared_lock<std::shared_mutex> lock(runtimeMutex);
    try {
        requireInstalled();
        const std::string stock = string(env, stockId, "stock id");
        const std::string document = string(env, documentJson, "document");
        return javaString(env, fotufilm::fotufilmCatalogueJSON(stock, document));
    } catch (const std::exception& error) {
        throwJava(env, error.what());
    } catch (...) {
        throwJava(env, "Fotufilm catalogue failed");
    }
    return nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_hinnka_mycamera_raw_fotufilm_FotufilmNativeBridge_buildFrame(
    JNIEnv* env, jobject, jstring stockId, jstring documentJson, jint width, jint height, jlong frameIndex,
    jobject meteringBuffer, jint meteringWidth, jint meteringHeight, jboolean measureFlareOnDevice,
    jfloatArray sceneMetering, jstring cachedFilmTilesKey, jfloatArray cachedFilmTiles) {
    std::shared_lock<std::shared_mutex> lock(runtimeMutex);
    try {
        requireInstalled();
        if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid Fotufilm frame size");
        fotufilm::FotufilmFrameBuilder::Request request;
        request.stockID = string(env, stockId, "stock id");
        request.options = fotufilm::optionsFromDocument(request.stockID, string(env, documentJson, "document"));
        request.width = width;
        request.height = height;
        request.frameIndex = static_cast<uint64_t>(frameIndex);
        if (sceneMetering) {
            if (env->GetArrayLength(sceneMetering) != 7) throw std::invalid_argument("Scene metering holds 7 floats");
            float readings[7];
            env->GetFloatArrayRegion(sceneMetering, 0, 7, readings);
            request.options.sceneHighlightStops = readings[0];
            request.options.sceneToneStops = fotufilm::Float3(readings[1], readings[2], readings[3]);
            request.options.sceneChannelMedians = fotufilm::Float3(readings[4], readings[5], readings[6]);
        }

        fotufilm::FotufilmFrameBuilder builder(request);
        const bool wantsMetering = builder.wantsSceneMetering();
        const bool wantsFlare = builder.wantsFlareMean();
        const float* pixels = nullptr;
        if (meteringBuffer) {
            pixels = static_cast<const float*>(env->GetDirectBufferAddress(meteringBuffer));
            const jlong capacity = env->GetDirectBufferCapacity(meteringBuffer);
            if (!pixels || capacity < 0) throw std::invalid_argument("Metering buffer must be direct");
            if (meteringWidth <= 0 || meteringHeight <= 0 ||
                capacity < static_cast<jlong>(meteringWidth) * meteringHeight * 4 * static_cast<jlong>(sizeof(float))) {
                throw std::invalid_argument("Metering buffer is smaller than its size");
            }
        }
        if (pixels && (wantsMetering || wantsFlare)) {
            builder.meter(pixels, meteringWidth, meteringHeight);
        } else if (measureFlareOnDevice) {
            builder.measureFlareOnDevice();
        }
        const fotufilm::FotufilmFrame frame = builder.frame();
        persistGrain(builder.invocation());

        jobject frameValue = frameObject(env, frame, cachedFilmTilesKey, cachedFilmTiles);
        jobject meteringValue = meteringObject(env, frame);
        jclass resultClass = env->FindClass("com/hinnka/mycamera/raw/fotufilm/FotufilmBuildResult");
        if (!resultClass) throw std::runtime_error("Missing FotufilmBuildResult class");
        jmethodID constructor = env->GetMethodID(
            resultClass, "<init>",
            "(Lcom/hinnka/mycamera/raw/fotufilm/FotufilmFrame;Lcom/hinnka/mycamera/raw/fotufilm/FotufilmMetering;ZZ)V");
        if (!constructor) throw std::runtime_error("Missing FotufilmBuildResult constructor");
        jobject result = env->NewObject(resultClass, constructor, frameValue, meteringValue,
                                        static_cast<jboolean>(wantsMetering), static_cast<jboolean>(wantsFlare));
        if (env->ExceptionCheck() || !result) throw std::runtime_error("Unable to construct FotufilmBuildResult");
        env->DeleteLocalRef(frameValue);
        if (meteringValue) env->DeleteLocalRef(meteringValue);
        env->DeleteLocalRef(resultClass);
        return result;
    } catch (const std::exception& error) {
        throwJava(env, error.what());
    } catch (...) {
        throwJava(env, "Fotufilm frame build failed");
    }
    return nullptr;
}
