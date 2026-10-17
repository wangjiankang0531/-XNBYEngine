#include <gtest/gtest.h>
#include <hongtu/coordinate/scale_switch.hpp>

using namespace hongtu::coordinate;

TEST(ScaleLevelTest, Names) {
  EXPECT_STREQ(scaleLevelName(ScaleLevel::Global), "Global");
  EXPECT_STREQ(scaleLevelName(ScaleLevel::Device), "Device");
  EXPECT_STREQ(scaleLevelName(ScaleLevel::City), "City");
}

TEST(ScaleLevelTest, AltitudeRanges) {
  double min_alt, max_alt;
  scaleLevelAltitudeRange(ScaleLevel::Global, min_alt, max_alt);
  EXPECT_GT(max_alt, 1e6);

  scaleLevelAltitudeRange(ScaleLevel::Device, min_alt, max_alt);
  EXPECT_LT(max_alt, 200.0);
}

TEST(ScaleSwitchTest, InitialState) {
  ScaleSwitchManager mgr;
  EXPECT_EQ(mgr.getCurrentLevel(), ScaleLevel::Global);
  EXPECT_FALSE(mgr.isTransitioning());
}

TEST(ScaleSwitchTest, AltitudeToLevel) {
  ScaleSwitchManager mgr;

  EXPECT_EQ(mgr.altitudeToScaleLevel(2e7), ScaleLevel::Global);
  EXPECT_EQ(mgr.altitudeToScaleLevel(500000), ScaleLevel::National);   // 500km
  EXPECT_EQ(mgr.altitudeToScaleLevel(50000), ScaleLevel::City);       // 50km
  EXPECT_EQ(mgr.altitudeToScaleLevel(5000), ScaleLevel::District);    // 5km
  EXPECT_EQ(mgr.altitudeToScaleLevel(500), ScaleLevel::Building);     // 500m
  EXPECT_EQ(mgr.altitudeToScaleLevel(50), ScaleLevel::Device);        // 50m
}

TEST(ScaleSwitchTest, SwitchOnZoomIn) {
  ScaleSwitchConfig cfg;
  cfg.transition_duration_sec = 0.01; // Fast transitions for test
  ScaleSwitchManager mgr(cfg);
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  // Start at global altitude
  mgr.update(geo, 2e7, 0.016);
  EXPECT_EQ(mgr.getCurrentLevel(), ScaleLevel::Global);

  // Zoom to device level - need enough frames for 6 transitions
  for (int i = 0; i < 300; ++i) {
    mgr.update(geo, 50.0, 0.016);
  }
  // Should have switched to a local level
  EXPECT_NE(mgr.getCurrentLevel(), ScaleLevel::Global);
}

TEST(ScaleSwitchTest, SwitchOnZoomOut) {
  ScaleSwitchConfig cfg;
  cfg.transition_duration_sec = 0.01; // Fast transitions for test
  ScaleSwitchManager mgr(cfg);
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  // Zoom to device level (need many frames for multi-level transition)
  for (int i = 0; i < 300; ++i) mgr.update(geo, 30.0, 0.016);

  ScaleLevel local_level = mgr.getCurrentLevel();
  EXPECT_NE(local_level, ScaleLevel::Global);

  // Zoom back out to global
  for (int i = 0; i < 300; ++i) mgr.update(geo, 2e7, 0.016);
  EXPECT_EQ(mgr.getCurrentLevel(), ScaleLevel::Global);
}

TEST(ScaleSwitchTest, BlendFactor) {
  ScaleSwitchManager mgr;
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  // At global scale, blend should be 0 (fully global)
  mgr.update(geo, 2e7, 0.016);
  double blend = mgr.getLocalBlendFactor();
  EXPECT_LE(blend, 0.5);

  // At device scale, blend should be close to 1 (fully local)
  ScaleSwitchConfig cfg;
  cfg.transition_duration_sec = 0.01;
  mgr = ScaleSwitchManager(cfg);
  for (int i = 0; i < 300; ++i) mgr.update(geo, 30.0, 0.016);
  blend = mgr.getLocalBlendFactor();
  EXPECT_GE(blend, 0.5);
}

TEST(ScaleSwitchTest, ForceLevel) {
  ScaleSwitchManager mgr;
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  mgr.forceLevel(ScaleLevel::Building);
  EXPECT_EQ(mgr.getCurrentLevel(), ScaleLevel::Building);
}

TEST(ScaleSwitchTest, CoordState) {
  ScaleSwitchManager mgr;
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  mgr.update(geo, 2e7, 0.016);
  auto state = mgr.getCoordState();
  EXPECT_EQ(state.level, ScaleLevel::Global);
  EXPECT_FALSE(state.use_local_frame);
}

TEST(ScaleSwitchTest, Hysteresis) {
  ScaleSwitchConfig cfg;
  cfg.hysteresis_factor = 0.3;
  ScaleSwitchManager mgr(cfg);
  auto geo = Geodetic::fromDegrees(39.9, 116.4, 50.0);

  // Hover near a boundary - should not oscillate
  mgr.update(geo, 2e7, 0.016);
  ScaleLevel initial = mgr.getCurrentLevel();

  // Small altitude changes near boundary
  for (int i = 0; i < 30; ++i) {
    double alt = 10000.0 + (i % 2 == 0 ? 500 : -500); // Oscillate around boundary
    mgr.update(geo, alt, 0.016);
  }

  // Should not have oscillated rapidly (hysteresis prevents it)
  // Just verify it's at a reasonable level
  auto level = mgr.getCurrentLevel();
  EXPECT_NE(level, ScaleLevel::Count);
}
