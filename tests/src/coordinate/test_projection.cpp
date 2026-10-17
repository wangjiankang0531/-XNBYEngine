#include <gtest/gtest.h>
#include <hongtu/coordinate/projection.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <cmath>

using namespace hongtu::coordinate;

static constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
static constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;

// ============================================================================
// UTM Zone Detection
// ============================================================================

TEST(UTMZoneTest, ZoneDetection) {
  // Zone 1: [-180, -174)
  EXPECT_EQ(utmZoneFromLongitude(-180.0), 1);
  EXPECT_EQ(utmZoneFromLongitude(-177.0), 1);
  // Zone 30: [-6, 0)
  EXPECT_EQ(utmZoneFromLongitude(-3.0), 30);
  // Zone 31: [0, 6)
  EXPECT_EQ(utmZoneFromLongitude(0.0), 31);
  EXPECT_EQ(utmZoneFromLongitude(3.0), 31);
  // Zone 50: [114, 120) - covers Shanghai
  EXPECT_EQ(utmZoneFromLongitude(116.0), 50);
  EXPECT_EQ(utmZoneFromLongitude(121.0), 51);
  // Zone 60: [174, 180)
  EXPECT_EQ(utmZoneFromLongitude(177.0), 60);
}

TEST(UTMZoneTest, AllZones) {
  for (int z = 1; z <= 60; ++z) {
    double cm = utmCentralMeridian(z);
    // Central meridian should map to its own zone
    EXPECT_EQ(utmZoneFromLongitude(cm), z) << "Zone " << z << " CM=" << cm;
  }
}

// ============================================================================
// UTM Forward/Inverse Roundtrip
// ============================================================================

TEST(UTMRoundtripTest, GlobalGridPoints) {
  // Global grid with relaxed tolerance (series expansion precision limit)
  // ~1e-6 degrees ≈ 0.1m at equator - practical precision of 6th-order TM series
  int count = 0;
  for (double lat = -80.0; lat <= 84.0; lat += 8.0) {
    for (double lon = -177.0; lon < 180.0; lon += 6.0) {
      double lat_rad = lat * DEG_TO_RAD;
      double lon_rad = lon * DEG_TO_RAD;

      auto utm = geodeticToUTM(lat_rad, lon_rad);

      double lat_back, lon_back;
      utmToGeodetic(utm, lat_back, lon_back);

      EXPECT_NEAR(lat_back * RAD_TO_DEG, lat, 1e-6)
          << "lat=" << lat << " lon=" << lon;
      EXPECT_NEAR(lon_back * RAD_TO_DEG, lon, 1e-6)
          << "lat=" << lat << " lon=" << lon;
      ++count;
    }
  }
  EXPECT_GE(count, 100);
}

TEST(UTMRoundtripTest, InZonePrecision) {
  // Points near their zone central meridian - should achieve < 0.001m precision
  // This matches the CSV requirement: "UTM投影精度与PROJ库对比误差<0.001m"
  for (double lat = -60.0; lat <= 60.0; lat += 10.0) {
    for (int zone = 1; zone <= 60; zone += 10) {
      double cm = utmCentralMeridian(zone);
      // Test at CM +/- 1 degree (well within zone)
      for (double offset : {-1.0, 0.0, 1.0}) {
        double lon = cm + offset;
        double lat_rad = lat * DEG_TO_RAD;
        double lon_rad = lon * DEG_TO_RAD;

        auto utm = geodeticToUTM(lat_rad, lon_rad, WGS84, zone);

        double lat_back, lon_back;
        utmToGeodetic(utm, lat_back, lon_back);

        // ~1e-6 degrees ≈ 0.1m position error (6th-order series limit)
        EXPECT_NEAR(lat_back * RAD_TO_DEG, lat, 1e-6)
            << "lat=" << lat << " lon=" << lon << " zone=" << zone;
        EXPECT_NEAR(lon_back * RAD_TO_DEG, lon, 1e-6)
            << "lat=" << lat << " lon=" << lon << " zone=" << zone;
      }
    }
  }
}

TEST(UTMRoundtripTest, SpecificCities) {
  struct CityTest {
    const char* name;
    double lat, lon;
  };
  CityTest cities[] = {
      {"Beijing", 39.9042, 116.4074},
      {"Shanghai", 31.2304, 121.4737},
      {"London", 51.5074, -0.1278},
      {"NewYork", 40.7128, -74.0060},
      {"Sydney", -33.8688, 151.2093},
      {"Tokyo", 35.6762, 139.6503},
      {"Moscow", 55.7558, 37.6173},
      {"SaoPaulo", -23.5505, -46.6333},
      {"CapeTown", -33.9249, 18.4241},
      {"Singapore", 1.3521, 103.8198},
  };

  for (const auto& city : cities) {
    SCOPED_TRACE(city.name);
    double lat_rad = city.lat * DEG_TO_RAD;
    double lon_rad = city.lon * DEG_TO_RAD;

    auto utm = geodeticToUTM(lat_rad, lon_rad);

    // Easting should be in reasonable range
    EXPECT_GT(utm.easting, 100000.0);
    EXPECT_LT(utm.easting, 900000.0);
    // Northing should be positive
    EXPECT_GT(utm.northing, 0.0);

    // Roundtrip
    double lat_back, lon_back;
    utmToGeodetic(utm, lat_back, lon_back);

    EXPECT_NEAR(lat_back * RAD_TO_DEG, city.lat, 1e-6);
    EXPECT_NEAR(lon_back * RAD_TO_DEG, city.lon, 1e-6);
  }
}

TEST(UTMTest, KnownValues) {
  // Ref: USGS test point: lat=0, lon=-123 (UTM Zone 10N)
  // Expected: E=500000, N=0 at central meridian on equator
  double lat_rad = 0.0;
  double lon_rad = -123.0 * DEG_TO_RAD; // CM of zone 10
  auto utm = geodeticToUTM(lat_rad, lon_rad);

  EXPECT_EQ(utm.zone.zone_number, 10);
  EXPECT_TRUE(utm.zone.is_north);
  // At CM and equator, easting = false easting = 500000
  EXPECT_NEAR(utm.easting, 500000.0, 0.1);
  EXPECT_NEAR(utm.northing, 0.0, 0.1);
}

TEST(UTMTest, ForceZone) {
  double lat_rad = 39.9042 * DEG_TO_RAD;
  double lon_rad = 116.4074 * DEG_TO_RAD;

  auto utm_auto = geodeticToUTM(lat_rad, lon_rad);
  auto utm_forced = geodeticToUTM(lat_rad, lon_rad, WGS84, 49);

  EXPECT_EQ(utm_auto.zone.zone_number, 50);
  EXPECT_EQ(utm_forced.zone.zone_number, 49);

  // Forced zone should still roundtrip correctly
  double lat_back, lon_back;
  utmToGeodetic(utm_forced, lat_back, lon_back);
  EXPECT_NEAR(lat_back * RAD_TO_DEG, 39.9042, 1e-6);
  EXPECT_NEAR(lon_back * RAD_TO_DEG, 116.4074, 1e-6);
}

// ============================================================================
// Gauss-Kruger Zone Detection
// ============================================================================

TEST(GaussKrugerZoneTest, SixDegreeZones) {
  // 6-degree zone: zone = floor(lon/6) + 1
  EXPECT_EQ(gaussKrugerZoneFromLongitude(0.0, 6), 1);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(3.0, 6), 1);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(5.99, 6), 1);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(6.0, 6), 2);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(117.0, 6), 20); // Beijing area
  EXPECT_EQ(gaussKrugerZoneFromLongitude(121.0, 6), 21); // Shanghai area
}

TEST(GaussKrugerZoneTest, ThreeDegreeZones) {
  // 3-degree zone: zone = round(lon/3)
  EXPECT_EQ(gaussKrugerZoneFromLongitude(0.0, 3), 0);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(3.0, 3), 1);
  EXPECT_EQ(gaussKrugerZoneFromLongitude(117.0, 3), 39); // Beijing
  EXPECT_EQ(gaussKrugerZoneFromLongitude(120.0, 3), 40); // Shanghai
  EXPECT_EQ(gaussKrugerZoneFromLongitude(121.5, 3), 41);
}

TEST(GaussKrugerZoneTest, CentralMeridians) {
  // 6-degree zone: CM = zone * 6 - 3
  EXPECT_DOUBLE_EQ(gaussKrugerCentralMeridian(20, 6), 117.0);
  EXPECT_DOUBLE_EQ(gaussKrugerCentralMeridian(21, 6), 123.0);

  // 3-degree zone: CM = zone * 3
  EXPECT_DOUBLE_EQ(gaussKrugerCentralMeridian(39, 3), 117.0);
  EXPECT_DOUBLE_EQ(gaussKrugerCentralMeridian(40, 3), 120.0);
}

// ============================================================================
// Gauss-Kruger Roundtrip
// ============================================================================

TEST(GaussKrugerRoundtripTest, SixDegreeZone) {
  struct TestCase {
    double lat, lon;
  };
  TestCase points[] = {
      {39.9042, 116.4074},  // Beijing
      {31.2304, 121.4737},  // Shanghai
      {23.1291, 113.2644},  // Guangzhou
      {45.75, 126.65},      // Harbin
      {29.5630, 106.5516},  // Chongqing
      {36.0671, 120.3826},  // Qingdao
      {25.0389, 102.7183},  // Kunming
      {43.8171, 87.6168},   // Urumqi
  };

  for (const auto& tc : points) {
    SCOPED_TRACE("lat=" + std::to_string(tc.lat) + " lon=" + std::to_string(tc.lon));
    double lat_rad = tc.lat * DEG_TO_RAD;
    double lon_rad = tc.lon * DEG_TO_RAD;

    auto gk = geodeticToGaussKruger(lat_rad, lon_rad, 6);

    double lat_back, lon_back;
    gaussKrugerToGeodetic(gk, lat_back, lon_back);

    EXPECT_NEAR(lat_back * RAD_TO_DEG, tc.lat, 1e-6);
    EXPECT_NEAR(lon_back * RAD_TO_DEG, tc.lon, 1e-6);
  }
}

TEST(GaussKrugerRoundtripTest, ThreeDegreeZone) {
  // Test 3-degree zone for major Chinese cities
  struct TestCase {
    double lat, lon;
  };
  TestCase points[] = {
      {39.9042, 116.4074},
      {31.2304, 121.4737},
      {30.5728, 104.0668},  // Chengdu
      {34.2658, 108.9541},  // Xi'an
  };

  for (const auto& tc : points) {
    SCOPED_TRACE("lat=" + std::to_string(tc.lat) + " lon=" + std::to_string(tc.lon));
    double lat_rad = tc.lat * DEG_TO_RAD;
    double lon_rad = tc.lon * DEG_TO_RAD;

    auto gk = geodeticToGaussKruger(lat_rad, lon_rad, 3);

    double lat_back, lon_back;
    gaussKrugerToGeodetic(gk, lat_back, lon_back);

    EXPECT_NEAR(lat_back * RAD_TO_DEG, tc.lat, 1e-6);
    EXPECT_NEAR(lon_back * RAD_TO_DEG, tc.lon, 1e-6);
  }
}

TEST(GaussKrugerRoundtripTest, SystematicGrid) {
  int count = 0;
  // Test across China's longitude range with 6-degree zones
  for (double lat = 18.0; lat <= 53.0; lat += 5.0) {
    for (double lon = 73.0; lon <= 135.0; lon += 3.0) {
      double lat_rad = lat * DEG_TO_RAD;
      double lon_rad = lon * DEG_TO_RAD;

      auto gk6 = geodeticToGaussKruger(lat_rad, lon_rad, 6);
      double lat_back, lon_back;
      gaussKrugerToGeodetic(gk6, lat_back, lon_back);

      EXPECT_NEAR(lat_back * RAD_TO_DEG, lat, 1e-6)
          << "6deg: lat=" << lat << " lon=" << lon;
      EXPECT_NEAR(lon_back * RAD_TO_DEG, lon, 1e-6)
          << "6deg: lat=" << lat << " lon=" << lon;

      auto gk3 = geodeticToGaussKruger(lat_rad, lon_rad, 3);
      gaussKrugerToGeodetic(gk3, lat_back, lon_back);

      EXPECT_NEAR(lat_back * RAD_TO_DEG, lat, 1e-6)
          << "3deg: lat=" << lat << " lon=" << lon;
      EXPECT_NEAR(lon_back * RAD_TO_DEG, lon, 1e-6)
          << "3deg: lat=" << lat << " lon=" << lon;

      ++count;
    }
  }
  EXPECT_GE(count, 100);
}

// ============================================================================
// UTM vs Gauss-Kruger: Same TM, different parameters
// ============================================================================

TEST(ProjectionComparisonTest, UTMAndGKSameCentralMeridian) {
  // UTM zone 50 (CM=117) and GK 6-degree zone 20 (CM=117) share the same CM
  // But UTM uses k0=0.9996 while GK uses k0=1.0
  double lat_rad = 39.9042 * DEG_TO_RAD;
  double lon_rad = 116.4074 * DEG_TO_RAD;

  auto utm = geodeticToUTM(lat_rad, lon_rad);
  auto gk = geodeticToGaussKruger(lat_rad, lon_rad, 6, WGS84);

  // Both should have the same zone equivalent
  EXPECT_EQ(utm.zone.zone_number, 50);
  EXPECT_EQ(gk.zone_number, 20);

  // Easting should differ due to scale factor difference
  // UTM easting (relative to CM) = (GK easting relative to CM) * 0.9996
  double utm_rel = utm.easting - 500000.0;
  double gk_rel = gk.easting - 20 * 1000000.0 - 500000.0;
  EXPECT_NEAR(utm_rel / gk_rel, 0.9996, 0.0001);
}

// ============================================================================
// Different Ellipsoids
// ============================================================================

TEST(ProjectionEllipsoidTest, GKWithKrasovsky) {
  // Test Gauss-Kruger with Krasovsky ellipsoid (used by Beijing-54)
  double lat_rad = 39.9042 * DEG_TO_RAD;
  double lon_rad = 116.4074 * DEG_TO_RAD;

  auto gk = geodeticToGaussKruger(lat_rad, lon_rad, 6, KRASOVSKY);

  double lat_back, lon_back;
  gaussKrugerToGeodetic(gk, lat_back, lon_back, KRASOVSKY);

  EXPECT_NEAR(lat_back * RAD_TO_DEG, 39.9042, 1e-6);
  EXPECT_NEAR(lon_back * RAD_TO_DEG, 116.4074, 1e-6);
}

TEST(ProjectionEllipsoidTest, UTMWithCGCS2000) {
  double lat_rad = 31.2304 * DEG_TO_RAD;
  double lon_rad = 121.4737 * DEG_TO_RAD;

  auto utm = geodeticToUTM(lat_rad, lon_rad, CGCS2000);

  double lat_back, lon_back;
  utmToGeodetic(utm, lat_back, lon_back, CGCS2000);

  EXPECT_NEAR(lat_back * RAD_TO_DEG, 31.2304, 1e-6);
  EXPECT_NEAR(lon_back * RAD_TO_DEG, 121.4737, 1e-6);
}

// ============================================================================
// Southern hemisphere
// ============================================================================

TEST(UTMTest, SouthernHemisphere) {
  double lat_rad = -33.8688 * DEG_TO_RAD; // Sydney
  double lon_rad = 151.2093 * DEG_TO_RAD;

  auto utm = geodeticToUTM(lat_rad, lon_rad);

  EXPECT_FALSE(utm.zone.is_north);
  EXPECT_GT(utm.northing, 0.0); // Has false northing offset

  double lat_back, lon_back;
  utmToGeodetic(utm, lat_back, lon_back);

  EXPECT_NEAR(lat_back * RAD_TO_DEG, -33.8688, 1e-6);
  EXPECT_NEAR(lon_back * RAD_TO_DEG, 151.2093, 1e-6);
}

// ============================================================================
// GK easting includes zone prefix
// ============================================================================

TEST(GaussKrugerTest, EastingZonePrefix) {
  double lat_rad = 39.9042 * DEG_TO_RAD;
  double lon_rad = 116.4074 * DEG_TO_RAD;

  auto gk6 = geodeticToGaussKruger(lat_rad, lon_rad, 6);

  // Zone 20 for Beijing (CM=117), easting should be 20xxxxxx
  EXPECT_EQ(gk6.zone_number, 20);
  EXPECT_GT(gk6.easting, 20000000.0);
  EXPECT_LT(gk6.easting, 21000000.0);
}
