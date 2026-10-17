// Pure unit tests for BillboardRenderer math + atlas loading. No Vulkan
// device is created — we exercise the helpers and the JSON loader directly.
//
// Coverage:
//   - atlasSlotFromPixels: pixel rect → UV rect math, including a non-square
//     atlas and edge slots
//   - billboardFadeAlpha: monotonic decrease, endpoints, disabled case
//   - BillboardAtlas: roundtrip JSON, unknown-slot fallback, slot count

#include <gtest/gtest.h>
#include <hongtu/renderer/effects/billboard_renderer.hpp>

#include <cmath>

using hongtu::renderer::AtlasSlot;
using hongtu::renderer::BillboardAtlas;
using hongtu::renderer::atlasSlotFromPixels;
using hongtu::renderer::billboardFadeAlpha;

namespace {
constexpr float kEps = 1e-5f;
}

TEST(BillboardAtlasMathTest, AtlasSlotFromPixelsTopLeft) {
  // 256×256 atlas, slot at (0,0) sized 64×64. UVs should be (0..0.25)×(0..0.25).
  AtlasSlot s = atlasSlotFromPixels(0, 0, 64, 64, 256, 256);
  EXPECT_NEAR(s.uv_min.x, 0.0f,  kEps);
  EXPECT_NEAR(s.uv_min.y, 0.0f,  kEps);
  EXPECT_NEAR(s.uv_max.x, 0.25f, kEps);
  EXPECT_NEAR(s.uv_max.y, 0.25f, kEps);
}

TEST(BillboardAtlasMathTest, AtlasSlotFromPixelsBottomRight) {
  // Last slot of a 4×4 grid at (192,192) sized 64×64. UVs (0.75..1)×(0.75..1).
  AtlasSlot s = atlasSlotFromPixels(192, 192, 64, 64, 256, 256);
  EXPECT_NEAR(s.uv_min.x, 0.75f, kEps);
  EXPECT_NEAR(s.uv_min.y, 0.75f, kEps);
  EXPECT_NEAR(s.uv_max.x, 1.0f,  kEps);
  EXPECT_NEAR(s.uv_max.y, 1.0f,  kEps);
}

TEST(BillboardAtlasMathTest, AtlasSlotFromPixelsNonSquare) {
  // 512×128 atlas. A 32×128 slot at (256,0) covers a thin x sliver and the
  // full y range. UV math must use independent w/h denominators.
  AtlasSlot s = atlasSlotFromPixels(256, 0, 32, 128, 512, 128);
  EXPECT_NEAR(s.uv_min.x, 0.5f,    kEps);
  EXPECT_NEAR(s.uv_min.y, 0.0f,    kEps);
  EXPECT_NEAR(s.uv_max.x, 0.5625f, kEps);
  EXPECT_NEAR(s.uv_max.y, 1.0f,    kEps);
}

TEST(BillboardFadeTest, BelowNearIsFullAlpha) {
  EXPECT_NEAR(billboardFadeAlpha(50.0f, 100.0f, 200.0f), 1.0f, kEps);
}

TEST(BillboardFadeTest, AboveFarIsZero) {
  EXPECT_NEAR(billboardFadeAlpha(250.0f, 100.0f, 200.0f), 0.0f, kEps);
}

TEST(BillboardFadeTest, AtMidpointIsHalfish) {
  // smoothstep midpoint is exactly 0.5 → 1 - 0.5 = 0.5.
  EXPECT_NEAR(billboardFadeAlpha(150.0f, 100.0f, 200.0f), 0.5f, kEps);
}

TEST(BillboardFadeTest, MonotonicDecrease) {
  float prev = billboardFadeAlpha(0.0f, 100.0f, 200.0f);
  for (float d = 10.0f; d <= 250.0f; d += 10.0f) {
    float a = billboardFadeAlpha(d, 100.0f, 200.0f);
    EXPECT_LE(a, prev + kEps);
    prev = a;
  }
}

TEST(BillboardFadeTest, DisabledWhenFarLessThanNear) {
  // far ≤ near → fade off, full alpha returned.
  EXPECT_NEAR(billboardFadeAlpha(1000.0f, 100.0f, 100.0f), 1.0f, kEps);
  EXPECT_NEAR(billboardFadeAlpha(1000.0f, 100.0f,   0.0f), 1.0f, kEps);
}

TEST(BillboardAtlasLoaderTest, LoadFromInlineJson) {
  const std::string j = R"({
    "atlas": "park.png",
    "width": 256, "height": 256,
    "slots": {
      "warning":  {"x": 0,   "y": 0,   "w": 64, "h": 64},
      "danger":   {"x": 64,  "y": 0,   "w": 64, "h": 64},
      "exit":     {"x": 0,   "y": 128, "w": 64, "h": 64}
    }
  })";
  BillboardAtlas atlas;
  ASSERT_TRUE(atlas.loadFromJson(j));
  EXPECT_EQ(atlas.getWidth(),     256u);
  EXPECT_EQ(atlas.getHeight(),    256u);
  EXPECT_EQ(atlas.getSlotCount(), 3u);
  EXPECT_EQ(atlas.getAtlasPath(), "park.png");

  glm::vec4 uv = atlas.lookupUVRect("warning");
  EXPECT_NEAR(uv.x, 0.0f,  kEps);
  EXPECT_NEAR(uv.y, 0.0f,  kEps);
  EXPECT_NEAR(uv.z, 0.25f, kEps);
  EXPECT_NEAR(uv.w, 0.25f, kEps);

  uv = atlas.lookupUVRect("exit");
  EXPECT_NEAR(uv.x, 0.0f,  kEps);
  EXPECT_NEAR(uv.y, 0.5f,  kEps);
  EXPECT_NEAR(uv.z, 0.25f, kEps);
  EXPECT_NEAR(uv.w, 0.75f, kEps);
}

TEST(BillboardAtlasLoaderTest, UnknownSlotReturnsFullUv) {
  BillboardAtlas atlas;
  atlas.loadFromJson(R"({"atlas":"x.png","width":1,"height":1,"slots":{}})");
  glm::vec4 uv = atlas.lookupUVRect("does_not_exist");
  EXPECT_NEAR(uv.x, 0.0f, kEps);
  EXPECT_NEAR(uv.y, 0.0f, kEps);
  EXPECT_NEAR(uv.z, 1.0f, kEps);
  EXPECT_NEAR(uv.w, 1.0f, kEps);
}

TEST(BillboardAtlasLoaderTest, RejectsMalformedJson) {
  BillboardAtlas atlas;
  EXPECT_FALSE(atlas.loadFromJson("not a json"));
}

TEST(BillboardAtlasLoaderTest, RejectsMissingDimensions) {
  BillboardAtlas atlas;
  EXPECT_FALSE(atlas.loadFromJson(R"({"slots":{}})"));
}
