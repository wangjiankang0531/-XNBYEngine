#include <hongtu/gis/geometry.hpp>
#include <algorithm>
#include <limits>

namespace hongtu::gis {

const char* geometryTypeName(GeometryType type) {
  switch (type) {
    case GeometryType::Point:              return "Point";
    case GeometryType::MultiPoint:         return "MultiPoint";
    case GeometryType::LineString:          return "LineString";
    case GeometryType::MultiLineString:    return "MultiLineString";
    case GeometryType::Polygon:            return "Polygon";
    case GeometryType::MultiPolygon:       return "MultiPolygon";
    case GeometryType::GeometryCollection: return "GeometryCollection";
    case GeometryType::Unknown:            return "Unknown";
  }
  return "Unknown";
}

void Geometry::getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const {
  min_x = min_y = std::numeric_limits<f64>::max();
  max_x = max_y = std::numeric_limits<f64>::lowest();

  auto update = [&](f64 x, f64 y) {
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
  };

  switch (type) {
    case GeometryType::Point:
      update(point.x, point.y);
      break;
    case GeometryType::MultiPoint:
    case GeometryType::LineString:
      for (const auto& p : points) update(p.x, p.y);
      break;
    case GeometryType::MultiLineString:
    case GeometryType::Polygon:
    case GeometryType::MultiPolygon:
      for (const auto& ring : rings) {
        for (const auto& p : ring) update(p.x, p.y);
      }
      break;
    case GeometryType::GeometryCollection:
      for (const auto& g : geometries) {
        f64 gx0, gy0, gx1, gy1;
        g.getBounds(gx0, gy0, gx1, gy1);
        min_x = std::min(min_x, gx0);
        min_y = std::min(min_y, gy0);
        max_x = std::max(max_x, gx1);
        max_y = std::max(max_y, gy1);
      }
      break;
    case GeometryType::Unknown:
      break;
  }
}

u32 Geometry::getVertexCount() const {
  switch (type) {
    case GeometryType::Point: return 1;
    case GeometryType::MultiPoint:
    case GeometryType::LineString:
      return static_cast<u32>(points.size());
    case GeometryType::MultiLineString:
    case GeometryType::Polygon:
    case GeometryType::MultiPolygon: {
      u32 count = 0;
      for (const auto& ring : rings) count += static_cast<u32>(ring.size());
      return count;
    }
    case GeometryType::GeometryCollection: {
      u32 count = 0;
      for (const auto& g : geometries) count += g.getVertexCount();
      return count;
    }
    case GeometryType::Unknown: return 0;
  }
  return 0;
}

void FeatureCollection::getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const {
  min_x = min_y = std::numeric_limits<f64>::max();
  max_x = max_y = std::numeric_limits<f64>::lowest();

  for (const auto& f : features) {
    f64 fx0, fy0, fx1, fy1;
    f.geometry.getBounds(fx0, fy0, fx1, fy1);
    min_x = std::min(min_x, fx0);
    min_y = std::min(min_y, fy0);
    max_x = std::max(max_x, fx1);
    max_y = std::max(max_y, fy1);
  }
}

} // namespace hongtu::gis
