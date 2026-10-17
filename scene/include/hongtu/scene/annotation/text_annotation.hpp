#pragma once

#include <hongtu/scene/annotation/annotation.hpp>
#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>

namespace hongtu::scene {

/// Text annotation — a single anchor point + text payload in `style().text`.
/// P0 limited to ASCII (English / digits) — Chinese / Unicode is a future
/// SDF-font task. See spec §11.
class TextAnnotation : public Annotation {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Text; }

  AnnotationRenderPayload toRenderPayload(
      const coordinate::PrecisionOrigin& origin) const override {
    AnnotationRenderPayload p;
    p.kind         = AnnotationKind::Text;
    p.rtc_vertices = { getRtcAnchor(origin) };
    p.style        = style_;
    p.id           = id_;
    p.visible      = visible_;
    return p;
  }

 protected:
  virtual glm::vec3
      getRtcAnchor(const coordinate::PrecisionOrigin& origin) const = 0;
};

class GeoTextAnnotation : public TextAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Geo; }

  void setAnchor(const coordinate::Geodetic& g) { anchor_ = g; }
  const coordinate::Geodetic& getAnchor() const { return anchor_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  glm::vec3 getRtcAnchor(const coordinate::PrecisionOrigin& origin) const override;

 private:
  coordinate::Geodetic anchor_{};
};

class LocalTextAnnotation : public TextAnnotation {
 public:
  AnnotationMode getMode() const override { return AnnotationMode::Local; }

  void setAnchor(const glm::dvec3& p) { anchor_ = p; }
  const glm::dvec3& getAnchor() const { return anchor_; }

  nlohmann::json toJson() const override;
  void           fromJson(const nlohmann::json& j) override;

 protected:
  glm::vec3 getRtcAnchor(const coordinate::PrecisionOrigin& origin) const override;

 private:
  glm::dvec3 anchor_{0.0};
};

}  // namespace hongtu::scene
