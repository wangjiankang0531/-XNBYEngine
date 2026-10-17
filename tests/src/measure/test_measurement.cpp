#include <gtest/gtest.h>

#include <hongtu/measure/measurement.hpp>
#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/geodetic.hpp>

#include <array>
#include <limits>

using namespace hongtu::measure;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::geodeticToECEF;
using hongtu::coordinate::enuFrameAt;

// ---- 本地模式 ----

TEST(MeasureLocal, DistancePolylineSumsSegments) {
  std::vector<glm::dvec3> pts = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
  auto r = compute(MeasureKind::Distance, pts, /*geo=*/false);
  ASSERT_TRUE(r.valid);
  ASSERT_EQ(r.seg_len.size(), 2u);
  EXPECT_NEAR(r.seg_len[0], 3.0, 1e-9);   // 水平距离
  EXPECT_NEAR(r.seg_len[1], 4.0, 1e-9);
  EXPECT_NEAR(r.total_len, 7.0, 1e-9);
}

TEST(MeasureLocal, DistanceTwoPointGivesSlant) {
  // 水平 3、垂直 4 → 地表(水平)3,斜距 5。
  std::vector<glm::dvec3> pts = {{0, 0, 0}, {3, 0, 4}};
  auto r = compute(MeasureKind::Distance, pts, false);
  EXPECT_NEAR(r.total_len, 3.0, 1e-9);   // 水平
  EXPECT_NEAR(r.slant_len, 5.0, 1e-9);   // 3-4-5
}

TEST(MeasureLocal, Height) {
  std::vector<glm::dvec3> pts = {{1, 1, 2}, {4, 5, 12}};
  auto r = compute(MeasureKind::Height, pts, false);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.height, 10.0, 1e-9);           // Δz
  EXPECT_NEAR(r.horizontal, 5.0, 1e-9);        // 3-4-5 水平
  EXPECT_NEAR(r.slant_len, std::sqrt(125.0), 1e-9);
}

TEST(MeasureLocal, AreaSquare) {
  // 10×10 方形 → 面积 100,周长 40。
  std::vector<glm::dvec3> pts = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  auto r = compute(MeasureKind::Area, pts, false);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.area, 100.0, 1e-6);
  EXPECT_NEAR(r.total_len, 40.0, 1e-6);
}

TEST(MeasureTerrainVolume, FlatCutAndFillAreExact) {
  const std::vector<glm::dvec2> footprint = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
  TerrainVolumeOptions options;
  options.reference_elevation_m = 3.0;
  options.sample_spacing_m = 3.0;
  const auto cut = computeTerrainVolume(footprint, options, [](double, double) { return 5.0; });
  ASSERT_TRUE(cut.valid);
  EXPECT_NEAR(cut.footprint_area_m2, 100.0, 1.0e-9);
  EXPECT_NEAR(cut.sampled_area_m2, 100.0, 1.0e-9);
  EXPECT_NEAR(cut.cut_volume_m3, 200.0, 1.0e-8);
  EXPECT_DOUBLE_EQ(cut.fill_volume_m3, 0.0);
  EXPECT_NEAR(cut.net_volume_m3, 200.0, 1.0e-8);

  options.reference_elevation_m = 7.0;
  const auto fill = computeTerrainVolume(footprint, options, [](double, double) { return 5.0; });
  ASSERT_TRUE(fill.valid);
  EXPECT_DOUBLE_EQ(fill.cut_volume_m3, 0.0);
  EXPECT_NEAR(fill.fill_volume_m3, 200.0, 1.0e-8);
  EXPECT_NEAR(fill.net_volume_m3, -200.0, 1.0e-8);
}

TEST(MeasureTerrainVolume, PlanarSlopeCutAndFillIntegrateExactly) {
  const std::vector<glm::dvec2> footprint = {{-5.0, 0.0}, {5.0, 0.0}, {5.0, 10.0}, {-5.0, 10.0}};
  TerrainVolumeOptions options;
  options.reference_elevation_m = 0.0;
  options.sample_spacing_m = 1.0;
  const auto result = computeTerrainVolume(footprint, options, [](double x, double) { return x; });
  ASSERT_TRUE(result.valid);
  EXPECT_NEAR(result.cut_volume_m3, 125.0, 1.0e-8);
  EXPECT_NEAR(result.fill_volume_m3, 125.0, 1.0e-8);
  EXPECT_NEAR(result.net_volume_m3, 0.0, 1.0e-8);
  EXPECT_EQ(result.grid_cell_count, 100u);
  EXPECT_EQ(result.contributing_cell_count, 100u);
}

TEST(MeasureTerrainVolume, ConcaveFootprintPreservesExactArea) {
  const std::vector<glm::dvec2> footprint = {
      {0.0, 0.0}, {8.0, 0.0}, {8.0, 3.0}, {3.0, 3.0}, {3.0, 8.0}, {0.0, 8.0}};
  TerrainVolumeOptions options;
  options.reference_elevation_m = 8.0;
  options.sample_spacing_m = 2.5;
  const auto result = computeTerrainVolume(footprint, options, [](double, double) { return 10.0; });
  ASSERT_TRUE(result.valid);
  EXPECT_NEAR(result.footprint_area_m2, 39.0, 1.0e-9);
  EXPECT_NEAR(result.sampled_area_m2, 39.0, 1.0e-9);
  EXPECT_NEAR(result.cut_volume_m3, 78.0, 1.0e-8);
}

TEST(MeasureTerrainVolume, HuilianWgs84EnuFootprintKeepsMetricVolume) {
  const Geodetic huilian_anchor = Geodetic::fromDegrees(22.747768545529, 114.599640461187, 18.0);
  const auto frame = enuFrameAt(huilian_anchor);
  const std::array<glm::dvec3, 4> local_points = {
      glm::dvec3{-50.0, -40.0, 0.0}, glm::dvec3{50.0, -40.0, 0.0},
      glm::dvec3{50.0, 40.0, 0.0}, glm::dvec3{-50.0, 40.0, 0.0}};
  std::vector<glm::dvec2> footprint;
  for (const glm::dvec3& point : local_points) {
    const glm::dvec3 roundtrip = frame.ecefToLocal(frame.localToEcef(point));
    footprint.emplace_back(roundtrip.x, roundtrip.y);
  }
  TerrainVolumeOptions options;
  options.reference_elevation_m = 20.0;
  options.sample_spacing_m = 12.0;
  const auto result = computeTerrainVolume(footprint, options, [](double, double) { return 23.5; });
  ASSERT_TRUE(result.valid);
  EXPECT_NEAR(result.footprint_area_m2, 8000.0, 1.0e-5);
  EXPECT_NEAR(result.cut_volume_m3, 28000.0, 1.0e-4);
}

TEST(MeasureTerrainVolume, RejectsNonFiniteSampleAndCellBudgetOverflow) {
  const std::vector<glm::dvec2> footprint = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
  TerrainVolumeOptions options;
  options.sample_spacing_m = 1.0;
  EXPECT_FALSE(computeTerrainVolume(footprint, options,
                                    [](double, double) { return std::numeric_limits<double>::quiet_NaN(); })
                   .valid);
  options.maximum_cells = 99;
  EXPECT_FALSE(computeTerrainVolume(footprint, options, [](double, double) { return 0.0; }).valid);
  options.maximum_cells = 1000;
  const double huge = std::numeric_limits<double>::max();
  const std::vector<glm::dvec2> overflowing_bounds = {
      {-huge, -1.0}, {huge, -1.0}, {huge, 1.0}, {-huge, 1.0}};
  EXPECT_FALSE(computeTerrainVolume(overflowing_bounds, options, [](double, double) { return 0.0; }).valid);
}

TEST(MeasurePoint, LocalXYZ) {
  std::vector<glm::dvec3> pts = {{1.5, -2.0, 3.0}};
  auto r = compute(MeasureKind::Point, pts, false);
  ASSERT_TRUE(r.valid);
  EXPECT_DOUBLE_EQ(r.local.x, 1.5);
  EXPECT_DOUBLE_EQ(r.local.z, 3.0);
}

// ---- 地理模式(ECEF)----

TEST(MeasureGeo, GroundDistanceMatchesHaversine) {
  // 北京 → 上海,约 1067 km(Haversine 平均半径)。容差 5 km。
  auto a = geodeticToECEF(Geodetic::fromDegrees(39.9042, 116.4074, 0.0));
  auto b = geodeticToECEF(Geodetic::fromDegrees(31.2304, 121.4737, 0.0));
  double d = groundDistance(a, b, /*geo=*/true);
  EXPECT_GT(d, 1.0e6);
  EXPECT_NEAR(d, 1.067e6, 5.0e3);
}

TEST(MeasureGeo, HeightUsesEllipsoidAlt) {
  auto lo = geodeticToECEF(Geodetic::fromDegrees(30.0, 120.0, 10.0));
  auto hi = geodeticToECEF(Geodetic::fromDegrees(30.0, 120.0, 110.0));
  auto r = compute(MeasureKind::Height, {lo, hi}, true);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.height, 100.0, 0.5);          // 椭球高差 ~100 m
  EXPECT_NEAR(r.horizontal, 0.0, 1.0);        // 同经纬,水平≈0
}

TEST(MeasureGeo, AreaSmallSquareApproxPlanar) {
  // 赤道附近 ~100m 见方:东/北各 ~0.001° 量级。面积应接近平面值。
  const double lat = 0.0, lon = 0.0;
  const double dN = 100.0 / 6378137.0 * 180.0 / 3.14159265358979;  // 100m → 度
  const double dE = dN;  // 赤道处经度间距≈纬度
  std::vector<glm::dvec3> pts = {
    geodeticToECEF(Geodetic::fromDegrees(lat,      lon,      0.0)),
    geodeticToECEF(Geodetic::fromDegrees(lat,      lon + dE, 0.0)),
    geodeticToECEF(Geodetic::fromDegrees(lat + dN, lon + dE, 0.0)),
    geodeticToECEF(Geodetic::fromDegrees(lat + dN, lon,      0.0)),
  };
  auto r = compute(MeasureKind::Area, pts, true);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.area, 100.0 * 100.0, 100.0 * 100.0 * 0.05);  // 100×100,5% 容差
}
