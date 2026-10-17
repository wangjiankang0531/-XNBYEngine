#pragma once

#include <hongtu/scene/annotation/annotation.hpp>
#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>

namespace hongtu::scene {

/// Arrow annotation — two endpoints; rendered as a polyline with arrow head
/// at the second point. Renderer-side reuses PolylineRenderer's
/// arrow_head support.
class ArrowAnnotation : public Annotation {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Arrow; }

  AnnotationRenderPayload toRenderPayload(
      const coordinate::PrecisionOrigin& origin) const override {
    AnnotationRenderPayload p;
    p.kind         = AnnotationKind::Arrow;
    p.rtc_vertices = getRtcEndpoints(origin);
    p.style        = style_;
    p.id           = id_;
    p.visible      = visible_;
    return p;
  }

 protected:
  /// Always returns exactly 2 points: { start, end }.
  virtual std::vector<glm::vec3>
      getRtcEndpoints(const coordinate::PrecisionOrigin& origin) const = 0;
};

class GeoArrowAnnotation : public ArrowAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Geo; }

  void setEndpoints(const coordinate::Geodetic& start, const coordinate::Geodetic& end) {
    start_ = start; end_ = end;
  }
  const coordinate::Geodetic& getStart() const { return start_; }
  const coordinate::Geodetic& getEnd()   const { return end_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  std::vector<glm::vec3>
      getRtcEndpoints(const coordinate::PrecisionOrigin& origin) const override;

 private:
  coordinate::Geodetic start_{}, end_{};
};

class LocalArrowAnnotation : public ArrowAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Local; }

  void setEndpoints(const glm::dvec3& start, const glm::dvec3& end) {
    start_ = start; end_ = end;
  }
  const glm::dvec3& getStart() const { return start_; }
  const glm::dvec3& getEnd()   const { return end_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  std::vector<glm::vec3>
      getRtcEndpoints(const coordinate::PrecisionOrigin& origin) const override;

 private:
  glm::dvec3 start_{0.0}, end_{0.0};
};

}  // namespace hongtu::scene
