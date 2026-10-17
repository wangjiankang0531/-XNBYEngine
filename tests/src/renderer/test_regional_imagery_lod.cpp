#include <gtest/gtest.h>
#include <hongtu/renderer/globe/regional_imagery_lod.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

using hongtu::renderer::RegionalImageryLod;
using hongtu::renderer::RegionalImageryLodConfig;

TEST(RegionalImageryLodTest, DescendsFromGlobeThroughCoarseAndDetailTiles) {
  const RegionalImageryLod lod;

  const auto globe = lod.sample(12000000.0);
  EXPECT_DOUBLE_EQ(globe.coverage_weight, 0.0);
  EXPECT_DOUBLE_EQ(globe.detail_weight, 0.0);
  EXPECT_DOUBLE_EQ(globe.context_weight, 0.0);
  EXPECT_DOUBLE_EQ(globe.near_weight, 0.0);
  EXPECT_EQ(globe.effective_zoom, -1);

  const auto coarse = lod.sample(2000000.0);
  EXPECT_DOUBLE_EQ(coarse.coverage_weight, 1.0);
  EXPECT_DOUBLE_EQ(coarse.detail_weight, 0.0);
  EXPECT_EQ(coarse.effective_zoom, 3);

  const auto detail = lod.sample(750000.0);
  EXPECT_DOUBLE_EQ(detail.coverage_weight, 1.0);
  EXPECT_GT(detail.detail_weight, 0.5);
  EXPECT_LT(detail.context_weight, 0.5);
  EXPECT_EQ(detail.effective_zoom, 4);

  const auto context = lod.sample(250000.0);
  EXPECT_DOUBLE_EQ(context.context_weight, 1.0);
  EXPECT_LT(context.near_weight, 0.5);
  EXPECT_EQ(context.effective_zoom, 8);

  const auto near = lod.sample(50000.0);
  EXPECT_DOUBLE_EQ(near.near_weight, 1.0);
  EXPECT_EQ(near.effective_zoom, 10);
}

TEST(RegionalImageryLodTest, WeightsAreMonotonicAsAltitudeDecreases) {
  const RegionalImageryLod lod;
  double previous_coverage = 0.0;
  double previous_detail = 0.0;
  double previous_context = 0.0;
  double previous_near = 0.0;
  for (int i = 0; i <= 256; ++i) {
    const double t = static_cast<double>(i) / 256.0;
    const double altitude = std::exp(std::log(12000000.0) + (std::log(10000.0) - std::log(12000000.0)) * t);
    const auto sample = lod.sample(altitude);
    EXPECT_GE(sample.coverage_weight + 1.0e-12, previous_coverage);
    EXPECT_GE(sample.detail_weight + 1.0e-12, previous_detail);
    EXPECT_GE(sample.context_weight + 1.0e-12, previous_context);
    EXPECT_GE(sample.near_weight + 1.0e-12, previous_near);
    previous_coverage = sample.coverage_weight;
    previous_detail = sample.detail_weight;
    previous_context = sample.context_weight;
    previous_near = sample.near_weight;
  }
}

TEST(RegionalImageryLodTest, RejectsInvertedAltitudeRangesAndNonFiniteSamples) {
  RegionalImageryLodConfig invalid;
  invalid.detail_start_altitude_m = invalid.detail_full_altitude_m;
  // Braced construction is intentional: `RegionalImageryLod(invalid);` is
  // parsed as a default-constructed local declaration inside EXPECT_THROW and
  // therefore never passes the invalid configuration to the constructor.
  EXPECT_THROW(RegionalImageryLod{invalid}, std::invalid_argument);

  const RegionalImageryLod lod;
  EXPECT_THROW(lod.sample(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}
