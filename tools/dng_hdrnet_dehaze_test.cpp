#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// Exercise the production HDRNet evaluator and Dehaze/DHA curve implementation directly.
#include "../app/src/main/cpp/dng_hdrnet_pgtm_jni.cpp"

namespace {

// Minimal host JNI array access so this regression exercises the production
// gain-table generator, including its point count and stored-weight contract.
struct TestFloatArray : _jfloatArray {
  std::vector<float> values;
  explicit TestFloatArray(std::vector<float> data) : values(std::move(data)) {}
};

std::vector<float> GenerateHighlightTable(int points, float range_scale,
                                        float input_gain = 1.0f, float post_gain = 1.0f) {
  JNINativeInterface_ functions{};
  functions.GetArrayLength = [](JNIEnv*, jarray array) -> jsize {
    return static_cast<jsize>(static_cast<TestFloatArray*>(array)->values.size());
  };
  functions.GetFloatArrayElements = [](JNIEnv*, jfloatArray array, jboolean*) -> jfloat* {
    return static_cast<TestFloatArray*>(array)->values.data();
  };
  functions.ReleaseFloatArrayElements = [](JNIEnv*, jfloatArray, jfloat*, jint) {};
  functions.ExceptionCheck = [](JNIEnv*) -> jboolean { return JNI_FALSE; };
  JNIEnv env{&functions};
  TestFloatArray coefficients({1.0f, 0.0f});
  TestFloatArray model({0.5f * input_gain, 0.5f * input_gain,
                       0.5f * input_gain, 2.0f * input_gain});
  TestFloatArray shifts({0.0f}), slopes({1.0f}), curve({0.0f, 1.0f});
  TestFloatArray dehaze({0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f});
  const float weight_scale = range_scale * input_gain;
  TestFloatArray weights({0.1495f * weight_scale, 0.2935f * weight_scale,
                         0.057f * weight_scale, 0.125f * weight_scale, 0.375f * weight_scale});
  TestFloatArray output(std::vector<float>(points, 0.0f));
  assert(Java_com_hinnka_mycamera_raw_DngHdrNetProfileGainTableNative_nativeGenerateGains(
      &env, nullptr, &coefficients, &model, 1, 1, 4, 1, 1, 1, 2, 1, 1, points,
      4.0f, input_gain, 1.0f, 0.03f, 30.0f, 0.0f, 1.0f / 4096.0f, 4096.0f,
      &shifts, &slopes, &curve, &dehaze, post_gain, &weights, &output));
  return output.values;
}

float RenderNeutralThroughTable(float source, float range_scale, const std::vector<float>& table) {
  const float index = std::clamp(source * range_scale * table.size(), 0.0f,
                                 static_cast<float>(table.size() - 1));
  const int lower = static_cast<int>(std::floor(index));
  const int upper = std::min(lower + 1, static_cast<int>(table.size() - 1));
  return source * Lerp(table[lower], table[upper], index - lower);
}

void TestRangeExtensionDoesNotRebrightenCompressedHighlights() {
  const auto old_table = GenerateHighlightTable(257, 1.0f);
  const float scale = 257.0f / 413.0f;
  const auto expanded = GenerateHighlightTable(413, scale);
  for (int point = 0; point < 257; ++point) {
    assert(std::abs(old_table[point] - expanded[point]) < 2.0e-5f);
  }
  // The old table hits its terminal gain: a source of 1.6 becomes a bright
  // island, despite the final post-exposure target remaining at white.
  assert(RenderNeutralThroughTable(1.6f, 1.0f, old_table) > 1.6f);
  for (float value : {0.5f, 0.8f, 1.0f, 1.2f, 1.6f}) {
    // Linear interpolation of reciprocal gains has at most ~1.6e-5 error here.
    assert(std::abs(RenderNeutralThroughTable(value, scale, expanded) - 1.0f) < 2.0e-5f);
  }
}

void TestInputExposureIsBakedOnceBeforeHighlightCompression() {
  const auto legacy = GenerateHighlightTable(257, 1.0f, 1.0f, 0.25f);
  const auto input_exposed = GenerateHighlightTable(257, 1.0f, 0.25f, 1.0f);
  // The old post-EV turns both clipped highlights into the same gray plateau.
  for (float source : {0.5f, 0.8f}) {
    assert(std::abs(RenderNeutralThroughTable(source, 1.0f, legacy) - 0.25f) < 1.0e-5f);
    // New exposure participates in the nonlinear response. The stored N weights
    // and denominator encode it once, while the renderer still receives original RAW.
    assert(std::abs(RenderNeutralThroughTable(source, 0.25f, input_exposed) - source) < 1.0e-5f);
  }
}

void TestDisabledCurveIsIdentity() {
  std::vector<uint32_t> haze(kDehazeHistogramSize, 0);
  std::vector<uint32_t> highlight(kHighlightHistogramSize, 0);
  int sample_count = 0;
  for (int index = 0; index < 4096; ++index) {
    AddDehazeHistogramSample(
        RgbSample{0.2f, 0.4f, 0.6f}, &haze, &highlight, &sample_count);
  }
  const DehazeCurve curve =
      EstimateDehazeCurve(haze, highlight, sample_count, 0.0f, 0.0f);
  for (int index = 0; index <= 100; ++index) {
    const float value = static_cast<float>(index) / 100.0f;
    assert(std::abs(MappedDehazeLuminance(value, curve) - value) < 1.0e-6f);
  }
}

void TestHdrNetOutputFeedsWholeImageDehaze() {
  constexpr int input_width = 256;
  constexpr int input_height = 192;
  constexpr int input_channels = 4;
  constexpr int grid_width = 16;
  constexpr int grid_height = 12;
  constexpr int grid_depth = 8;
  constexpr int coefficient_count = 2;
  std::vector<float> model_input(
      input_width * input_height * input_channels, 0.0f);
  for (size_t offset = 0; offset < model_input.size(); offset += input_channels) {
    model_input[offset] = 0.25f;
    model_input[offset + 1] = 0.25f;
    model_input[offset + 2] = 0.25f;
    model_input[offset + 3] = 0.5f;
  }
  std::vector<float> coefficients(
      grid_width * grid_height * grid_depth * coefficient_count, 0.0f);
  const float guide_shifts[] = {0.0f};
  const float guide_slopes[] = {1.0f};

  std::vector<uint32_t> haze(kDehazeHistogramSize, 0);
  std::vector<uint32_t> highlight(kHighlightHistogramSize, 0);
  int sample_count = 0;
  for (int y = 0; y < input_height; ++y) {
    for (int x = 0; x < input_width; ++x) {
      RgbSample hdr_rgb{};
      assert(EvaluateHdrNetRgb(
          coefficients.data(), grid_width, grid_height, grid_depth,
          coefficient_count, model_input.data(), input_width, input_height,
          input_channels, (x + 0.5f) / input_width, (y + 0.5f) / input_height,
          2.0f, 0.25f, 8.0f, 0.02f, guide_shifts, guide_slopes, 1,
          &hdr_rgb));
      assert(std::abs(hdr_rgb.red - 0.25f) < 2.0e-6f);
      AddDehazeHistogramSample(
          hdr_rgb, &haze, &highlight, &sample_count);
    }
  }
  assert(sample_count == input_width * input_height);

  const DehazeCurve curve =
      EstimateDehazeCurve(haze, highlight, sample_count, 1.0f, 1.0f);
  const float render_gain = 0.25f / (0.25f + kHdrNetGainEpsilon);
  const RgbSample hdr_rgb{
      0.25f * render_gain,
      0.25f * render_gain,
      0.25f * render_gain,
  };
  const RgbSample composed = ApplyDehaze(hdr_rgb, curve);
  const float baked_target = PostExposedHdrNetTargetLuma(
      RgbSample{0.25f, 0.25f, 0.25f}, render_gain, curve,
      photon::hdrnet_post_exposure::SplitGain(1.0f));
  assert(curve.sampled_pixel_count == input_width * input_height);
  assert(std::abs(
      composed.red - MappedDehazeLuminance(hdr_rgb.red, curve)) < 1.0e-6f);
  assert(std::abs(composed.red - baked_target) < 1.0e-6f);
  assert(composed.red > 0.25f);
  constexpr float post_exposure_gain = 1.25f;
  const auto gains = photon::hdrnet_post_exposure::SplitGain(post_exposure_gain);
  const float exposed_target = PostExposedHdrNetTargetLuma(
      RgbSample{0.25f, 0.25f, 0.25f}, render_gain, curve, gains);
  const auto expected = photon::hdrnet_post_exposure::Apply(composed, gains);
  assert(std::abs(exposed_target - expected.red) < 1.0e-6f);
  assert(exposed_target < composed.red * post_exposure_gain);
}

void TestColoredPgtmCoordinateAndRolloffAgreeWithEvaluation() {
  const float weights[] = {0.1495f, 0.2935f, 0.057f, 0.125f, 0.375f};
  const RgbSample source{0.3f, 0.05f, 0.05f};
  const float coordinate = TableIntensity(source, weights);
  assert(std::abs(coordinate - 0.181125f) < 1.0e-7f);
  assert(std::abs(HdrNetLuma(source) - 0.12470703125f) < 1.0e-7f);
  const auto reconstructed = RgbForTableCoordinate(coordinate, source, coordinate, 1.0f);
  assert(std::abs(reconstructed.red - source.red) < 1.0e-7f);
  assert(std::abs(reconstructed.green - source.green) < 1.0e-7f);
  const DehazeCurve identity;
  const auto gains = photon::hdrnet_post_exposure::SplitGain(1.5f);
  const auto expected = photon::hdrnet_post_exposure::Apply(source, gains);
  const float baked = PostExposedHdrNetTargetLuma(reconstructed, 1.0f, identity, gains);
  assert(std::abs(baked - HdrNetLuma(expected)) < 1.0e-7f);
  // Applying the shared gain to a colored pixel depends on peak RGB, not PXL N or Rec.601 Y.
  const auto gray = photon::hdrnet_post_exposure::Apply(
      {HdrNetLuma(source), HdrNetLuma(source), HdrNetLuma(source)}, gains);
  assert(gray.red > baked);

  const RgbSample signed_noise{0.1f, -0.05f, -0.05f};
  const auto neutral = RgbForTableCoordinate(
      0.2f, signed_noise, TableIntensity(signed_noise, weights), 1.0f);
  assert(neutral.red == 0.2f && neutral.green == 0.2f && neutral.blue == 0.2f);
}

void TestRangeScalingPreservesDarkCellChromaticity() {
  const float weights[] = {0.1495f, 0.2935f, 0.057f, 0.125f, 0.375f};
  const RgbSample cell{3.0e-6f, 1.0e-6f, 0.5e-6f};
  const float coordinate = TableIntensity(cell, weights);
  const auto original = RgbForTableCoordinate(0.4f, cell, coordinate, 1.0f);
  for (float scale : {0.63f, 0.1f}) {
    const auto expanded = RgbForTableCoordinate(0.4f * scale, cell, coordinate * scale, scale);
    assert(std::abs(expanded.red - original.red) < 1.0e-6f);
    assert(std::abs(expanded.green - original.green) < 1.0e-6f);
    assert(std::abs(expanded.blue - original.blue) < 1.0e-6f);
    assert(expanded.red > expanded.green * 2.9f);
  }
}

}  // namespace

int main() {
  TestInputExposureIsBakedOnceBeforeHighlightCompression();
  TestRangeExtensionDoesNotRebrightenCompressedHighlights();
  TestRangeScalingPreservesDarkCellChromaticity();
  TestDisabledCurveIsIdentity();
  TestHdrNetOutputFeedsWholeImageDehaze();
  TestColoredPgtmCoordinateAndRolloffAgreeWithEvaluation();
  std::cout << "HDRNet Dehaze/DHA composition tests passed\n";
  return 0;
}
