// PickingService unit tests. Pure host-side — no Vulkan. Uses LocalCamera +
// LocalEntity to build a deterministic flat-world scene, then verifies that
// pickAtScreen returns the right entity (or none) for various rays.

#include <gtest/gtest.h>
#include <hongtu/scene/picking/picking_service.hpp>
#include <hongtu/scene/local_entity.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/instancing/instance_buffer.hpp>

using hongtu::scene::PickingService;
using hongtu::scene::LocalEntity;
using hongtu::renderer::AABB;
using hongtu::renderer::LocalCamera;

namespace {

// Configure a camera looking at origin from (0, -20, 10). Centered screen
// pixel (640, 360) should ray-cast roughly toward origin.
//
// LocalCamera is non-copyable, so callers default-construct one and pass it
// here to configure.
void configureTestCamera(LocalCamera& c) {
  c.setScreenSize(1280.0f, 720.0f);
  c.setMode(LocalCamera::Mode::Orbit);
  c.lookAtTarget({0.0, 0.0, 0.0}, /*distance=*/22.0,
                 /*yaw=*/0.0, /*pitch=*/-0.5);
}

void configureEntityAt(LocalEntity& e, double x, double y, double z) {
  e.setPosition({x, y, z});
}

constexpr AABB kUnitCube{glm::vec3(-1.0f), glm::vec3(1.0f)};

}  // namespace

// =============================================================================
// Registration + lifecycle
// =============================================================================

TEST(PickingServiceTest, EmptyByDefault) {
  PickingService s;
  EXPECT_EQ(s.size(), 0u);
}

TEST(PickingServiceTest, RegisterAddsEntry) {
  PickingService s;
  LocalEntity e;
  s.registerEntity(&e, kUnitCube);
  EXPECT_EQ(s.size(), 1u);
}

TEST(PickingServiceTest, RegisterSameEntityReplacesNotDuplicates) {
  PickingService s;
  LocalEntity e;
  s.registerEntity(&e, kUnitCube);
  s.registerEntity(&e, AABB{glm::vec3(-2.0f), glm::vec3(2.0f)});
  EXPECT_EQ(s.size(), 1u);
}

TEST(PickingServiceTest, UnregisterRemoves) {
  PickingService s;
  LocalEntity a, b;
  s.registerEntity(&a, kUnitCube);
  s.registerEntity(&b, kUnitCube);
  s.unregisterEntity(&a);
  EXPECT_EQ(s.size(), 1u);
}

TEST(PickingServiceTest, ClearWipesAll) {
  PickingService s;
  LocalEntity a, b, c;
  s.registerEntity(&a, kUnitCube);
  s.registerEntity(&b, kUnitCube);
  s.registerEntity(&c, kUnitCube);
  s.clear();
  EXPECT_EQ(s.size(), 0u);
}

TEST(PickingServiceTest, RegisterNullEntityIsIgnored) {
  PickingService s;
  s.registerEntity(nullptr, kUnitCube);
  EXPECT_EQ(s.size(), 0u);
}

// =============================================================================
// Pick correctness
// =============================================================================

TEST(PickingServiceTest, PickAtScreenHitsEntityAtOrigin) {
  PickingService s;
  LocalEntity at_origin; configureEntityAt(at_origin, 0.0, 0.0, 0.0);
  s.registerEntity(&at_origin, kUnitCube);

  LocalCamera cam; configureTestCamera(cam);
  auto r = s.pickAtScreen(640.0, 360.0, cam, 1280.0, 720.0);

  ASSERT_TRUE(r.hit);
  EXPECT_EQ(r.entity, &at_origin);
  EXPECT_GT(r.ray_t, 0.0);
}

TEST(PickingServiceTest, PickAtScreenMissesEmpty) {
  PickingService s;
  LocalCamera cam; configureTestCamera(cam);
  auto r = s.pickAtScreen(640.0, 360.0, cam, 1280.0, 720.0);
  EXPECT_FALSE(r.hit);
}

TEST(PickingServiceTest, PickReturnsNearestOfTwo) {
  PickingService s;
  // Camera at (0, -20, 10) looking at origin → far entity behind origin (+Y)
  // should be occluded by near entity at origin.
  LocalEntity near_e; configureEntityAt(near_e, 0.0, 0.0, 0.0);
  LocalEntity far_e;  configureEntityAt(far_e,  0.0, 5.0, 0.0);
  s.registerEntity(&near_e, kUnitCube);
  s.registerEntity(&far_e,  kUnitCube);

  LocalCamera cam; configureTestCamera(cam);
  auto r = s.pickAtScreen(640.0, 360.0, cam, 1280.0, 720.0);

  ASSERT_TRUE(r.hit);
  EXPECT_EQ(r.entity, &near_e);
}

TEST(PickingServiceTest, PickGroundHitsZPlane) {
  PickingService s;
  LocalCamera cam; configureTestCamera(cam);   // pitched down, looking at z=0 plane
  auto r = s.pickGround(640.0, 360.0, cam, 1280.0, 720.0);

  ASSERT_TRUE(r.hit);
  // Hit should be near the orbit target (origin), z ≈ 0.
  EXPECT_NEAR(r.world_hit.z, 0.0, 1e-3);
  EXPECT_LT(std::abs(r.world_hit.x), 5.0);
  EXPECT_LT(std::abs(r.world_hit.y), 5.0);
}
