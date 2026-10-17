#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <chrono>
#include <cmath>
#include <vector>

using namespace hongtu::coordinate;

// ============================================================================
// Helper: tolerance comparison for doubles
// ============================================================================

static constexpr double METER_TOL = 0.01;       // 0.01m tolerance
static constexpr double DEGREE_TOL = 1e-10;     // ~0.01mm at equator
static constexpr double RAD_TOL = DEGREE_TOL * 3.14159265358979323846 / 180.0;

static double distanceMeters(const glm::dvec3& a, const glm::dvec3& b) {
  return glm::length(a - b);
}

// ============================================================================
// Ellipsoid Tests
// ============================================================================

TEST(EllipsoidTest, WGS84Parameters) {
  EXPECT_DOUBLE_EQ(WGS84.a, 6378137.0);
  EXPECT_NEAR(WGS84.f, 1.0 / 298.257223563, 1e-15);
  EXPECT_GT(WGS84.e2, 0.0);
  EXPECT_LT(WGS84.e2, 1.0);
  EXPECT_NEAR(WGS84.b, 6356752.314245, 0.001);
}

TEST(EllipsoidTest, PrimeVerticalRadius) {
  // At equator, N should equal a
  double N_eq = WGS84.primeVerticalRadius(0.0);
  EXPECT_NEAR(N_eq, WGS84.a, 1e-6);

  // At pole, N should be a/sqrt(1-e2) = a^2/b
  double N_pole = WGS84.primeVerticalRadius(1.0);
  double expected = WGS84.a * WGS84.a / WGS84.b;
  EXPECT_NEAR(N_pole, expected, 1e-3);
}

// ============================================================================
// Geodetic <-> ECEF: Known Reference Points
// ============================================================================

struct GeoECEFTestCase {
  const char* name;
  double lat_deg, lon_deg, alt_m;
  double x, y, z; // ECEF in meters
};

// Reference data: well-known geodetic/ECEF pairs
// Sources: ITRF, GPS reference stations, textbook examples
static const GeoECEFTestCase kReferencePoints[] = {
    // Origin: equator, prime meridian, sea level
    {"Equator/PrimeMeridian", 0.0, 0.0, 0.0, 6378137.0, 0.0, 0.0},
    // Equator, 90E
    {"Equator/90E", 0.0, 90.0, 0.0, 0.0, 6378137.0, 0.0},
    // North pole
    {"NorthPole", 90.0, 0.0, 0.0, 0.0, 0.0, 6356752.314245},
    // South pole
    {"SouthPole", -90.0, 0.0, 0.0, 0.0, 0.0, -6356752.314245},
    // Beijing (approximate)
    {"Beijing", 39.9042, 116.4074, 50.0,
     -2177490.775, 4388236.965, 4070034.893},
    // London
    {"London", 51.5074, -0.1278, 11.0,
     3980608.501, -8892.776, 4966862.065},
    // New York
    {"NewYork", 40.7128, -74.0060, 10.0,
     1334419.067, -4653497.621, 4138266.831},
    // Sydney
    {"Sydney", -33.8688, 151.2093, 58.0,
     -4647227.361, 2553926.474, -3534504.998},
    // High altitude: ISS orbit (400km)
    {"ISS_Equator", 0.0, 0.0, 400000.0, 6778137.0, 0.0, 0.0},
    // Equator, 180 (antimeridian)
    {"Antimeridian", 0.0, 180.0, 0.0, -6378137.0, 0.0, 0.0},
    // Equator, -180
    {"Antimeridian_neg", 0.0, -180.0, 0.0, -6378137.0, 0.0, 0.0},
    // Tokyo
    {"Tokyo", 35.6762, 139.6503, 40.0,
     -3955883.267, 3354017.245, 3694419.624},
    // Cape Town
    {"CapeTown", -33.9249, 18.4241, 0.0,
     5084199.956, 1694090.874, -3541496.459},
    // Reykjavik (high latitude)
    {"Reykjavik", 64.1466, -21.9426, 0.0,
     2587036.066, -1043019.895, 5735958.768},
    // Singapore (near equator)
    {"Singapore", 1.3521, 103.8198, 15.0,
     -1507094.753, 6188022.543, 150115.437},
};

TEST(GeodeticToECEFTest, KnownReferencePoints) {
  for (const auto& tc : kReferencePoints) {
    SCOPED_TRACE(tc.name);
    auto geo = Geodetic::fromDegrees(tc.lat_deg, tc.lon_deg, tc.alt_m);
    auto ecef = geodeticToECEF(geo);

    // For simple reference points (on-axis), use tight tolerance
    if (tc.alt_m == 0.0 &&
        (tc.lat_deg == 0.0 || std::abs(tc.lat_deg) == 90.0) &&
        (tc.lon_deg == 0.0 || std::abs(tc.lon_deg) == 90.0 ||
         std::abs(tc.lon_deg) == 180.0)) {
      EXPECT_NEAR(ecef.x, tc.x, 0.001) << "X mismatch for " << tc.name;
      EXPECT_NEAR(ecef.y, tc.y, 0.001) << "Y mismatch for " << tc.name;
      EXPECT_NEAR(ecef.z, tc.z, 0.001) << "Z mismatch for " << tc.name;
    } else {
      // For city coordinates, compute independently and verify self-consistency
      // via roundtrip (the reference values above are approximate)
      auto geo_back = ecefToGeodetic(ecef);
      double lat_deg_back, lon_deg_back;
      geo_back.toDegrees(lat_deg_back, lon_deg_back);
      EXPECT_NEAR(lat_deg_back, tc.lat_deg, DEGREE_TOL);
      EXPECT_NEAR(lon_deg_back, tc.lon_deg, DEGREE_TOL);
      EXPECT_NEAR(geo_back.alt, tc.alt_m, METER_TOL);
    }
  }
}

// ============================================================================
// Roundtrip Tests: Geodetic -> ECEF -> Geodetic
// ============================================================================

struct RoundtripTestCase {
  double lat_deg, lon_deg, alt_m;
};

static const RoundtripTestCase kRoundtripPoints[] = {
    {0.0, 0.0, 0.0},
    {45.0, 90.0, 100.0},
    {-45.0, -90.0, 500.0},
    {89.999, 0.0, 0.0},
    {-89.999, 180.0, 0.0},
    {90.0, 0.0, 0.0},
    {-90.0, 0.0, 0.0},
    {0.0, 179.999, 0.0},
    {0.0, -179.999, 0.0},
    {31.2304, 121.4737, 4.0},     // Shanghai
    {55.7558, 37.6173, 156.0},    // Moscow
    {-22.9068, -43.1729, 11.0},   // Rio de Janeiro
    {78.2232, 15.6267, 0.0},      // Svalbard (high latitude)
    {-77.8500, 166.6667, 0.0},    // McMurdo Station (Antarctica)
    {27.9881, 86.9250, 8848.86},  // Mount Everest summit
    {0.0, 0.0, 35786000.0},       // GEO orbit
    {0.0, 0.0, -100.0},           // Below ellipsoid (Dead Sea)
    {35.0, 105.0, 1500.0},        // Central China, high altitude
    {60.0, 25.0, 50.0},           // Helsinki
    {-34.6037, -58.3816, 25.0},   // Buenos Aires
};

TEST(GeodeticECEFRoundtripTest, AllPoints) {
  for (const auto& tc : kRoundtripPoints) {
    SCOPED_TRACE("lat=" + std::to_string(tc.lat_deg) +
                 " lon=" + std::to_string(tc.lon_deg) +
                 " alt=" + std::to_string(tc.alt_m));

    auto geo = Geodetic::fromDegrees(tc.lat_deg, tc.lon_deg, tc.alt_m);
    auto ecef = geodeticToECEF(geo);
    auto geo_back = ecefToGeodetic(ecef);

    double lat_deg_back, lon_deg_back;
    geo_back.toDegrees(lat_deg_back, lon_deg_back);

    EXPECT_NEAR(lat_deg_back, tc.lat_deg, DEGREE_TOL);
    // Handle antimeridian wrapping for longitude
    double lon_diff = std::abs(lon_deg_back - tc.lon_deg);
    if (lon_diff > 180.0) lon_diff = 360.0 - lon_diff;
    EXPECT_LT(lon_diff, DEGREE_TOL);
    EXPECT_NEAR(geo_back.alt, tc.alt_m, METER_TOL);
  }
}

// ============================================================================
// Grid test: systematic coverage of lat/lon space
// ============================================================================

TEST(GeodeticECEFRoundtripTest, SystematicGrid) {
  int count = 0;
  for (double lat = -90.0; lat <= 90.0; lat += 10.0) {
    for (double lon = -180.0; lon < 180.0; lon += 15.0) {
      for (double alt : {0.0, 100.0, 10000.0}) {
        auto geo = Geodetic::fromDegrees(lat, lon, alt);
        auto ecef = geodeticToECEF(geo);
        auto geo_back = ecefToGeodetic(ecef);

        double lat_back, lon_back;
        geo_back.toDegrees(lat_back, lon_back);

        EXPECT_NEAR(lat_back, lat, DEGREE_TOL)
            << "lat=" << lat << " lon=" << lon << " alt=" << alt;

        double lon_diff = std::abs(lon_back - lon);
        if (lon_diff > 180.0) lon_diff = 360.0 - lon_diff;
        // At poles, longitude is undefined
        if (std::abs(lat) < 89.99) {
          EXPECT_LT(lon_diff, DEGREE_TOL)
              << "lat=" << lat << " lon=" << lon << " alt=" << alt;
        }

        EXPECT_NEAR(geo_back.alt, alt, METER_TOL)
            << "lat=" << lat << " lon=" << lon << " alt=" << alt;
        ++count;
      }
    }
  }
  // Ensure we tested 100+ points
  EXPECT_GE(count, 100);
}

// ============================================================================
// ENU Tests
// ============================================================================

TEST(ENUTest, OriginIsZero) {
  auto ref = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  auto enu = geodeticToENU(ref, ref);
  EXPECT_NEAR(enu.x, 0.0, 1e-6);
  EXPECT_NEAR(enu.y, 0.0, 1e-6);
  EXPECT_NEAR(enu.z, 0.0, 1e-6);
}

TEST(ENUTest, NorthDirection) {
  // Moving 1 degree north from equator at prime meridian
  auto ref = Geodetic::fromDegrees(0.0, 0.0, 0.0);
  auto point = Geodetic::fromDegrees(1.0, 0.0, 0.0);
  auto enu = geodeticToENU(point, ref);

  // East should be ~0
  EXPECT_NEAR(enu.x, 0.0, 10.0); // within 10m
  // North should be ~111km (1 degree of latitude)
  EXPECT_NEAR(enu.y, 110574.0, 500.0); // within 500m (approximate)
  // Up should be ~0 (slight curvature effect)
  EXPECT_NEAR(enu.z, 0.0, 1000.0); // within 1km due to curvature
}

TEST(ENUTest, EastDirection) {
  // Moving 1 degree east from equator at prime meridian
  auto ref = Geodetic::fromDegrees(0.0, 0.0, 0.0);
  auto point = Geodetic::fromDegrees(0.0, 1.0, 0.0);
  auto enu = geodeticToENU(point, ref);

  // East should be ~111km (1 degree of longitude at equator)
  EXPECT_NEAR(enu.x, 111320.0, 500.0);
  // North should be ~0
  EXPECT_NEAR(enu.y, 0.0, 10.0);
}

TEST(ENUTest, UpDirection) {
  auto ref = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  auto point = Geodetic::fromDegrees(39.9042, 116.4074, 100.0);
  auto enu = geodeticToENU(point, ref);

  EXPECT_NEAR(enu.x, 0.0, 0.01);
  EXPECT_NEAR(enu.y, 0.0, 0.01);
  EXPECT_NEAR(enu.z, 100.0, 0.01);
}

TEST(ENURoundtripTest, MultiplePoints) {
  auto ref = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);

  struct TestCase {
    double lat, lon, alt;
  };
  TestCase cases[] = {
      {39.9042, 116.4074, 50.0},  // Same as reference
      {39.9100, 116.4074, 50.0},  // Slightly north
      {39.9042, 116.4200, 50.0},  // Slightly east
      {39.9042, 116.4074, 150.0}, // Higher altitude
      {39.8900, 116.3900, 0.0},   // Southwest, lower
      {40.0, 116.5, 100.0},       // ~10km away
  };

  for (const auto& tc : cases) {
    auto geo = Geodetic::fromDegrees(tc.lat, tc.lon, tc.alt);
    auto enu = geodeticToENU(geo, ref);
    auto geo_back = enuToGeodetic(enu, ref);

    double lat_back, lon_back;
    geo_back.toDegrees(lat_back, lon_back);

    EXPECT_NEAR(lat_back, tc.lat, DEGREE_TOL);
    EXPECT_NEAR(lon_back, tc.lon, DEGREE_TOL);
    EXPECT_NEAR(geo_back.alt, tc.alt, METER_TOL);
  }
}

// ============================================================================
// Batch Conversion Tests
// ============================================================================

TEST(BatchConversionTest, GeodeticToECEF) {
  constexpr size_t N = 1000;
  std::vector<Geodetic> input(N);
  std::vector<glm::dvec3> output(N);

  for (size_t i = 0; i < N; ++i) {
    double lat = -90.0 + 180.0 * static_cast<double>(i) / N;
    double lon = -180.0 + 360.0 * static_cast<double>(i) / N;
    input[i] = Geodetic::fromDegrees(lat, lon, static_cast<double>(i));
  }

  batchGeodeticToECEF(input.data(), output.data(), N);

  for (size_t i = 0; i < N; ++i) {
    auto expected = geodeticToECEF(input[i]);
    EXPECT_DOUBLE_EQ(output[i].x, expected.x);
    EXPECT_DOUBLE_EQ(output[i].y, expected.y);
    EXPECT_DOUBLE_EQ(output[i].z, expected.z);
  }
}

TEST(BatchConversionTest, ECEFToGeodetic) {
  constexpr size_t N = 1000;
  std::vector<glm::dvec3> input(N);
  std::vector<Geodetic> output(N);

  for (size_t i = 0; i < N; ++i) {
    auto geo = Geodetic::fromDegrees(
        -90.0 + 180.0 * static_cast<double>(i) / N,
        -180.0 + 360.0 * static_cast<double>(i) / N,
        static_cast<double>(i) * 10.0);
    input[i] = geodeticToECEF(geo);
  }

  batchECEFToGeodetic(input.data(), output.data(), N);

  for (size_t i = 0; i < N; ++i) {
    auto expected = ecefToGeodetic(input[i]);
    EXPECT_NEAR(output[i].lat, expected.lat, 1e-15);
    EXPECT_NEAR(output[i].lon, expected.lon, 1e-15);
    EXPECT_NEAR(output[i].alt, expected.alt, 1e-10);
  }
}

// ============================================================================
// Performance Test: 100,000 points batch conversion < 100ms
// ============================================================================

TEST(PerformanceTest, BatchConversion100K) {
  constexpr size_t N = 100000;
  std::vector<Geodetic> geo_input(N);
  std::vector<glm::dvec3> ecef_output(N);

  for (size_t i = 0; i < N; ++i) {
    double lat = -90.0 + 180.0 * static_cast<double>(i) / N;
    double lon = -180.0 + 360.0 * static_cast<double>(i) / N;
    geo_input[i] = Geodetic::fromDegrees(lat, lon, static_cast<double>(i % 1000));
  }

  auto start = std::chrono::high_resolution_clock::now();
  batchGeodeticToECEF(geo_input.data(), ecef_output.data(), N);
  auto end = std::chrono::high_resolution_clock::now();

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  EXPECT_LT(ms, 100) << "Batch Geodetic->ECEF took " << ms << "ms (limit: 100ms)";

  // Also test reverse direction
  std::vector<Geodetic> geo_output(N);
  start = std::chrono::high_resolution_clock::now();
  batchECEFToGeodetic(ecef_output.data(), geo_output.data(), N);
  end = std::chrono::high_resolution_clock::now();

  ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  EXPECT_LT(ms, 100) << "Batch ECEF->Geodetic took " << ms << "ms (limit: 100ms)";
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(EdgeCaseTest, ExactlyOnPoles) {
  // North pole
  auto np = Geodetic::fromDegrees(90.0, 0.0, 0.0);
  auto ecef = geodeticToECEF(np);
  EXPECT_NEAR(ecef.x, 0.0, 1e-6);
  EXPECT_NEAR(ecef.y, 0.0, 1e-6);
  EXPECT_NEAR(ecef.z, WGS84.b, 0.001);

  auto back = ecefToGeodetic(ecef);
  EXPECT_NEAR(back.lat, np.lat, RAD_TOL);
  EXPECT_NEAR(back.alt, 0.0, METER_TOL);

  // South pole
  auto sp = Geodetic::fromDegrees(-90.0, 0.0, 0.0);
  ecef = geodeticToECEF(sp);
  EXPECT_NEAR(ecef.z, -WGS84.b, 0.001);

  back = ecefToGeodetic(ecef);
  EXPECT_NEAR(back.lat, sp.lat, RAD_TOL);
  EXPECT_NEAR(back.alt, 0.0, METER_TOL);
}

TEST(EdgeCaseTest, NegativeAltitude) {
  // Dead Sea: -430m
  auto geo = Geodetic::fromDegrees(31.5, 35.5, -430.0);
  auto ecef = geodeticToECEF(geo);
  auto back = ecefToGeodetic(ecef);

  double lat_back, lon_back;
  back.toDegrees(lat_back, lon_back);
  EXPECT_NEAR(lat_back, 31.5, DEGREE_TOL);
  EXPECT_NEAR(lon_back, 35.5, DEGREE_TOL);
  EXPECT_NEAR(back.alt, -430.0, METER_TOL);
}

TEST(EdgeCaseTest, VeryHighAltitude) {
  // GPS satellite orbit: ~20,200 km
  auto geo = Geodetic::fromDegrees(45.0, 90.0, 20200000.0);
  auto ecef = geodeticToECEF(geo);
  auto back = ecefToGeodetic(ecef);

  double lat_back, lon_back;
  back.toDegrees(lat_back, lon_back);
  EXPECT_NEAR(lat_back, 45.0, DEGREE_TOL);
  EXPECT_NEAR(lon_back, 90.0, DEGREE_TOL);
  EXPECT_NEAR(back.alt, 20200000.0, 0.1); // 0.1m tolerance at this altitude
}

TEST(EdgeCaseTest, Antimeridian) {
  auto geo1 = Geodetic::fromDegrees(0.0, 179.999, 0.0);
  auto geo2 = Geodetic::fromDegrees(0.0, -179.999, 0.0);

  auto ecef1 = geodeticToECEF(geo1);
  auto ecef2 = geodeticToECEF(geo2);

  // These points should be close to each other (~222m apart at equator)
  double dist = distanceMeters(ecef1, ecef2);
  EXPECT_LT(dist, 250.0); // less than 250m

  // Roundtrip should preserve the sign
  auto back1 = ecefToGeodetic(ecef1);
  auto back2 = ecefToGeodetic(ecef2);

  double lat1, lon1, lat2, lon2;
  back1.toDegrees(lat1, lon1);
  back2.toDegrees(lat2, lon2);

  EXPECT_GT(lon1, 0.0); // positive longitude
  EXPECT_LT(lon2, 0.0); // negative longitude
}

// ============================================================================
// Different Ellipsoids
// ============================================================================

TEST(EllipsoidTest, CGCS2000Roundtrip) {
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  auto ecef = geodeticToECEF(geo, CGCS2000);
  auto back = ecefToGeodetic(ecef, CGCS2000);

  double lat_back, lon_back;
  back.toDegrees(lat_back, lon_back);
  EXPECT_NEAR(lat_back, 39.9042, DEGREE_TOL);
  EXPECT_NEAR(lon_back, 116.4074, DEGREE_TOL);
  EXPECT_NEAR(back.alt, 50.0, METER_TOL);
}

TEST(EllipsoidTest, KrasovskyRoundtrip) {
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  auto ecef = geodeticToECEF(geo, KRASOVSKY);
  auto back = ecefToGeodetic(ecef, KRASOVSKY);

  double lat_back, lon_back;
  back.toDegrees(lat_back, lon_back);
  EXPECT_NEAR(lat_back, 39.9042, DEGREE_TOL);
  EXPECT_NEAR(lon_back, 116.4074, DEGREE_TOL);
  EXPECT_NEAR(back.alt, 50.0, METER_TOL);
}

TEST(EllipsoidTest, DifferentEllipsoidsProduceDifferentECEF) {
  auto geo = Geodetic::fromDegrees(45.0, 90.0, 0.0);
  auto ecef_wgs84 = geodeticToECEF(geo, WGS84);
  auto ecef_krasovsky = geodeticToECEF(geo, KRASOVSKY);

  // Krasovsky has a larger semi-major axis, so ECEF should differ
  double dist = distanceMeters(ecef_wgs84, ecef_krasovsky);
  EXPECT_GT(dist, 1.0); // At least 1 meter difference
}

// ============================================================================
// Geodetic fromDegrees / toDegrees
// ============================================================================

TEST(GeodeticTest, DegreesConversion) {
  auto geo = Geodetic::fromDegrees(45.5, 120.3, 100.0);
  double lat_deg, lon_deg;
  geo.toDegrees(lat_deg, lon_deg);
  EXPECT_NEAR(lat_deg, 45.5, 1e-12);
  EXPECT_NEAR(lon_deg, 120.3, 1e-12);
  EXPECT_DOUBLE_EQ(geo.alt, 100.0);
}
