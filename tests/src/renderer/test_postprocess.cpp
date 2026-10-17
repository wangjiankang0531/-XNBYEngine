#include <gtest/gtest.h>
#include <hongtu/renderer/postprocessing/post_process_pipeline.hpp>
#include <nlohmann/json.hpp>

using namespace hongtu::renderer;

// ============================================================================
// Pipeline Configuration
// ============================================================================

TEST(PostProcessPipelineTest, Empty) {
  PostProcessPipeline pipeline;
  EXPECT_EQ(pipeline.getEffectCount(), 0u);
}

TEST(PostProcessPipelineTest, AddEffects) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::Bloom);
  EXPECT_EQ(pipeline.getEffectCount(), 2u);
  EXPECT_EQ(pipeline.getEffect(0).type, PostEffectType::ToneMapping);
  EXPECT_EQ(pipeline.getEffect(1).type, PostEffectType::Bloom);
}

TEST(PostProcessPipelineTest, InsertEffect) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::FXAA);
  pipeline.insertEffect(1, PostEffectType::Bloom);

  EXPECT_EQ(pipeline.getEffectCount(), 3u);
  EXPECT_EQ(pipeline.getEffect(0).type, PostEffectType::ToneMapping);
  EXPECT_EQ(pipeline.getEffect(1).type, PostEffectType::Bloom);
  EXPECT_EQ(pipeline.getEffect(2).type, PostEffectType::FXAA);
}

TEST(PostProcessPipelineTest, RemoveEffect) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::Bloom);
  pipeline.addEffect(PostEffectType::FXAA);

  pipeline.removeEffect(1); // Remove Bloom
  EXPECT_EQ(pipeline.getEffectCount(), 2u);
  EXPECT_EQ(pipeline.getEffect(0).type, PostEffectType::ToneMapping);
  EXPECT_EQ(pipeline.getEffect(1).type, PostEffectType::FXAA);
}

TEST(PostProcessPipelineTest, MoveEffect) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::Bloom);
  pipeline.addEffect(PostEffectType::FXAA);

  pipeline.moveEffect(2, 0); // Move FXAA to front
  EXPECT_EQ(pipeline.getEffect(0).type, PostEffectType::FXAA);
}

TEST(PostProcessPipelineTest, EnableDisable) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::Bloom);

  EXPECT_TRUE(pipeline.isEffectEnabled(0));
  pipeline.setEffectEnabled(0, false);
  EXPECT_FALSE(pipeline.isEffectEnabled(0));

  auto enabled = pipeline.getEnabledEffects();
  EXPECT_EQ(enabled.size(), 1u);
  EXPECT_EQ(enabled[0], PostEffectType::Bloom);
}

TEST(PostProcessPipelineTest, FindEffect) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::Bloom);

  EXPECT_EQ(pipeline.findEffect(PostEffectType::Bloom), 1);
  EXPECT_EQ(pipeline.findEffect(PostEffectType::Vignette), -1);
}

TEST(PostProcessPipelineTest, DefaultPipeline) {
  auto pipeline = PostProcessPipeline::createDefault();
  EXPECT_GT(pipeline.getEffectCount(), 0u);

  // Should have tone mapping
  EXPECT_GE(pipeline.findEffect(PostEffectType::ToneMapping), 0);
}

TEST(PostProcessPipelineTest, Clear) {
  auto pipeline = PostProcessPipeline::createDefault();
  pipeline.clear();
  EXPECT_EQ(pipeline.getEffectCount(), 0u);
}

// ============================================================================
// Serialization
// ============================================================================

TEST(PostProcessPipelineTest, JsonRoundtrip) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::Bloom);
  auto& bloom = pipeline.getEffect(0);
  bloom.bloom.threshold = 1.5f;
  bloom.bloom.intensity = 0.8f;

  pipeline.addEffect(PostEffectType::ToneMapping);
  auto& tm = pipeline.getEffect(1);
  tm.tone_mapping.exposure = 2.0f;
  tm.enabled = false;

  auto json = pipeline.toJson();

  PostProcessPipeline restored;
  restored.fromJson(json);

  EXPECT_EQ(restored.getEffectCount(), 2u);
  EXPECT_EQ(restored.getEffect(0).type, PostEffectType::Bloom);
  EXPECT_FLOAT_EQ(restored.getEffect(0).bloom.threshold, 1.5f);
  EXPECT_FLOAT_EQ(restored.getEffect(0).bloom.intensity, 0.8f);

  EXPECT_EQ(restored.getEffect(1).type, PostEffectType::ToneMapping);
  EXPECT_FLOAT_EQ(restored.getEffect(1).tone_mapping.exposure, 2.0f);
  EXPECT_FALSE(restored.getEffect(1).enabled);
}

TEST(PostProcessPipelineTest, EffectTypeNames) {
  EXPECT_STREQ(postEffectTypeName(PostEffectType::ToneMapping), "ToneMapping");
  EXPECT_STREQ(postEffectTypeName(PostEffectType::Bloom), "Bloom");
  EXPECT_STREQ(postEffectTypeName(PostEffectType::FXAA), "FXAA");

  EXPECT_EQ(postEffectTypeFromName("Bloom"), PostEffectType::Bloom);
  EXPECT_EQ(postEffectTypeFromName("FXAA"), PostEffectType::FXAA);
}

// ============================================================================
// Tone Mapping CPU Reference
// ============================================================================

TEST(ToneMappingTest, ACESBlack) {
  auto result = acesToneMap({0, 0, 0});
  EXPECT_NEAR(result.r, 0.0f, 0.01f);
  EXPECT_NEAR(result.g, 0.0f, 0.01f);
  EXPECT_NEAR(result.b, 0.0f, 0.01f);
}

TEST(ToneMappingTest, ACESClamped) {
  auto result = acesToneMap({100, 100, 100});
  EXPECT_LE(result.r, 1.0f);
  EXPECT_LE(result.g, 1.0f);
  EXPECT_LE(result.b, 1.0f);
}

TEST(ToneMappingTest, ACESMonotonic) {
  // Brighter input should produce brighter output
  auto dark = acesToneMap({0.1f, 0.1f, 0.1f});
  auto mid = acesToneMap({1.0f, 1.0f, 1.0f});
  auto bright = acesToneMap({5.0f, 5.0f, 5.0f});

  EXPECT_LT(dark.r, mid.r);
  EXPECT_LT(mid.r, bright.r);
}

TEST(ToneMappingTest, ACESExposure) {
  auto normal = acesToneMap({1, 1, 1}, 1.0f);
  auto bright = acesToneMap({1, 1, 1}, 2.0f);
  EXPECT_LT(normal.r, bright.r);
}

TEST(ToneMappingTest, ReinhardBasic) {
  auto result = reinhardToneMap({1, 1, 1});
  EXPECT_GT(result.r, 0.0f);
  EXPECT_LT(result.r, 1.0f);

  auto black = reinhardToneMap({0, 0, 0});
  EXPECT_NEAR(black.r, 0.0f, 0.01f);
}

// ============================================================================
// Effect Parameters
// ============================================================================

TEST(PostProcessParamsTest, ModifyBloomParams) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::Bloom);

  auto& bloom = pipeline.getEffect(0).bloom;
  bloom.threshold = 2.0f;
  bloom.intensity = 0.7f;
  bloom.radius = 10.0f;
  bloom.mip_levels = 6;

  EXPECT_FLOAT_EQ(pipeline.getEffect(0).bloom.threshold, 2.0f);
  EXPECT_EQ(pipeline.getEffect(0).bloom.mip_levels, 6u);
}

TEST(PostProcessParamsTest, ModifyColorGrading) {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::ColorGrading);

  auto& cg = pipeline.getEffect(0).color_grading;
  cg.saturation = 1.5f;
  cg.contrast = 1.2f;
  cg.temperature = 0.1f;

  EXPECT_FLOAT_EQ(pipeline.getEffect(0).color_grading.saturation, 1.5f);
}
