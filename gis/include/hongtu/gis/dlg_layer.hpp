#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/gis/geometry.hpp>
#include <string>
#include <vector>

namespace hongtu::gis {

/// Semantic layers commonly carried by DLG/topographic vector data.
enum class DlgFeatureClass : std::uint8_t {
  Road,
  Railway,
  Water,
  Boundary,
  Building,
  Pipeline,
  Power,
  Barrier,
  Industrial,
  Other,
  Count,
};

constexpr std::size_t kDlgFeatureClassCount = static_cast<std::size_t>(DlgFeatureClass::Count);

const char* dlgFeatureClassName(DlgFeatureClass feature_class);

struct DlgWgs84Bounds {
  double min_lon = 0.0;
  double min_lat = 0.0;
  double max_lon = 0.0;
  double max_lat = 0.0;
  bool valid = false;
};

struct DlgPolyline {
  std::int64_t feature_id = -1;
  DlgFeatureClass feature_class = DlgFeatureClass::Other;
  bool closed = false;
  std::vector<GeoPoint> points;
};

/// Validated WGS84 DLG representation independent of a rendering backend.
struct DlgDataset {
  bool valid = false;
  std::string source;
  std::string error;
  std::size_t source_feature_count = 0;
  std::size_t vertex_count = 0;
  DlgWgs84Bounds bounds;
  std::array<std::size_t, kDlgFeatureClassCount> class_counts{};
  std::vector<DlgPolyline> polylines;
};

struct DlgLocalPolyline {
  std::int64_t feature_id = -1;
  DlgFeatureClass feature_class = DlgFeatureClass::Other;
  bool closed = false;
  std::vector<glm::dvec3> enu_points;
};

/// Double-precision local representation relative to one WGS84 anchor.
struct DlgLocalDataset {
  bool valid = false;
  std::string error;
  hongtu::coordinate::Geodetic anchor{};
  glm::dvec3 minimum_enu{0.0};
  glm::dvec3 maximum_enu{0.0};
  double maximum_roundtrip_error_m = 0.0;
  std::size_t vertex_count = 0;
  std::array<std::size_t, kDlgFeatureClassCount> class_counts{};
  std::vector<DlgLocalPolyline> polylines;
};

/// Classify and flatten a parsed GIS feature collection into DLG polylines.
DlgDataset buildDlgDataset(const FeatureCollection& features, std::string source = {});

/// Read a UTF-8 GeoJSON file and build a validated WGS84 DLG dataset.
DlgDataset loadDlgGeoJson(const std::filesystem::path& path);

/// Project a WGS84 DLG dataset into an anchor-relative ENU frame in metres.
DlgLocalDataset projectDlgToEnu(const DlgDataset& dataset, const hongtu::coordinate::Geodetic& anchor);

}  // namespace hongtu::gis
