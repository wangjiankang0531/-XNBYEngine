#include <gtest/gtest.h>
#include <hongtu/renderer/ssr/ssr_system.hpp>
#include <cmath>
#include <vector>

using namespace hongtu::renderer;

// ============================================================================
// HiZPyramid
// ============================================================================

TEST(HiZPyramidTest, Build) {
  HiZPyramid pyramid;
  pyramid.build(1920, 1080, 8);
  EXPECT_EQ(pyramid.getMipCount(), 8u);
  EXPECT_EQ(pyramid.getLevels()[0].width, 1920u);
  EXPECT_EQ(pyramid.getLevels()[0].height, 1080u);
  EXPECT_EQ(pyramid.getLevels()[1].width, 960u);
  EXPECT_EQ(pyramid.getLevels()[1].height, 540u);
}

TEST(HiZPyramidTest, MipSelection) {
  HiZPyramid pyramid;
  pyramid.build(1920, 1080, 6);

  EXPECT_EQ(pyramid.selectMipForStepSize(0.5f), 0u); // Fine step -> mip 0
  EXPECT_GT(pyramid.selectMipForStepSize(8.0f), 0u);  // Coarse step -> higher mip
}

TEST(HiZPyramidTest, SmallTexture) {
  HiZPyramid pyramid;
  pyramid.build(4, 4, 10);
  // 4x4 -> 2x2 -> 1x1 = 3 levels max
  EXPECT_LE(pyramid.getMipCount(), 3u);
}

// ============================================================================
// Reflection
// ============================================================================

TEST(SSRReflectionTest, PerfectMirror) {
  // Ray going straight down, reflecting off horizontal surface (normal = up)
  glm::vec3 view_dir(0, -1, 0);
  glm::vec3 normal(0, 1, 0);
  auto reflected = SSRRayMarcher::reflectDirection(view_dir, normal);
  EXPECT_NEAR(reflected.x, 0.0f, 1e-5f);
  EXPECT_NEAR(reflected.y, 1.0f, 1e-5f);
  EXPECT_NEAR(reflected.z, 0.0f, 1e-5f);
}

TEST(SSRReflectionTest, AnglePreserved) {
  glm::vec3 view_dir = glm::normalize(glm::vec3(1, -1, 0));
  glm::vec3 normal(0, 1, 0);
  auto reflected = SSRRayMarcher::reflectDirection(view_dir, normal);

  // Angle of incidence = angle of reflection
  float cos_in = glm::dot(-view_dir, normal);
  float cos_out = glm::dot(reflected, normal);
  EXPECT_NEAR(cos_in, cos_out, 1e-5f);
}

TEST(SSRReflectionTest, UnitLength) {
  glm::vec3 view_dir = glm::normalize(glm::vec3(0.5f, -0.7f, 0.3f));
  glm::vec3 normal = glm::normalize(glm::vec3(0.1f, 1.0f, 0.2f));
  auto reflected = SSRRayMarcher::reflectDirection(view_dir, normal);
  EXPECT_NEAR(glm::length(reflected), 1.0f, 1e-4f);
}

// ============================================================================
// Roughness and Fallback
// ============================================================================

TEST(SSRFallbackTest, SmoothSurfaceUsesSSR) {
  float weight = SSRRayMarcher::computeFallbackWeight(0.0f, 1.0f, 1.0f);
  EXPECT_LT(weight, 0.5f); // Should favor SSR
}

TEST(SSRFallbackTest, RoughSurfaceUsesProbe) {
  float weight = SSRRayMarcher::computeFallbackWeight(0.8f, 0.5f, 1.0f);
  EXPECT_GT(weight, 0.5f); // Should favor probe
}

TEST(SSRFallbackTest, LowConfidenceUsesProbe) {
  float weight = SSRRayMarcher::computeFallbackWeight(0.1f, 0.1f, 1.0f);
  EXPECT_GT(weight, 0.5f); // Low confidence -> probe
}

TEST(SSRFallbackTest, ScreenEdgeFade) {
  // Center of screen -> no fade
  EXPECT_NEAR(SSRRayMarcher::screenEdgeFade({0.5f, 0.5f}), 1.0f, 0.01f);
  // Edge of screen -> strong fade
  EXPECT_LT(SSRRayMarcher::screenEdgeFade({0.01f, 0.5f}), 0.5f);
  EXPECT_LT(SSRRayMarcher::screenEdgeFade({0.5f, 0.99f}), 0.5f);
  // Corner -> very strong fade
  EXPECT_LT(SSRRayMarcher::screenEdgeFade({0.01f, 0.01f}), 0.1f);
}

TEST(SSRRoughnessTest, BlurRadius) {
  // Smooth surface -> no blur
  EXPECT_NEAR(SSRRayMarcher::roughnessBlurRadius(0.0f, 10.0f), 0.0f, 1e-5f);
  // Rougher surface -> more blur
  float smooth = SSRRayMarcher::roughnessBlurRadius(0.2f, 10.0f);
  float rough = SSRRayMarcher::roughnessBlurRadius(0.8f, 10.0f);
  EXPECT_LT(smooth, rough);
}

// ============================================================================
// Ray Marching (with synthetic depth buffer)
// ============================================================================

TEST(SSRRayMarchTest, HitFloorPlane) {
  // Create a simple depth buffer where bottom half has a "floor"
  const uint32_t W = 64, H = 64;
  std::vector<float> depth(W * H, 1.0f); // Far plane

  // Set bottom half to depth=0.5 (a floor surface)
  for (uint32_t y = H / 2; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      depth[y * W + x] = 0.5f;
    }
  }

  SSRConfig config;
  config.max_steps = 128;
  config.thickness = 0.1f;
  config.stride = 1.0f;
  SSRRayMarcher marcher(config);

  // Ray from top-center going down
  glm::vec3 origin(32.0f, 16.0f, 0.3f);
  glm::vec3 dir(0.0f, 1.0f, 0.005f); // Slightly into screen

  auto result = marcher.marchRay(origin, dir, {64, 64}, depth.data(), W, H);
  EXPECT_TRUE(result.hit);
  EXPECT_GT(result.confidence, 0.0f);
  EXPECT_GT(result.steps_taken, 0u);
}

TEST(SSRRayMarchTest, MissEmptyScene) {
  const uint32_t W = 32, H = 32;
  std::vector<float> depth(W * H, 1.0f); // Everything at far plane

  SSRConfig config;
  config.max_steps = 32;
  SSRRayMarcher marcher(config);

  glm::vec3 origin(16.0f, 16.0f, 0.5f);
  glm::vec3 dir(1.0f, 0.0f, 0.0f); // Horizontal

  auto result = marcher.marchRay(origin, dir, {32, 32}, depth.data(), W, H);
  // Should go off-screen without hitting
  EXPECT_FALSE(result.hit);
}

TEST(SSRRayMarchTest, NullBuffer) {
  SSRRayMarcher marcher;
  auto result = marcher.marchRay({0, 0, 0}, {1, 0, 0}, {100, 100}, nullptr, 0, 0);
  EXPECT_FALSE(result.hit);
}
