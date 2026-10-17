#include <gtest/gtest.h>
#include <hongtu/gis/tile/tile_quad_tree_selector.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace hongtu;
using namespace hongtu::gis;
using namespace hongtu::coordinate;

namespace {

// Camera at (lat=0, lon=0, alt=10 000 km) looking straight down.
glm::dvec3 highCameraEcef() {
  return geodeticToECEF(Geodetic{0.0, 0.0, 10'000'000.0}, WGS84);
}

struct CameraVP { glm::dmat4 view; glm::dmat4 proj; };

CameraVP highCameraVP() {
  glm::dvec3 eye = highCameraEcef();
  glm::dvec3 center(0.0, 0.0, 0.0);
  glm::dvec3 up(0.0, 0.0, 1.0);
  glm::dmat4 view = glm::lookAt(eye, center, up);
  glm::dmat4 proj = glm::perspective(glm::radians(45.0), 16.0/9.0,
                                     1.0, 100'000'000.0);
  return {view, proj};
}

}  // namespace

TEST(TileQuadTreeSelector, HighAltitudeSelectsLowZoom) {
  TileQuadTreeSelector::Config cfg;
  cfg.min_level = 1;
  cfg.max_level = 18;
  cfg.land_resolution = 1.0f;

  TileQuadTreeSelector sel(cfg);
  auto cam = highCameraVP();
  auto leaves = sel.select(highCameraEcef(), cam.view, cam.proj, 720,
                           /*query=*/nullptr);

  // At 10 000 km altitude, no leaf should be z > 5.
  for (const auto& info : leaves) {
    EXPECT_LE(info.coord.z, 5);
  }
  EXPECT_GT(leaves.size(), 0u);
}

TEST(TileQuadTreeSelector, LowerThresholdProducesMoreTiles) {
  TileQuadTreeSelector::Config cfg_loose;
  cfg_loose.land_resolution = 2.0f;
  TileQuadTreeSelector::Config cfg_tight;
  cfg_tight.land_resolution = 0.5f;

  TileQuadTreeSelector loose(cfg_loose), tight(cfg_tight);
  auto cam = highCameraVP();
  auto a = loose.select(highCameraEcef(), cam.view, cam.proj, 720, nullptr);
  auto b = tight.select(highCameraEcef(), cam.view, cam.proj, 720, nullptr);

  EXPECT_LT(a.size(), b.size());
}

TEST(TileQuadTreeSelector, RespectsMaxLevel) {
  TileQuadTreeSelector::Config cfg;
  cfg.max_level = 6;
  cfg.land_resolution = 0.01f;  // would otherwise drive deep refinement

  TileQuadTreeSelector sel(cfg);
  // Camera at sea level over (0,0).
  glm::dvec3 eye = geodeticToECEF(Geodetic{0.0, 0.0, 1'000.0}, WGS84);
  glm::dmat4 view = glm::lookAt(eye, glm::dvec3(0, 0, 0), glm::dvec3(0, 0, 1));
  glm::dmat4 proj = glm::perspective(glm::radians(45.0), 16.0/9.0, 1.0, 1'000'000.0);
  auto leaves = sel.select(eye, view, proj, 720, nullptr);
  for (const auto& info : leaves) {
    EXPECT_LE(info.coord.z, 6);
  }
}

TEST(TileQuadTreeSelector, RotatedCameraConsistentLOD) {
  // A camera at lat=45, lon=45 altitude with two different view orientations
  // (looking straight down vs slightly tilted) should extract the same
  // tan_fov_y from the projection matrix, independent of the view's V[1][1].
  // This test would have caught the bug where tan_fov_y was read from the
  // combined VP matrix instead of the projection matrix alone.
  TileQuadTreeSelector::Config cfg;
  cfg.land_resolution = 1.0f;
  TileQuadTreeSelector sel(cfg);

  // Same camera position at lat=45, lon=45, alt=1000 km.
  glm::dvec3 eye = geodeticToECEF(Geodetic{glm::radians(45.0), glm::radians(45.0), 1'000'000.0}, WGS84);
  glm::dvec3 center = geodeticToECEF(Geodetic{glm::radians(45.0), glm::radians(45.0), 0.0}, WGS84);

  // Two different "up" vectors produce different V[1][1] values in lookAt.
  glm::dvec3 up_radial = glm::normalize(eye);
  glm::dvec3 up_z(0.0, 0.0, 1.0);  // world Z — different V[1][1]

  glm::dmat4 view1 = glm::lookAt(eye, center, up_radial);
  glm::dmat4 view2 = glm::lookAt(eye, center, up_z);
  glm::dmat4 proj = glm::perspective(glm::radians(45.0), 16.0/9.0, 100.0, 100'000'000.0);

  auto leaves1 = sel.select(eye, view1, proj, 720, nullptr);
  auto leaves2 = sel.select(eye, view2, proj, 720, nullptr);

  // Same camera position + same projection → same tile selection.
  // The only difference is the "up" axis (camera roll), which must not
  // affect the LOD metric when tan_fov_y is read from projection alone.
  ASSERT_GT(leaves1.size(), 0u);
  ASSERT_GT(leaves2.size(), 0u);
  EXPECT_EQ(leaves1.size(), leaves2.size());
}

TEST(TileQuadTreeSelector, EverestWithDemHeightsStillSelectsTiles) {
  TileQuadTreeSelector::Config cfg;
  TileQuadTreeSelector sel(cfg);

  // Camera at 11.4 km over Everest, looking straight down at the summit.
  glm::dvec3 eye = geodeticToECEF(
      Geodetic{glm::radians(27.99), glm::radians(86.93), 11400.0}, WGS84);
  glm::dvec3 center = geodeticToECEF(
      Geodetic{glm::radians(27.99), glm::radians(86.93), 0.0}, WGS84);
  glm::dvec3 up = glm::normalize(eye);

  glm::dmat4 view = glm::lookAt(eye, center, up);
  const f64 R = 6378137.0;
  const f64 alt = 11400.0;
  const f64 horizon = std::sqrt(alt * (alt + 2.0 * R));
  glm::dmat4 proj = glm::perspective(glm::radians(60.0), 16.0/9.0,
                                     570.0, horizon * 2.0 + 200000.0);

  // HeightQuery that returns elevated heights for tiles near Everest
  // (lat 27..29 deg, lon 86..88 deg) — simulating DEM-loaded state.
  TileQuadTreeSelector::HeightQuery hq =
      [](const TileCoord& c, f64& min_h, f64& max_h) {
        auto b = c.toBounds();
        f64 lat = (b.south + b.north) * 0.5;
        f64 lon = (b.west + b.east) * 0.5;
        if (lat > glm::radians(27.0) && lat < glm::radians(29.0) &&
            lon > glm::radians(86.0) && lon < glm::radians(88.0)) {
          min_h = 5000.0;
          max_h = 8000.0;
        } else {
          min_h = 0.0;
          max_h = 0.0;
        }
      };

  auto leaves = sel.select(eye, view, proj, 720, &hq);
  EXPECT_GT(leaves.size(), 0u)
      << "Selector must not return empty when DEM elevates Everest-area OBBs — pre-fix regression";
}

TEST(TileQuadTreeSelector, LowAltitudeReturnsTilesNotEmpty) {
  TileQuadTreeSelector::Config cfg;
  TileQuadTreeSelector sel(cfg);

  // Camera at 285 km altitude over (lat=20°N, lon=105°E) looking straight down.
  glm::dvec3 eye = geodeticToECEF(
      Geodetic{glm::radians(20.0), glm::radians(105.0), 285000.0}, WGS84);
  glm::dvec3 center = geodeticToECEF(
      Geodetic{glm::radians(20.0), glm::radians(105.0), 0.0}, WGS84);
  glm::dvec3 up = glm::normalize(eye);

  glm::dmat4 view = glm::lookAt(eye, center, up);
  // Far plane mimics globe_camera.cpp: 2*horizon + 200 km.
  const f64 R = 6378137.0;
  const f64 alt = 285000.0;
  const f64 horizon = std::sqrt(alt * (alt + 2.0 * R));
  glm::dmat4 proj = glm::perspective(glm::radians(60.0), 16.0 / 9.0,
                                     14250.0, horizon * 2.0 + 200000.0);

  auto leaves = sel.select(eye, view, proj, 720, nullptr);
  EXPECT_GT(leaves.size(), 0u)
      << "Selector must not return empty at 285 km altitude — pre-fix bug returned 0";
}
