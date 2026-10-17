#include <gtest/gtest.h>
#include <hongtu/coordinate/anchor_manager.hpp>
#include <nlohmann/json.hpp>
#include <cmath>

using namespace hongtu::coordinate;

static constexpr double METER_TOL = 0.001; // 1mm tolerance

// ============================================================================
// CRUD Tests
// ============================================================================

TEST(AnchorManagerTest, AddAndGet) {
  AnchorManager mgr;
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  ASSERT_TRUE(mgr.addAnchor("a1", "Beijing", geo, 0.0));
  EXPECT_EQ(mgr.size(), 1u);

  auto* anchor = mgr.getAnchor("a1");
  ASSERT_NE(anchor, nullptr);
  EXPECT_EQ(anchor->name, "Beijing");
}

TEST(AnchorManagerTest, RemoveAnchor) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Test", Geodetic::fromDegrees(0, 0, 0));
  EXPECT_TRUE(mgr.removeAnchor("a1"));
  EXPECT_FALSE(mgr.removeAnchor("a1")); // Already removed
  EXPECT_EQ(mgr.size(), 0u);
}

TEST(AnchorManagerTest, EmptyIdRejected) {
  AnchorManager mgr;
  EXPECT_FALSE(mgr.addAnchor("", "Test", Geodetic::fromDegrees(0, 0, 0)));
}

TEST(AnchorManagerTest, UpdateExisting) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Old Name", Geodetic::fromDegrees(0, 0, 0));
  mgr.addAnchor("a1", "New Name", Geodetic::fromDegrees(1, 1, 0));
  EXPECT_EQ(mgr.size(), 1u);
  EXPECT_EQ(mgr.getAnchor("a1")->name, "New Name");
}

// ============================================================================
// Conversion: Origin returns zero
// ============================================================================

TEST(AnchorConversionTest, OriginIsZero) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Test", Geodetic::fromDegrees(39.9042, 116.4074, 50.0));

  glm::dvec3 ecef;
  ASSERT_TRUE(mgr.localToECEF({0, 0, 0}, "a1", ecef));

  glm::dvec3 local;
  ASSERT_TRUE(mgr.ecefToLocal(ecef, "a1", local));
  EXPECT_NEAR(local.x, 0.0, 1e-6);
  EXPECT_NEAR(local.y, 0.0, 1e-6);
  EXPECT_NEAR(local.z, 0.0, 1e-6);
}

// ============================================================================
// Same-anchor roundtrip
// ============================================================================

TEST(AnchorConversionTest, SameAnchorRoundtrip) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Beijing", Geodetic::fromDegrees(39.9042, 116.4074, 50.0));

  glm::dvec3 local_in(100.0, 200.0, 50.0);
  glm::dvec3 result;

  ASSERT_TRUE(mgr.convertBetweenAnchors(local_in, "a1", "a1", result));
  EXPECT_NEAR(result.x, local_in.x, METER_TOL);
  EXPECT_NEAR(result.y, local_in.y, METER_TOL);
  EXPECT_NEAR(result.z, local_in.z, METER_TOL);
}

// ============================================================================
// Inter-anchor conversion transitivity: A->B->C == A->C
// ============================================================================

TEST(AnchorConversionTest, Transitivity) {
  AnchorManager mgr;
  // Three anchors in Beijing area
  mgr.addAnchor("a", "Station A", Geodetic::fromDegrees(39.90, 116.40, 50.0));
  mgr.addAnchor("b", "Station B", Geodetic::fromDegrees(39.95, 116.45, 60.0));
  mgr.addAnchor("c", "Station C", Geodetic::fromDegrees(39.85, 116.35, 40.0));

  glm::dvec3 local_a(100.0, 200.0, 30.0);
  glm::dvec3 via_b, direct_c;

  // A -> B -> C
  ASSERT_TRUE(mgr.convertBetweenAnchors(local_a, "a", "b", via_b));
  ASSERT_TRUE(mgr.convertBetweenAnchors(via_b, "b", "c", direct_c));

  // A -> C directly
  glm::dvec3 direct;
  ASSERT_TRUE(mgr.convertBetweenAnchors(local_a, "a", "c", direct));

  EXPECT_NEAR(direct_c.x, direct.x, METER_TOL);
  EXPECT_NEAR(direct_c.y, direct.y, METER_TOL);
  EXPECT_NEAR(direct_c.z, direct.z, METER_TOL);
}

TEST(AnchorConversionTest, TransitivityWith50Anchors) {
  AnchorManager mgr;

  // Create 50 anchors spread across China
  for (int i = 0; i < 50; ++i) {
    double lat = 20.0 + (i % 10) * 3.0;
    double lon = 80.0 + (i / 10) * 10.0;
    double alt = static_cast<double>(i * 100);
    double azimuth = static_cast<double>(i * 7) * 3.14159265358979323846 / 180.0;
    mgr.addAnchor("anchor_" + std::to_string(i), "Anchor " + std::to_string(i),
                   Geodetic::fromDegrees(lat, lon, alt), azimuth);
  }
  EXPECT_EQ(mgr.size(), 50u);

  // Test transitivity for random anchor chains
  glm::dvec3 origin(500.0, 300.0, 100.0);

  for (int i = 0; i < 10; ++i) {
    std::string a = "anchor_" + std::to_string(i * 5);
    std::string b = "anchor_" + std::to_string((i * 5 + 13) % 50);
    std::string c = "anchor_" + std::to_string((i * 5 + 29) % 50);

    glm::dvec3 ab, abc, ac;
    ASSERT_TRUE(mgr.convertBetweenAnchors(origin, a, b, ab));
    ASSERT_TRUE(mgr.convertBetweenAnchors(ab, b, c, abc));
    ASSERT_TRUE(mgr.convertBetweenAnchors(origin, a, c, ac));

    EXPECT_NEAR(abc.x, ac.x, METER_TOL) << "Chain " << a << "->" << b << "->" << c;
    EXPECT_NEAR(abc.y, ac.y, METER_TOL);
    EXPECT_NEAR(abc.z, ac.z, METER_TOL);
  }
}

// ============================================================================
// Azimuth rotation
// ============================================================================

TEST(AnchorConversionTest, AzimuthRotation) {
  AnchorManager mgr;
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);

  // Anchor with no azimuth
  mgr.addAnchor("no_az", "No Azimuth", geo, 0.0);
  // Anchor with 90-degree azimuth
  constexpr double PI = 3.14159265358979323846;
  mgr.addAnchor("az90", "Azimuth 90", geo, PI / 2.0);

  // A point 100m east in the no-azimuth frame
  glm::dvec3 east_100(100.0, 0.0, 0.0);

  // Convert to ECEF and back through azimuth=90 frame
  glm::dvec3 ecef;
  mgr.localToECEF(east_100, "no_az", ecef);

  glm::dvec3 in_az90;
  mgr.ecefToLocal(ecef, "az90", in_az90);

  // In the rotated frame, what was east should now be ~north
  EXPECT_NEAR(in_az90.x, 0.0, 1.0);   // Should be ~0 in rotated X
  EXPECT_NEAR(in_az90.y, -100.0, 1.0); // Should be ~-100 in rotated Y
}

// ============================================================================
// Nearest anchor
// ============================================================================

TEST(AnchorManagerTest, FindNearestAnchor) {
  AnchorManager mgr;
  mgr.addAnchor("beijing", "Beijing", Geodetic::fromDegrees(39.9042, 116.4074, 50.0));
  mgr.addAnchor("shanghai", "Shanghai", Geodetic::fromDegrees(31.2304, 121.4737, 4.0));
  mgr.addAnchor("guangzhou", "Guangzhou", Geodetic::fromDegrees(23.1291, 113.2644, 21.0));

  // A point near Shanghai
  auto near_sh = geodeticToECEF(Geodetic::fromDegrees(31.5, 121.0, 0.0));
  EXPECT_EQ(mgr.findNearestAnchor(near_sh), "shanghai");

  // A point near Beijing
  auto near_bj = geodeticToECEF(Geodetic::fromDegrees(40.0, 116.0, 0.0));
  EXPECT_EQ(mgr.findNearestAnchor(near_bj), "beijing");
}

TEST(AnchorManagerTest, FindNearestEmpty) {
  AnchorManager mgr;
  EXPECT_EQ(mgr.findNearestAnchor({0, 0, 0}), "");
}

// ============================================================================
// Serialization
// ============================================================================

TEST(AnchorSerializationTest, RoundtripJson) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Station A", Geodetic::fromDegrees(39.9042, 116.4074, 50.0), 0.1);
  mgr.addAnchor("a2", "Station B", Geodetic::fromDegrees(31.2304, 121.4737, 4.0), 0.0);

  auto json = mgr.toJson();

  AnchorManager mgr2;
  size_t loaded = mgr2.fromJson(json);
  EXPECT_EQ(loaded, 2u);
  EXPECT_EQ(mgr2.size(), 2u);

  auto* a1 = mgr2.getAnchor("a1");
  ASSERT_NE(a1, nullptr);
  EXPECT_EQ(a1->name, "Station A");

  // Verify ECEF positions match after deserialization
  auto* orig = mgr.getAnchor("a1");
  EXPECT_NEAR(a1->origin_ecef.x, orig->origin_ecef.x, 0.001);
  EXPECT_NEAR(a1->origin_ecef.y, orig->origin_ecef.y, 0.001);
  EXPECT_NEAR(a1->origin_ecef.z, orig->origin_ecef.z, 0.001);
}

TEST(AnchorSerializationTest, ConversionAfterDeserialization) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "A", Geodetic::fromDegrees(39.9042, 116.4074, 50.0));
  mgr.addAnchor("a2", "B", Geodetic::fromDegrees(39.91, 116.41, 50.0));

  // Do a conversion with original
  glm::dvec3 orig_result;
  mgr.convertBetweenAnchors({100, 200, 50}, "a1", "a2", orig_result);

  // Serialize, deserialize, and convert again
  auto json = mgr.toJson();
  AnchorManager mgr2;
  mgr2.fromJson(json);

  glm::dvec3 deser_result;
  mgr2.convertBetweenAnchors({100, 200, 50}, "a1", "a2", deser_result);

  EXPECT_NEAR(deser_result.x, orig_result.x, METER_TOL);
  EXPECT_NEAR(deser_result.y, orig_result.y, METER_TOL);
  EXPECT_NEAR(deser_result.z, orig_result.z, METER_TOL);
}

// ============================================================================
// Invalid anchor IDs
// ============================================================================

TEST(AnchorManagerTest, InvalidAnchorIds) {
  AnchorManager mgr;
  mgr.addAnchor("a1", "Test", Geodetic::fromDegrees(0, 0, 0));

  glm::dvec3 ecef, local;
  EXPECT_FALSE(mgr.localToECEF({0, 0, 0}, "nonexistent", ecef));
  EXPECT_FALSE(mgr.ecefToLocal({0, 0, 0}, "nonexistent", local));
  EXPECT_FALSE(mgr.convertBetweenAnchors({0, 0, 0}, "a1", "nonexistent", local));
  EXPECT_FALSE(mgr.convertBetweenAnchors({0, 0, 0}, "nonexistent", "a1", local));
}

// ============================================================================
// Clear
// ============================================================================

TEST(AnchorManagerTest, Clear) {
  AnchorManager mgr;
  for (int i = 0; i < 10; ++i) {
    mgr.addAnchor("a" + std::to_string(i), "Test", Geodetic::fromDegrees(i, i, 0));
  }
  EXPECT_EQ(mgr.size(), 10u);
  mgr.clear();
  EXPECT_EQ(mgr.size(), 0u);
}
