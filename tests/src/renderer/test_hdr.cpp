#include <gtest/gtest.h>
#include <hongtu/renderer/hdr/hdr_pipeline.hpp>
#include <cmath>
#include <vector>

using namespace hongtu::renderer;

// ============================================================================
// Luminance
// ============================================================================

TEST(LuminanceTest, White) {
  EXPECT_NEAR(luminance({1, 1, 1}), 1.0f, 0.01f);
}

TEST(LuminanceTest, Black) {
  EXPECT_NEAR(luminance({0, 0, 0}), 0.0f, 1e-6f);
}

TEST(LuminanceTest, PureGreen) {
  // Green has highest luminance weight (0.7152)
  float g = luminance({0, 1, 0});
  float r = luminance({1, 0, 0});
  float b = luminance({0, 0, 1});
  EXPECT_GT(g, r);
  EXPECT_GT(g, b);
}

// ============================================================================
// HDR Format
// ============================================================================

TEST(HDRFormatTest, BytesPerPixel) {
  EXPECT_EQ(hdrFormatBytesPerPixel(HDRFormat::RGBA16F), 8u);
  EXPECT_EQ(hdrFormatBytesPerPixel(HDRFormat::RGBA32F), 16u);
  EXPECT_EQ(hdrFormatBytesPerPixel(HDRFormat::R11G11B10F), 4u);
}

TEST(HDRFormatTest, Names) {
  EXPECT_STREQ(hdrFormatName(HDRFormat::RGBA16F), "RGBA16F");
  EXPECT_STREQ(hdrFormatName(HDRFormat::R11G11B10F), "R11G11B10F");
}

// ============================================================================
// Luminance Histogram
// ============================================================================

TEST(HistogramTest, EmptyHistogram) {
  LuminanceHistogram hist;
  EXPECT_EQ(hist.getTotalSamples(), 0u);
  EXPECT_NEAR(hist.computeAverageLuminance(), 0.1f, 0.01f); // Default fallback
}

TEST(HistogramTest, UniformSamples) {
  LuminanceHistogram hist;
  for (int i = 0; i < 1000; ++i) {
    hist.addSample(1.0f); // All same luminance
  }
  EXPECT_EQ(hist.getTotalSamples(), 1000u);
  float avg = hist.computeAverageLuminance();
  EXPECT_NEAR(avg, 1.0f, 0.5f); // Should be close to 1.0
}

TEST(HistogramTest, BuildFromBuffer) {
  std::vector<float> buffer(1000, 0.5f);
  LuminanceHistogram hist;
  hist.buildFromBuffer(buffer.data(), static_cast<uint32_t>(buffer.size()));
  EXPECT_EQ(hist.getTotalSamples(), 1000u);
}

TEST(HistogramTest, ExposureFromLuminance) {
  // Bright scene -> low exposure
  float bright_exp = LuminanceHistogram::computeExposure(2.0f, 0.18f);
  // Dark scene -> high exposure
  float dark_exp = LuminanceHistogram::computeExposure(0.01f, 0.18f);
  EXPECT_LT(bright_exp, dark_exp);
}

TEST(HistogramTest, BinAccess) {
  LuminanceHistogram hist;
  hist.addSample(1.0f);
  // At least one bin should have a count
  uint32_t total = 0;
  for (uint32_t i = 0; i < LuminanceHistogram::BIN_COUNT; ++i) {
    total += hist.getBin(i);
  }
  EXPECT_EQ(total, 1u);
}

TEST(HistogramTest, LuminanceRange) {
  LuminanceHistogram hist;
  float min_log, max_log;
  hist.getLuminanceRange(min_log, max_log);
  EXPECT_LT(min_log, max_log);
}

// ============================================================================
// Auto Exposure
// ============================================================================

TEST(AutoExposureTest, ManualMode) {
  HDRConfig config;
  config.auto_exposure = false;
  config.manual_exposure = 2.5f;
  AutoExposure ae(config);

  float exp = ae.update(1.0f, 0.016f);
  EXPECT_FLOAT_EQ(exp, 2.5f);
}

TEST(AutoExposureTest, AdaptsToBrightScene) {
  HDRConfig config;
  config.auto_exposure = true;
  config.adaptation_speed_up = 10.0f;
  config.adaptation_speed_down = 10.0f;
  AutoExposure ae(config);

  // Simulate several frames of bright scene
  float exp = 1.0f;
  for (int i = 0; i < 100; ++i) {
    exp = ae.update(5.0f, 0.016f); // Bright avg luminance
  }
  // Exposure should decrease for bright scenes
  EXPECT_LT(exp, 1.0f);
}

TEST(AutoExposureTest, AdaptsToDarkScene) {
  HDRConfig config;
  config.auto_exposure = true;
  config.adaptation_speed_up = 10.0f;
  config.adaptation_speed_down = 10.0f;
  AutoExposure ae(config);

  float exp = 1.0f;
  for (int i = 0; i < 100; ++i) {
    exp = ae.update(0.01f, 0.016f); // Dark scene
  }
  // Exposure should increase for dark scenes
  EXPECT_GT(exp, 1.0f);
}

TEST(AutoExposureTest, ClampedToRange) {
  HDRConfig config;
  config.auto_exposure = true;
  config.exposure_min = 0.1f;
  config.exposure_max = 5.0f;
  config.adaptation_speed_up = 100.0f;
  config.adaptation_speed_down = 100.0f;
  AutoExposure ae(config);

  // Very dark scene -> exposure should clamp at max
  for (int i = 0; i < 200; ++i) {
    ae.update(0.0001f, 0.016f);
  }
  EXPECT_LE(ae.getExposure(), config.exposure_max);
  EXPECT_GE(ae.getExposure(), config.exposure_min);
}

TEST(AutoExposureTest, Reset) {
  AutoExposure ae;
  ae.update(0.01f, 0.016f);
  ae.reset();
  EXPECT_FLOAT_EQ(ae.getExposure(), 1.0f);
}
