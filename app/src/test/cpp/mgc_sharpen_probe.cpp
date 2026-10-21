// Standalone Android/arm64 probe. No APK or instrumentation test installation.
// Compile with the production MGC adapter/runtime/capsule, -fopenmp
// -static-openmp -fno-fast-math -ffp-contract=off -ffunction-sections
// -Wl,--gc-sections.
#include "mgc_denoise_static.h"
#include "mgc_sharpen_adapter.h"
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <jni.h>
#include <vector>
using namespace photon::mgc_denoise;
using Clock = std::chrono::steady_clock;
extern "C" jfloat
Java_com_hinnka_mycamera_raw_RawDemosaicProcessor_estimateMgcReferenceSignalNative(
    JNIEnv *, jobject, jobject, jint, jint, jint, jint, jint, jint, jint,
    jfloat, jint, jfloat);

// Only the two direct-buffer JNI functions used by the production statistics
// entry are stubbed. All validation, sampling, histogram coordinates, sqrt and
// reduction run in production code.
static void SignalStats(int w, int h) {
  struct Buffer {
    void *data;
    jlong bytes;
  };
  std::vector<uint16_t> raw(size_t(w) * h, 512);
  Buffer buffer{raw.data(), jlong(raw.size() * 2)};
  JNINativeInterface functions{};
  functions.GetDirectBufferAddress = [](JNIEnv *, jobject b) -> void * {
    return reinterpret_cast<Buffer *>(b)->data;
  };
  functions.GetDirectBufferCapacity = [](JNIEnv *, jobject b) -> jlong {
    return reinterpret_cast<Buffer *>(b)->bytes;
  };
  JNIEnv env{&functions};
  for (int i = 0; i < 4; ++i) {
    auto start = Clock::now();
    float signal =
        Java_com_hinnka_mycamera_raw_RawDemosaicProcessor_estimateMgcReferenceSignalNative(
            &env, nullptr, reinterpret_cast<jobject>(&buffer), 0, buffer.bytes,
            w, h, w * 2, 1, 1, 64.f, 4095, 4031.f);
    double ms =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (!std::isfinite(signal) || std::abs(signal - 448.f / 4031.f) > 1e-6f)
      std::exit(1);
    printf("signalStats %dx%d run=%d nativeMs=%.3f signal=%.9g\n", w, h, i, ms,
           signal);
  }
}
static void Check(bool ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL %s gl=%x\n", what, glGetError());
    std::exit(1);
  }
}
static double Ms(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
static void Scalar(const uint8_t *rgba, size_t n, int16_t *out) {
  constexpr float m[] = {
      .2125999927520752f,   .7152000069618225f,  .07219959795475006f,
      -.16245023906230927f, -.5464943051338196f, .7089447379112244f,
      .9999967217445374f,   -.9083024859428406f, -.09169333428144455f};
#pragma omp parallel for schedule(static) num_threads(4)
  for (size_t i = 0; i < n; i++) {
    float r = rgba[i * 4] * (4095.f / 255.f),
          g = rgba[i * 4 + 1] * (4095.f / 255.f),
          b = rgba[i * 4 + 2] * (4095.f / 255.f);
    for (int c = 0; c < 3; c++)
      out[c * n + i] = std::clamp<long>(
          std::lrintf(m[c * 3] * r + m[c * 3 + 1] * g + m[c * 3 + 2] * b),
          -4095, 4095);
  }
}
static void VerifyConversions() {
  constexpr size_t n = 65536;
  std::vector<uint8_t> rgba(n * 4);
  std::vector<int16_t> a(n * 3), b(n * 3);
  for (int r = 0; r < 256; r++) {
    for (size_t i = 0; i < n; i++) {
      rgba[i * 4] = r;
      rgba[i * 4 + 1] = i / 256;
      rgba[i * 4 + 2] = i % 256;
      rgba[i * 4 + 3] = 255;
    }
    Scalar(rgba.data(), n, a.data());
    SharpenRgbaToYuv(rgba.data(), n, b.data());
    Check(a == b, "all RGB8 -> YUV12 exact");
  }
  for (size_t tail = 1; tail < 24; tail++) {
    Scalar(rgba.data(), tail, a.data());
    SharpenRgbaToYuv(rgba.data(), tail, b.data());
    Check(std::equal(a.begin(), a.begin() + tail * 3, b.begin()),
          "NEON scalar tail");
  }
  std::vector<uint16_t> rgb(n * 3);
  for (size_t i = 0; i < rgb.size(); i++)
    rgb[i] = i % 65536;
  SharpenRgbToRgba(rgb.data(), n, rgba.data());
  for (size_t i = 0; i < n; i++)
    for (int c = 0; c < 3; c++)
      Check(rgba[i * 4 + c] ==
                (std::min<unsigned>(rgb[i * 3 + c], 4095) * 255 + 2047) / 4095,
            "U12 to U8 exact");
  puts("PASS all 16777216 RGB8 colors, SIMD tails, all uint16 output values");
}
static void Sharpen(int16_t *in, uint16_t *out, int w, int h) {
  // Frozen SNR20 generic fixture for adapter/tile/performance checks.
  // Production defaults now live only in PhotonSharpenTuning; the JVM regression
  // checks cover its table and SNR interpolation independently of this probe.
  const float curves[] = {
      0, .05f, 1, 2, 3, 0, .03f, 1, 2, 3, 0, .02f, 1, 2, 3,
      0, .05f * 1.6f, 3.2f, 2, 3,
      0, .03f * 1.3f, 2.6f, 2, 3,
      0, .02f, 1.4f, 2, 3,
  };
  const float corner_correction[] = {0, 0, 0};
  int rc =
      RunSharpenTo16Bit(in, w, h, curves, corner_correction, .8f, out);
  if (rc)
    fprintf(stderr, "kernel size=%dx%d rc=%d\n", w, h, rc);
  Check(rc == 0, "kernel");
}
static void VerifyTiles() {
  const int w = 512, h = 384;
  const size_t n = w * h;
  std::vector<uint8_t> rgba(n * 4);
  for (size_t i = 0; i < rgba.size(); i++)
    rgba[i] = (i * 17 + i / 31) % 256;
  std::vector<int16_t> in(n * 3);
  std::vector<uint16_t> full(n * 3);
  SharpenRgbaToYuv(rgba.data(), n, in.data());
  Sharpen(in.data(), full.data(), w, h);
  // CFA-only phase 2 and odd dimensions are deliberate; the kernel must not
  // reset a pyramid phase.
  for (int origin : {0, 2, 8, 16, 50, 112}) {
    const int tw = 257, th = 193;
    const size_t tn = tw * th;
    std::vector<int16_t> ti(tn * 3);
    std::vector<uint16_t> to(size_t(tw) * ((th + 1) & ~1) * 3);
    for (int c = 0; c < 3; c++)
      for (int y = 0; y < th; y++)
        std::copy_n(in.data() + c * n + (y + origin) * w + origin, tw,
                    ti.data() + c * tn + y * tw);
    Sharpen(ti.data(), to.data(), tw, th);
    size_t mismatches = 0;
    for (int y = 52; y < th - 52; y++)
      for (int x = 52; x < tw - 52; x++)
        for (int c = 0; c < 3; c++)
          mismatches += to[(y * tw + x) * 3 + c] !=
                        full[((y + origin) * w + x + origin) * 3 + c];
    printf("tile origin=%d mismatches=%zu\n", origin, mismatches);
    Check(mismatches == 0,
          "tile interior equivalence with 52px sharpen support");
  }
}
static void Gpu(int w, int h, const char *shaderFile) {
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  Check(eglInitialize(d, nullptr, nullptr), "eglInitialize");
  EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                    EGL_OPENGL_ES3_BIT, EGL_NONE},
         num = 0;
  EGLConfig cfg;
  Check(eglChooseConfig(d, attrs, &cfg, 1, &num) && num > 0, "eglChooseConfig");
  EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE},
         pa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
  EGLSurface surf = eglCreatePbufferSurface(d, cfg, pa);
  Check(eglMakeCurrent(d, surf, surf, ctx), "eglMakeCurrent");
  printf("GPU %s %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  std::ifstream f(shaderFile);
  std::string src((std::istreambuf_iterator<char>(f)), {});
  Check(!src.empty(), "shader source");
  GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  const char *p = src.c_str();
  glShaderSource(sh, 1, &p, nullptr);
  glCompileShader(sh);
  GLint ok;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(sh, 2048, nullptr, log);
    puts(log);
  }
  Check(ok, "compute compile");
  GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  Check(ok, "compute link");
  size_t n = size_t(w) * h, bytes = n * 4;
  std::vector<uint8_t> input(bytes), reference(bytes), result(bytes);
  for (size_t i = 0; i < n; i++) {
    input[i * 4] = (i * 13 + i / w) % 256;
    input[i * 4 + 1] = (i * 19) % 256;
    input[i * 4 + 2] = (i / 7) % 256;
    input[i * 4 + 3] = 255;
  }
  std::vector<int16_t> yuv(n * 3);
  std::vector<uint16_t> rgb(size_t(w) * ((h + 1) & ~1) * 3);
  auto t = Clock::now();
  Scalar(input.data(), n, yuv.data());
  double oldMs = Ms(t);
  t = Clock::now();
  SharpenRgbaToYuv(input.data(), n, yuv.data());
  double newMs = Ms(t);
  t = Clock::now();
  Sharpen(yuv.data(), rgb.data(), w, h);
  double kernelMs = Ms(t);
  t = Clock::now();
  SharpenRgbToRgba(rgb.data(), n, reference.data());
  double outputMs = Ms(t);
  printf("CPU %dx%d oldConvertMs=%.3f neonConvertMs=%.3f kernelMs=%.3f "
         "outMs=%.3f\n",
         w, h, oldMs, newMs, kernelMs, outputMs);
  GLuint tex[2], fb, buf;
  glGenTextures(2, tex);
  for (auto v : tex) {
    glBindTexture(GL_TEXTURE_2D, v);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glBindTexture(GL_TEXTURE_2D, tex[0]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                  input.data());
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex[0], 0);
  Check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "framebuffer");
  glGenBuffers(1, &buf);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, buf);
  glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_COPY);
  GLint64 limit;
  glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limit);
  Check(bytes <= size_t(limit), "SSBO limit");
  for (bool compute : {false, true})
    for (int run = 0; run < 3; run++) {
      glFinish();
      t = Clock::now();
      if (compute) {
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex[0]);
        glUniform1i(glGetUniformLocation(prog, "uInput"), 0);
        glUniform2i(glGetUniformLocation(prog, "uSize"), w, h);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
        glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                        GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
      } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex[0], 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buf);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, buf);
      auto *mapped = (uint8_t *)glMapBufferRange(
          GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
      Check(mapped, "map");
      double transfer = Ms(t);
      Check(memcmp(mapped, input.data(), bytes) == 0,
            "packed RGBA8 equals framebuffer input");
      t = Clock::now();
      SharpenRgbaToYuv(mapped, n, yuv.data());
      double cin = Ms(t);
      t = Clock::now();
      Sharpen(yuv.data(), rgb.data(), w, h);
      double kernel = Ms(t);
      t = Clock::now();
      SharpenRgbToRgba(rgb.data(), n, mapped);
      double cout = Ms(t);
      Check(glUnmapBuffer(GL_PIXEL_PACK_BUFFER), "unmap");
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      t = Clock::now();
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buf);
      glBindTexture(GL_TEXTURE_2D, tex[1]);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                      nullptr);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      glFinish();
      double upload = Ms(t);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex[1], 0);
      glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
      Check(result == reference, "full original kernel transfer output exact");
      Check(glGetError() == GL_NO_ERROR, "complete GL chain");
      printf("GPU %dx%d %s run=%d transferMapMs=%.3f convertInMs=%.3f "
             "kernelMs=%.3f convertOutMs=%.3f uploadGpuCompleteMs=%.3f "
             "sumMs=%.3f exact=true\n",
             w, h, compute ? "SSBO" : "PBO", run, transfer, cin, kernel, cout,
             upload, transfer + cin + kernel + cout + upload);
      fflush(stdout);
    }
  glDeleteBuffers(1, &buf);
  glDeleteTextures(2, tex);
  glDeleteFramebuffers(1, &fb);
  glDeleteProgram(prog);
  glDeleteShader(sh);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(d, surf);
  eglDestroyContext(d, ctx);
  eglTerminate(d);
}
int main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--stats") == 0) {
    SignalStats(4000, 3000);
    SignalStats(8192, 6144);
    return 0;
  }
  if (argc == 1) {
    VerifyConversions();
    VerifyTiles();
    return 0;
  }
  Check(argc == 4, "arguments width height shaderFile");
  Gpu(atoi(argv[1]), atoi(argv[2]), argv[3]);
}
