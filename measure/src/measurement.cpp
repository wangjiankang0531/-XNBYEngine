#include <hongtu/measure/measurement.hpp>

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/enu_frame.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace hongtu::measure {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthR = 6371008.8;  // WGS84 平均半径(米)

// 大圆距离(Haversine),输入弧度经纬。
double haversine(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = lat2 - lat1, dlon = lon2 - lon1;
  const double a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
                   std::cos(lat1) * std::cos(lat2) *
                       std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
  return 2.0 * kEarthR * std::asin(std::min(1.0, std::sqrt(a)));
}

double cross2D(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) {
  const glm::dvec2 ab = b - a;
  const glm::dvec2 ac = c - a;
  return ab.x * ac.y - ab.y * ac.x;
}

bool samePoint(const glm::dvec2& a, const glm::dvec2& b) {
  const glm::dvec2 delta = a - b;
  return glm::dot(delta, delta) <= 1.0e-20;
}

bool pointOnSegment(const glm::dvec2& point, const glm::dvec2& a, const glm::dvec2& b) {
  constexpr double kEpsilon = 1.0e-11;
  if (std::abs(cross2D(a, b, point)) > kEpsilon) return false;
  return point.x >= std::min(a.x, b.x) - kEpsilon && point.x <= std::max(a.x, b.x) + kEpsilon &&
         point.y >= std::min(a.y, b.y) - kEpsilon && point.y <= std::max(a.y, b.y) + kEpsilon;
}

bool segmentsIntersect(const glm::dvec2& a,
                       const glm::dvec2& b,
                       const glm::dvec2& c,
                       const glm::dvec2& d) {
  const double ab_c = cross2D(a, b, c);
  const double ab_d = cross2D(a, b, d);
  const double cd_a = cross2D(c, d, a);
  const double cd_b = cross2D(c, d, b);
  if (((ab_c > 0.0 && ab_d < 0.0) || (ab_c < 0.0 && ab_d > 0.0)) &&
      ((cd_a > 0.0 && cd_b < 0.0) || (cd_a < 0.0 && cd_b > 0.0))) {
    return true;
  }
  return (std::abs(ab_c) <= 1.0e-11 && pointOnSegment(c, a, b)) ||
         (std::abs(ab_d) <= 1.0e-11 && pointOnSegment(d, a, b)) ||
         (std::abs(cd_a) <= 1.0e-11 && pointOnSegment(a, c, d)) ||
         (std::abs(cd_b) <= 1.0e-11 && pointOnSegment(b, c, d));
}

bool isSimplePolygon(const std::vector<glm::dvec2>& polygon) {
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const std::size_t i_next = (i + 1) % polygon.size();
    for (std::size_t j = i + 1; j < polygon.size(); ++j) {
      const std::size_t j_next = (j + 1) % polygon.size();
      if (i == j || i_next == j || j_next == i) continue;
      if (segmentsIntersect(polygon[i], polygon[i_next], polygon[j], polygon[j_next])) return false;
    }
  }
  return true;
}

double signedTwiceArea(const std::vector<glm::dvec2>& polygon) {
  double area2 = 0.0;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const glm::dvec2& a = polygon[i];
    const glm::dvec2& b = polygon[(i + 1) % polygon.size()];
    area2 += a.x * b.y - b.x * a.y;
  }
  return area2;
}

bool polygonAreaCentroid(const std::vector<glm::dvec2>& polygon,
                         double& area,
                         glm::dvec2& centroid) {
  if (polygon.size() < 3) return false;
  double area2 = 0.0;
  glm::dvec2 weighted(0.0);
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const glm::dvec2& a = polygon[i];
    const glm::dvec2& b = polygon[(i + 1) % polygon.size()];
    const double cross = a.x * b.y - b.x * a.y;
    area2 += cross;
    weighted += (a + b) * cross;
  }
  if (!std::isfinite(area2) || std::abs(area2) <= 1.0e-12) return false;
  centroid = weighted / (3.0 * area2);
  area = std::abs(area2) * 0.5;
  return std::isfinite(centroid.x) && std::isfinite(centroid.y) && std::isfinite(area);
}

bool pointInTriangle(const glm::dvec2& point,
                     const glm::dvec2& a,
                     const glm::dvec2& b,
                     const glm::dvec2& c) {
  constexpr double kEpsilon = 1.0e-11;
  return cross2D(a, b, point) >= -kEpsilon && cross2D(b, c, point) >= -kEpsilon &&
         cross2D(c, a, point) >= -kEpsilon;
}

using Triangle2D = std::array<glm::dvec2, 3>;

bool triangulateSimplePolygon(const std::vector<glm::dvec2>& polygon, std::vector<Triangle2D>& triangles) {
  if (polygon.size() < 3) return false;
  const double polygon_area2 = signedTwiceArea(polygon);
  if (!std::isfinite(polygon_area2) || std::abs(polygon_area2) <= 1.0e-12) return false;

  std::vector<std::size_t> indices(polygon.size());
  std::iota(indices.begin(), indices.end(), 0);
  if (polygon_area2 < 0.0) std::reverse(indices.begin(), indices.end());

  constexpr double kEpsilon = 1.0e-11;
  triangles.clear();
  triangles.reserve(polygon.size() - 2);
  std::size_t guard = 0;
  while (indices.size() > 3 && guard++ < polygon.size() * polygon.size()) {
    bool clipped_ear = false;
    for (std::size_t i = 0; i < indices.size(); ++i) {
      const std::size_t previous = indices[(i + indices.size() - 1) % indices.size()];
      const std::size_t current = indices[i];
      const std::size_t next = indices[(i + 1) % indices.size()];
      const glm::dvec2& a = polygon[previous];
      const glm::dvec2& b = polygon[current];
      const glm::dvec2& c = polygon[next];
      if (cross2D(a, b, c) <= kEpsilon) continue;

      bool contains_vertex = false;
      for (std::size_t candidate : indices) {
        if (candidate == previous || candidate == current || candidate == next) continue;
        if (pointInTriangle(polygon[candidate], a, b, c)) {
          contains_vertex = true;
          break;
        }
      }
      if (contains_vertex) continue;

      triangles.push_back({a, b, c});
      indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
      clipped_ear = true;
      break;
    }
    if (!clipped_ear) return false;
  }
  if (indices.size() != 3) return false;
  const Triangle2D final_triangle = {polygon[indices[0]], polygon[indices[1]], polygon[indices[2]]};
  if (cross2D(final_triangle[0], final_triangle[1], final_triangle[2]) <= kEpsilon) return false;
  triangles.push_back(final_triangle);
  return true;
}

enum class ClipBoundary { Left, Right, Bottom, Top };

bool insideClipBoundary(const glm::dvec2& point, ClipBoundary boundary, double value) {
  switch (boundary) {
    case ClipBoundary::Left:
      return point.x >= value;
    case ClipBoundary::Right:
      return point.x <= value;
    case ClipBoundary::Bottom:
      return point.y >= value;
    case ClipBoundary::Top:
      return point.y <= value;
  }
  return false;
}

glm::dvec2 clipIntersection(const glm::dvec2& a,
                            const glm::dvec2& b,
                            ClipBoundary boundary,
                            double value) {
  if (boundary == ClipBoundary::Left || boundary == ClipBoundary::Right) {
    const double denominator = b.x - a.x;
    const double t = std::abs(denominator) <= 1.0e-15 ? 0.0 : (value - a.x) / denominator;
    return {value, a.y + (b.y - a.y) * t};
  }
  const double denominator = b.y - a.y;
  const double t = std::abs(denominator) <= 1.0e-15 ? 0.0 : (value - a.y) / denominator;
  return {a.x + (b.x - a.x) * t, value};
}

std::vector<glm::dvec2> clipPolygon(const std::vector<glm::dvec2>& input,
                                    ClipBoundary boundary,
                                    double value) {
  std::vector<glm::dvec2> output;
  if (input.empty()) return output;
  output.reserve(input.size() + 1);
  glm::dvec2 previous = input.back();
  bool previous_inside = insideClipBoundary(previous, boundary, value);
  for (const glm::dvec2& current : input) {
    const bool current_inside = insideClipBoundary(current, boundary, value);
    if (current_inside != previous_inside) {
      output.push_back(clipIntersection(previous, current, boundary, value));
    }
    if (current_inside) output.push_back(current);
    previous = current;
    previous_inside = current_inside;
  }
  return output;
}

std::vector<glm::dvec2> clipTriangleToCell(const Triangle2D& triangle,
                                           double min_x,
                                           double max_x,
                                           double min_y,
                                           double max_y) {
  std::vector<glm::dvec2> clipped(triangle.begin(), triangle.end());
  clipped = clipPolygon(clipped, ClipBoundary::Left, min_x);
  clipped = clipPolygon(clipped, ClipBoundary::Right, max_x);
  clipped = clipPolygon(clipped, ClipBoundary::Bottom, min_y);
  return clipPolygon(clipped, ClipBoundary::Top, max_y);
}
}  // namespace

double groundDistance(const glm::dvec3& a, const glm::dvec3& b, bool geo) {
  if (geo) {
    const auto ga = coordinate::ecefToGeodetic(a);
    const auto gb = coordinate::ecefToGeodetic(b);
    return haversine(ga.lat, ga.lon, gb.lat, gb.lon);
  }
  // 本地:水平面(XY)距离。
  const double dx = b.x - a.x, dy = b.y - a.y;
  return std::sqrt(dx * dx + dy * dy);
}

MeasureResult compute(MeasureKind kind, const std::vector<glm::dvec3>& pts, bool geo) {
  MeasureResult r;
  switch (kind) {
    case MeasureKind::Point: {
      if (pts.empty()) return r;
      r.valid = true;
      r.local = pts[0];
      if (geo) {
        const auto g = coordinate::ecefToGeodetic(pts[0]);
        r.lat_deg = g.lat * 180.0 / kPi;
        r.lon_deg = g.lon * 180.0 / kPi;
        r.alt_m   = g.alt;
      }
      return r;
    }
    case MeasureKind::Distance: {
      if (pts.size() < 2) return r;
      r.valid = true;
      for (size_t i = 1; i < pts.size(); ++i) {
        const double s = groundDistance(pts[i - 1], pts[i], geo);
        r.seg_len.push_back(s);
        r.total_len += s;
      }
      // 两点时额外给 3D 斜距(含高差)。
      if (pts.size() == 2)
        r.slant_len = glm::length(pts[1] - pts[0]);
      return r;
    }
    case MeasureKind::Height: {
      if (pts.size() < 2) return r;
      r.valid = true;
      const auto& a = pts[0];
      const auto& b = pts[1];
      r.slant_len   = glm::length(b - a);
      r.horizontal  = groundDistance(a, b, geo);
      if (geo) {
        const auto ga = coordinate::ecefToGeodetic(a);
        const auto gb = coordinate::ecefToGeodetic(b);
        r.height = gb.alt - ga.alt;       // 椭球高差
      } else {
        r.height = b.z - a.z;             // 本地 Δz
      }
      return r;
    }
    case MeasureKind::Area: {
      if (pts.size() < 3) return r;
      r.valid = true;
      // 周长(闭合环,每段地表距离)。
      for (size_t i = 0; i < pts.size(); ++i) {
        const double s = groundDistance(pts[i], pts[(i + 1) % pts.size()], geo);
        r.seg_len.push_back(s);
        r.total_len += s;
      }
      // 面积:投影到一个平面做 shoelace。geo 投到形心处 ENU 切平面(EN),
      // 本地直接用 XY 平面。
      std::vector<glm::dvec2> en;
      en.reserve(pts.size());
      if (geo) {
        glm::dvec3 c(0.0);
        for (const auto& p : pts) c += p;
        c /= static_cast<double>(pts.size());
        const auto frame = coordinate::enuFrameAt(coordinate::ecefToGeodetic(c));
        for (const auto& p : pts) {
          const glm::dvec3 l = frame.ecefToLocal(p);
          en.emplace_back(l.x, l.y);
        }
      } else {
        for (const auto& p : pts) en.emplace_back(p.x, p.y);
      }
      double a2 = 0.0;
      for (size_t i = 0; i < en.size(); ++i) {
        const auto& p0 = en[i];
        const auto& p1 = en[(i + 1) % en.size()];
        a2 += p0.x * p1.y - p1.x * p0.y;
      }
      r.area = std::abs(a2) * 0.5;
      return r;
    }
  }
  return r;
}

TerrainVolumeResult computeTerrainVolume(const std::vector<glm::dvec2>& footprint,
                                         const TerrainVolumeOptions& options,
                                         const TerrainHeightSampler& terrain_height) {
  TerrainVolumeResult result;
  result.reference_elevation_m = options.reference_elevation_m;
  if (footprint.size() < 3 || !terrain_height || !std::isfinite(options.reference_elevation_m) ||
      !std::isfinite(options.sample_spacing_m) || options.sample_spacing_m <= 0.0 || options.maximum_cells == 0) {
    return result;
  }
  std::vector<glm::dvec2> polygon;
  polygon.reserve(footprint.size());
  for (const glm::dvec2& point : footprint) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return result;
    if (polygon.empty() || !samePoint(polygon.back(), point)) polygon.push_back(point);
  }
  if (polygon.size() > 1 && samePoint(polygon.front(), polygon.back())) polygon.pop_back();
  if (polygon.size() < 3 || !isSimplePolygon(polygon)) return result;

  std::vector<Triangle2D> triangles;
  if (!triangulateSimplePolygon(polygon, triangles)) return result;

  double footprint_area = 0.0;
  glm::dvec2 ignored_centroid(0.0);
  if (!polygonAreaCentroid(polygon, footprint_area, ignored_centroid)) return result;

  double min_x = polygon.front().x;
  double max_x = polygon.front().x;
  double min_y = polygon.front().y;
  double max_y = polygon.front().y;
  for (const glm::dvec2& point : polygon) {
    min_x = std::min(min_x, point.x);
    max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y);
    max_y = std::max(max_y, point.y);
  }
  const double width = max_x - min_x;
  const double height = max_y - min_y;
  if (width <= 0.0 || height <= 0.0) return result;

  const double column_count = std::ceil(width / options.sample_spacing_m);
  const double row_count = std::ceil(height / options.sample_spacing_m);
  if (!std::isfinite(column_count) || !std::isfinite(row_count) ||
      column_count > static_cast<double>(options.maximum_cells) ||
      row_count > static_cast<double>(options.maximum_cells)) {
    return result;
  }
  const std::size_t columns = static_cast<std::size_t>(column_count);
  const std::size_t rows = static_cast<std::size_t>(row_count);
  if (columns == 0 || rows == 0 || columns > options.maximum_cells / rows) return result;
  result.grid_cell_count = columns * rows;
  if (result.grid_cell_count > options.maximum_cells) return result;

  result.footprint_area_m2 = footprint_area;
  result.min_terrain_elevation_m = std::numeric_limits<double>::infinity();
  result.max_terrain_elevation_m = -std::numeric_limits<double>::infinity();
  for (std::size_t row = 0; row < rows; ++row) {
    const double cell_min_y = min_y + static_cast<double>(row) * options.sample_spacing_m;
    const double cell_max_y = std::min(max_y, cell_min_y + options.sample_spacing_m);
    for (std::size_t column = 0; column < columns; ++column) {
      const double cell_min_x = min_x + static_cast<double>(column) * options.sample_spacing_m;
      const double cell_max_x = std::min(max_x, cell_min_x + options.sample_spacing_m);
      bool cell_contributed = false;
      for (const Triangle2D& triangle : triangles) {
        const std::vector<glm::dvec2> fragment =
            clipTriangleToCell(triangle, cell_min_x, cell_max_x, cell_min_y, cell_max_y);
        double fragment_area = 0.0;
        glm::dvec2 fragment_centroid(0.0);
        if (!polygonAreaCentroid(fragment, fragment_area, fragment_centroid) || fragment_area <= 1.0e-12) continue;

        const double terrain_elevation = terrain_height(fragment_centroid.x, fragment_centroid.y);
        if (!std::isfinite(terrain_elevation)) return TerrainVolumeResult{};
        const double elevation_delta = terrain_elevation - options.reference_elevation_m;
        result.sampled_area_m2 += fragment_area;
        result.min_terrain_elevation_m = std::min(result.min_terrain_elevation_m, terrain_elevation);
        result.max_terrain_elevation_m = std::max(result.max_terrain_elevation_m, terrain_elevation);
        if (elevation_delta >= 0.0)
          result.cut_volume_m3 += elevation_delta * fragment_area;
        else
          result.fill_volume_m3 += -elevation_delta * fragment_area;
        ++result.terrain_sample_count;
        cell_contributed = true;
      }
      if (cell_contributed) ++result.contributing_cell_count;
    }
  }

  if (result.terrain_sample_count == 0 ||
      std::abs(result.sampled_area_m2 - result.footprint_area_m2) >
          std::max(1.0e-7, result.footprint_area_m2 * 1.0e-9)) {
    return TerrainVolumeResult{};
  }
  result.net_volume_m3 = result.cut_volume_m3 - result.fill_volume_m3;
  result.valid = true;
  return result;
}

}  // namespace hongtu::measure
