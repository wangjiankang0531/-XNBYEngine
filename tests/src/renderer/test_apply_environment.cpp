#include <gtest/gtest.h>

#include "hongtu/renderer/environment/apply_environment.hpp"

using namespace hongtu::renderer;

TEST(ApplyEnvironment, TodOverridesManualSun) {
  EnvironmentSettings s;
  s.tod_enabled = true;
  s.tod_hours   = 12.0f;

  ForwardSceneRenderer::FrameParams fp{};
  HdrComposeTarget::ComposeParams   cp{};
  applyEnvironment(s, 0.0f, fp, cp);

  EXPECT_GT(fp.sun_intensity, 0.0f);
  EXPECT_EQ(fp.skybox_mode, 2);
}

TEST(ApplyEnvironment, PostFxFlowToCompose) {
  EnvironmentSettings s;
  s.godray_intensity  = 0.8f;
  s.grade_saturation  = 1.6f;
  s.vignette_strength = 0.5f;

  ForwardSceneRenderer::FrameParams fp{};
  HdrComposeTarget::ComposeParams   cp{};
  applyEnvironment(s, 0.0f, fp, cp);

  EXPECT_FLOAT_EQ(cp.godray_intensity, 0.8f);
  EXPECT_FLOAT_EQ(cp.grade_saturation, 1.6f);
  EXPECT_FLOAT_EQ(cp.vignette, 0.5f);
}

TEST(ApplyEnvironment, DisabledGatesZeroOut) {
  EnvironmentSettings s;
  s.ibl_enabled      = false;
  s.bloom_enabled    = false;
  s.sun_casts_shadows = false;
  s.bloom_intensity  = 0.6f;
  s.shadow_strength  = 0.5f;

  ForwardSceneRenderer::FrameParams fp{};
  HdrComposeTarget::ComposeParams   cp{};
  applyEnvironment(s, 0.0f, fp, cp);

  EXPECT_FLOAT_EQ(fp.ibl_diffuse_scale, 0.0f);
  EXPECT_FLOAT_EQ(cp.bloom_intensity, 0.0f);
  EXPECT_FLOAT_EQ(fp.shadow_strength, 0.0f);
}
