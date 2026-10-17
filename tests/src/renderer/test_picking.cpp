// Picking tests:
//   - ray_caster pure helpers: screenToWorldRay / AABB / sphere / plane /
//     entity-AABB-with-model-matrix
//   - LocalCamera::pickWorld against z=0 ground (sample center + corner)
//   - GlobeCamera::pickWorld against WGS84 sphere (looking straight down)

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>

#include <cmath>
#include <numbers>

using hongtu::renderer::AABB;
using hongtu::renderer::GlobeCamera;
using hongtu::renderer::intersectRayAABB;
using hongtu::renderer::intersectRayPlane;
using hongtu::renderer::intersectRaySphere;
using hongtu::renderer::LocalCamera;
using hongtu::renderer::pickEntityAABB;
using hongtu::renderer::PickHit;
using hongtu::renderer::screenToWorldRay;
using hongtu::renderer::WorldRay;

namespace {
  constexpr double kEps = 1e-6;
}

// =============================================================================
// AABB
// =============================================================================

TEST(RayCasterTest, AabbHitFromOutsideAlongXAxis) {
  // Box at (1..2, -1..1, -1..1). Ray from origin pointing +X. First face is at x=1.
  WorldRay r{{0, 0, 0}, {1, 0, 0}};
  auto t = intersectRayAABB(r, {1, -1, -1}, {2, 1, 1});
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 1.0, kEps);
}

TEST(RayCasterTest, AabbMissReturnsNullopt) {
  // Ray below the box never enters it.
  WorldRay r{{0, 0, -5}, {1, 0, 0}};
  auto t = intersectRayAABB(r, {1, -1, -1}, {2, 1, 1});
  EXPECT_FALSE(t.has_value());
}

TEST(RayCasterTest, AabbBehindOriginReturnsNullopt) {
  // Box at (1..2, -1..1, -1..1). Ray pointing −X — box is behind.
  WorldRay r{{5, 0, 0}, {1, 0, 0}};
  auto t = intersectRayAABB(r, {1, -1, -1}, {2, 1, 1});
  EXPECT_FALSE(t.has_value());
}

TEST(RayCasterTest, AabbOriginInsideReturnsExit) {
  // Ray starting inside the box, moving +X. Should return the exit `t`.
  WorldRay r{{1.5, 0, 0}, {1, 0, 0}};
  auto t = intersectRayAABB(r, {1, -1, -1}, {2, 1, 1});
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 0.5, kEps);
}

// =============================================================================
// Plane
// =============================================================================

TEST(RayCasterTest, PlaneHitForRayPointingDown) {
  // Camera 10 m above z=0 plane, looking straight down.
  WorldRay r{{0, 0, 10}, {0, 0, -1}};
  auto t = intersectRayPlane(r, {0, 0, 0}, {0, 0, 1});
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 10.0, kEps);
}

TEST(RayCasterTest, PlaneMissWhenParallel) {
  WorldRay r{{0, 0, 10}, {1, 0, 0}};
  auto t = intersectRayPlane(r, {0, 0, 0}, {0, 0, 1});
  EXPECT_FALSE(t.has_value());
}

TEST(RayCasterTest, PlaneMissWhenLookingAway) {
  WorldRay r{{0, 0, 10}, {0, 0, 1}}; // looking up, plane below
  auto t = intersectRayPlane(r, {0, 0, 0}, {0, 0, 1});
  EXPECT_FALSE(t.has_value());
}

// =============================================================================
// Sphere
// =============================================================================

TEST(RayCasterTest, SphereHitFromOutside) {
  // Sphere radius 1 at origin. Ray from (-5,0,0) pointing +X enters at x=-1.
  WorldRay r{{-5, 0, 0}, {1, 0, 0}};
  auto t = intersectRaySphere(r, {0, 0, 0}, 1.0);
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 4.0, kEps);
}

TEST(RayCasterTest, SphereMissAboveTangent) {
  WorldRay r{{-5, 2, 0}, {1, 0, 0}}; // misses radius-1 sphere
  auto t = intersectRaySphere(r, {0, 0, 0}, 1.0);
  EXPECT_FALSE(t.has_value());
}

// =============================================================================
// Entity AABB (with model matrix)
// =============================================================================

TEST(RayCasterTest, EntityAABBNoTransformIsSameAsPlainAABB) {
  WorldRay r{{0, 0, 0}, {1, 0, 0}};
  glm::dmat4 identity(1.0);
  auto t = pickEntityAABB(r, identity, {1, -1, -1}, {2, 1, 1});
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 1.0, kEps);
}

TEST(RayCasterTest, EntityAABBWithTranslation) {
  // Shift the entity +10 along X. Local box (-1..1, -1..1, -1..1).
  // World box becomes 9..11, ±1, ±1. Ray from origin +X enters at x=9.
  WorldRay r{{0, 0, 0}, {1, 0, 0}};
  glm::dmat4 m(1.0);
  m[3] = glm::dvec4(10.0, 0.0, 0.0, 1.0);
  auto t = pickEntityAABB(r, m, {-1, -1, -1}, {1, 1, 1});
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 9.0, 1e-4); // f64 inverse + roundtrip — give a bit of slop
}

// =============================================================================
// ray–triangle / ray–mesh (场景表面拾取)
// =============================================================================

TEST(RayCasterTest, RayTriangleHitsInteriorAndMissesOutside) {
  using hongtu::renderer::intersectRayTriangle;
  // 三角形在 z=5 平面,顶点 (0,0,5)(4,0,5)(0,4,5);从原点朝 +Z 的射线
  // 命中内部点 (1,1) → t=5。
  glm::dvec3 a{0, 0, 5}, b{4, 0, 5}, c{0, 4, 5};
  WorldRay hit{{1, 1, 0}, {0, 0, 1}};
  auto t = intersectRayTriangle(hit, a, b, c);
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 5.0, 1e-9);
  // 三角形外(3,3 之和 >4)不命中。
  WorldRay miss{{3, 3, 0}, {0, 0, 1}};
  EXPECT_FALSE(intersectRayTriangle(miss, a, b, c).has_value());
  // 背向(朝 -Z)不命中。
  WorldRay back{{1, 1, 0}, {0, 0, -1}};
  EXPECT_FALSE(intersectRayTriangle(back, a, b, c).has_value());
}

TEST(RayCasterTest, RayMeshHitsNearestSurfaceThroughModelMatrix) {
  using hongtu::renderer::intersectRayMesh;
  // 单位四边形(两三角)在局部 z=0;模型矩阵抬到 z=10。自上而下的射线
  // 应命中世界 z=10(沿原射线 t≈40)。
  std::vector<glm::vec3> pos = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
  std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
  glm::dmat4 M(1.0);
  M[3] = glm::dvec4(0, 0, 10, 1.0);
  WorldRay down{{0, 0, 50}, {0, 0, -1}};
  auto t = intersectRayMesh(down, pos, idx, M);
  ASSERT_TRUE(t.has_value());
  EXPECT_NEAR(*t, 40.0, 1e-6);
  const glm::dvec3 p = down.origin + down.direction * (*t);
  EXPECT_NEAR(p.z, 10.0, 1e-6);
  // 偏到四边形外(x=5)不命中。
  WorldRay off{{5, 0, 50}, {0, 0, -1}};
  EXPECT_FALSE(intersectRayMesh(off, pos, idx, M).has_value());
}

TEST(RayCasterTest, RayMeshRespectsUniformScale) {
  using hongtu::renderer::intersectRayMesh;
  // 局部四边形放大 3×:x=2 仍在范围内(世界半宽 3),命中。
  std::vector<glm::vec3> pos = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
  std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
  glm::dmat4 M = glm::scale(glm::dmat4(1.0), glm::dvec3(3.0));
  WorldRay down{{2, 0, 50}, {0, 0, -1}};
  ASSERT_TRUE(intersectRayMesh(down, pos, idx, M).has_value());
}

// =============================================================================
// screenToWorldRay roundtrip
// =============================================================================

TEST(RayCasterTest, ScreenToWorldRayCenterPixelLooksAlongViewDir) {
  // Construct a simple identity-projection identity-view setup. Center
  // pixel of a 100×100 viewport unprojects through identity → ray along +Z
  // (Vulkan: near z=0 → far z=1, world point goes from (0,0,0) → (0,0,?)
  // since the matrix is identity).
  glm::dmat4 inv_vp(1.0);
  WorldRay r = screenToWorldRay(50.0, 50.0, 100.0, 100.0, inv_vp);
  // Identity matrix maps NDC (0,0,0,1) → world (0,0,0); NDC (0,0,1,1) →
  // (0,0,1). So the ray direction is +Z.
  EXPECT_NEAR(r.origin.x, 0.0, kEps);
  EXPECT_NEAR(r.origin.y, 0.0, kEps);
  EXPECT_NEAR(r.direction.x, 0.0, kEps);
  EXPECT_NEAR(r.direction.y, 0.0, kEps);
  EXPECT_NEAR(std::abs(r.direction.z), 1.0, kEps);
}

// =============================================================================
// LocalCamera::pickWorld
// =============================================================================

TEST(LocalCameraPickTest, ClickCenterHitsGroundDirectlyBelow) {
  // Camera at (0,0,10) orbit-looking at (0,0,0). Click center of a 100×100
  // viewport → ray hits ground at the origin.
  LocalCamera cam;
  cam.setScreenSize(100, 100);
  cam.lookAtTarget({0, 0, 0},
                   10.0,
                   /*yaw=*/0.0,
                   /*pitch=*/-std::numbers::pi_v<double> / 2.0);

  auto hit = cam.pickWorld(50.0, 50.0, 100.0, 100.0);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.x, 0.0, 0.01);
  EXPECT_NEAR(hit->point.y, 0.0, 0.01);
  EXPECT_NEAR(hit->point.z, 0.0, 0.01);
}

TEST(LocalCameraPickTest, ClickOffCenterShiftsHitAwayFromOrigin) {
  // We don't pin the axis convention (depends on camera basis / Y-flip);
  // just require that a far-right pixel hits noticeably off-origin in the
  // ground plane, while remaining on z=0.
  LocalCamera cam;
  cam.setScreenSize(100, 100);
  cam.lookAtTarget({0, 0, 0}, 10.0, 0.0, -std::numbers::pi_v<double> / 2.0);

  // 25 px off-center is well outside the floating-point noise floor for
  // a 60° default FoV at 10m altitude (~~5 m on ground at fov-half-tan).
  auto hit = cam.pickWorld(75.0, 50.0, 100.0, 100.0);
  ASSERT_TRUE(hit.has_value());
  double xy_dist = std::sqrt(hit->point.x * hit->point.x + hit->point.y * hit->point.y);
  EXPECT_GT(xy_dist, 1.0); // at least 1 m off origin
  EXPECT_NEAR(hit->point.z, 0.0, 0.01);
}

// =============================================================================
// GlobeCamera::pickWorld
// =============================================================================

TEST(GlobeCameraPickTest, ClickCenterHitsSphereDirectlyBelow) {
  // Look straight down at lat=0, lon=0 from 1000 km. Center pixel pick
  // should land on the equator at (a, 0, 0) where a = WGS84 equatorial
  // radius. pickWorld now intersects the WGS84 ELLIPSOID (not a mean-radius
  // sphere), so at the equator the geocentric radius is the equatorial
  // semi-axis 6 378 137 m — NOT the mean radius 6 371 008 m. Matching the
  // ellipsoid is what makes click placement land under the cursor at all
  // latitudes (see globe_camera.cpp pickGlobe).
  GlobeCamera cam;
  cam.setScreenSize(1280, 720);
  cam.lookAt(hongtu::coordinate::Geodetic::fromDegrees(0.0, 0.0, 0.0), 1000000.0, 0.0, -90.0);
  cam.update(0.0f);

  auto hit = cam.pickWorld(640.0, 360.0, 1280.0, 720.0);
  ASSERT_TRUE(hit.has_value());
  double r = std::sqrt(hit->point.x * hit->point.x + hit->point.y * hit->point.y + hit->point.z * hit->point.z);
  EXPECT_NEAR(r, 6378137.0, 100.0); // within 100m of equatorial radius
}
