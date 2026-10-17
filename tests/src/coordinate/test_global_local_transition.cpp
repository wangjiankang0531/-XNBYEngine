#include <gtest/gtest.h>
#include <hongtu/coordinate/global_local_transition.hpp>

#include <array>
#include <cmath>
#include <set>

using namespace hongtu::coordinate;

namespace {

GlobalLocalTransition makeHuilianTransition() {
  GlobalLocalTransitionConfig config;
  config.anchor = Geodetic::fromDegrees(22.747768545530, 114.599640461187, 112.0);
  return GlobalLocalTransition(config);
}

}  // namespace

TEST(GlobalLocalTransitionTest, EndpointsAreExactAndFinite) {
  const GlobalLocalTransition transition = makeHuilianTransition();
  const auto global = transition.sample(0.0);
  const auto local = transition.sample(1.0);

  EXPECT_DOUBLE_EQ(global.view_altitude_m, transition.config().global_altitude_m);
  EXPECT_NEAR(local.view_altitude_m, transition.config().local_altitude_m, 1.0e-9);
  EXPECT_EQ(global.scale_level, ScaleLevel::Global);
  EXPECT_EQ(local.scale_level, ScaleLevel::Building);
  EXPECT_LT(global.local_blend, 1.0e-12);
  EXPECT_GT(local.local_blend, 1.0 - 1.0e-12);
  EXPECT_TRUE(std::isfinite(global.camera_geodetic.lat));
  EXPECT_TRUE(std::isfinite(local.camera_geodetic.lon));
}

TEST(GlobalLocalTransitionTest, AltitudeAndBlendAreMonotonicAcrossAllRequiredScales) {
  const GlobalLocalTransition transition = makeHuilianTransition();
  double previous_altitude = std::numeric_limits<double>::infinity();
  double previous_blend = -1.0;
  std::set<ScaleLevel> levels;
  for (int i = 0; i <= 256; ++i) {
    const auto sample = transition.sample(static_cast<double>(i) / 256.0);
    EXPECT_LE(sample.view_altitude_m, previous_altitude + 1.0e-7);
    EXPECT_GE(sample.local_blend + 1.0e-12, previous_blend);
    EXPECT_GT(sample.far_clip_m, sample.near_clip_m);
    levels.insert(sample.scale_level);
    previous_altitude = sample.view_altitude_m;
    previous_blend = sample.local_blend;
  }
  EXPECT_TRUE(levels.contains(ScaleLevel::Global));
  EXPECT_TRUE(levels.contains(ScaleLevel::Continental));
  EXPECT_TRUE(levels.contains(ScaleLevel::National));
  EXPECT_TRUE(levels.contains(ScaleLevel::City));
  EXPECT_TRUE(levels.contains(ScaleLevel::District));
  EXPECT_TRUE(levels.contains(ScaleLevel::Building));
}

TEST(GlobalLocalTransitionTest, EcefAndEnuRoundTripRemainSubMicrometer) {
  const GlobalLocalTransition transition = makeHuilianTransition();
  for (double progress : std::array<double, 7>{0.0, 0.17, 0.33, 0.5, 0.67, 0.83, 1.0}) {
    const auto sample = transition.sample(progress);
    const glm::dvec3 reconstructed = transition.frame().localToEcef(
        transition.frame().ecefToLocal(sample.camera_ecef_m));
    EXPECT_LT(glm::length(reconstructed - sample.camera_ecef_m), 1.0e-6);
  }
}
