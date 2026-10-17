#pragma once

#include <hongtu/scene/annotation/annotation.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace hongtu::scene {

/// Polyline annotation — N vertices, drawn as a connected ribbon by
/// `PolylineRenderer`. Closed polylines are expressed via the style
/// (no separate field).
///
/// Geo and Local variants store vertices in their native coordinate system.
class PolylineAnnotation : public Annotation {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Polyline; }

  /// Vertex count — same regardless of mode (subclass forwards).
  virtual std::size_t vertexCount() const = 0;

  AnnotationRenderPayload toRenderPayload(
      const coordinate::PrecisionOrigin& origin) const override {
    AnnotationRenderPayload p;
    p.kind         = AnnotationKind::Polyline;
    p.rtc_vertices = getRtcVertices(origin);
    p.style        = style_;
    p.id           = id_;
    p.visible      = visible_;
    return p;
  }

 protected:
  /// Coordinate-specific lowering. Implemented by Geo / Local variants.
  virtual std::vector<glm::vec3>
      getRtcVertices(const coordinate::PrecisionOrigin& origin) const = 0;
};

// =============================================================================
// GeoPolylineAnnotation — vertices stored as Geodetic; lower via ECEF → RTC.
// =============================================================================

class GeoPolylineAnnotation : public PolylineAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Geo; }
  std::size_t    vertexCount() const override { return vertices_.size(); }

  void setVertices(std::vector<coordinate::Geodetic> v) { vertices_ = std::move(v); }
  void addVertex(const coordinate::Geodetic& g)         { vertices_.push_back(g); }
  void clearVertices()                                   { vertices_.clear(); }
  const std::vector<coordinate::Geodetic>& getVertices() const { return vertices_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  std::vector<glm::vec3>
      getRtcVertices(const coordinate::PrecisionOrigin& origin) const override;

 private:
  std::vector<coordinate::Geodetic> vertices_;
};

// =============================================================================
// LocalPolylineAnnotation — vertices stored as dvec3 flat-world meters.
// =============================================================================

class LocalPolylineAnnotation : public PolylineAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Local; }
  std::size_t    vertexCount() const override { return vertices_.size(); }

  void setVertices(std::vector<glm::dvec3> v) { vertices_ = std::move(v); }
  void addVertex(const glm::dvec3& p)         { vertices_.push_back(p); }
  void clearVertices()                         { vertices_.clear(); }
  const std::vector<glm::dvec3>& getVertices() const { return vertices_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  std::vector<glm::vec3>
      getRtcVertices(const coordinate::PrecisionOrigin& origin) const override;

 private:
  std::vector<glm::dvec3> vertices_;
};

}  // namespace hongtu::scene
