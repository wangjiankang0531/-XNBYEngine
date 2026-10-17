#include <hongtu/scene/annotation/drawing_tool.hpp>

#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>
#include <hongtu/coordinate/geodetic.hpp>

namespace hongtu::scene {

namespace {

/// Convert a world-space (f64) hit into the appropriate annotation vertex form.
/// Geo annotations need ECEF → geodetic; Local annotations take the world dvec3
/// straight. The caller's mode picks the path.

bool pickWorldHit(const DrawInputState& input,
                  const renderer::CameraBase& cam,
                  const PickingService* picking,
                  glm::dvec3& out) {
  // Prefer entity picking if a service is provided AND something is hit;
  // fall back to ground / globe pickWorld.
  if (picking) {
    auto r = picking->pickAtScreen(input.mouse_x, input.mouse_y, cam,
                                   input.viewport_w, input.viewport_h);
    if (r.hit) { out = r.world_hit; return true; }
  }
  auto h = cam.pickWorld(input.mouse_x, input.mouse_y,
                         input.viewport_w, input.viewport_h);
  if (!h.has_value()) return false;
  out = h->point;
  return true;
}

void addPointGeo(std::vector<coordinate::Geodetic>& dst, const glm::dvec3& world_hit) {
  // For GlobeCamera::pickWorld, world_hit is an ECEF point on/near the sphere.
  dst.push_back(coordinate::ecefToGeodetic(world_hit));
}

}  // namespace

// =============================================================================
// PolylineTool
// =============================================================================

void PolylineTool::tick(const DrawInputState& input,
                        const renderer::CameraBase& cam,
                        AnnotationMode mode,
                        const PickingService* picking) {
  mode_ = mode;

  if (input.escape_pressed) { cancel(); return; }

  if (input.left_clicked) {
    glm::dvec3 hit;
    if (pickWorldHit(input, cam, picking, hit)) {
      world_points_.push_back(hit);
      drawing_ = true;
    }
  }

  if (drawing_ && (input.right_clicked || input.enter_pressed)) {
    if (world_points_.size() >= 2) {
      if (mode_ == AnnotationMode::Geo) {
        auto a = std::make_unique<GeoPolylineAnnotation>();
        std::vector<coordinate::Geodetic> g;
        g.reserve(world_points_.size());
        for (const auto& p : world_points_) addPointGeo(g, p);
        a->setVertices(std::move(g));
        completed_ = std::move(a);
      } else {
        auto a = std::make_unique<LocalPolylineAnnotation>();
        a->setVertices(world_points_);
        completed_ = std::move(a);
      }
    }
    world_points_.clear();
    drawing_ = false;
  }
}

std::unique_ptr<Annotation> PolylineTool::takeCompleted() {
  return std::move(completed_);
}

std::optional<AnnotationRenderPayload>
PolylineTool::previewPayload(const coordinate::PrecisionOrigin& origin) const {
  if (world_points_.empty()) return std::nullopt;
  AnnotationRenderPayload p;
  p.kind    = AnnotationKind::Polyline;
  p.visible = true;
  p.rtc_vertices.reserve(world_points_.size());
  for (const auto& w : world_points_) p.rtc_vertices.push_back(origin.worldToLocal(w));
  return p;
}

void PolylineTool::cancel() {
  world_points_.clear();
  drawing_ = false;
  completed_.reset();
}

// =============================================================================
// PolygonTool
// =============================================================================

void PolygonTool::tick(const DrawInputState& input,
                       const renderer::CameraBase& cam,
                       AnnotationMode mode,
                       const PickingService* picking) {
  mode_ = mode;

  if (input.escape_pressed) { cancel(); return; }

  if (input.left_clicked) {
    glm::dvec3 hit;
    if (pickWorldHit(input, cam, picking, hit)) {
      world_points_.push_back(hit);
      drawing_ = true;
    }
  }

  if (drawing_ && (input.right_clicked || input.enter_pressed)) {
    if (world_points_.size() >= 3) {
      if (mode_ == AnnotationMode::Geo) {
        auto a = std::make_unique<GeoPolygonAnnotation>();
        std::vector<coordinate::Geodetic> g;
        g.reserve(world_points_.size());
        for (const auto& p : world_points_) addPointGeo(g, p);
        a->setVertices(std::move(g));
        completed_ = std::move(a);
      } else {
        auto a = std::make_unique<LocalPolygonAnnotation>();
        a->setVertices(world_points_);
        completed_ = std::move(a);
      }
    }
    world_points_.clear();
    drawing_ = false;
  }
}

std::unique_ptr<Annotation> PolygonTool::takeCompleted() {
  return std::move(completed_);
}

std::optional<AnnotationRenderPayload>
PolygonTool::previewPayload(const coordinate::PrecisionOrigin& origin) const {
  if (world_points_.empty()) return std::nullopt;
  AnnotationRenderPayload p;
  p.kind    = AnnotationKind::Polygon;
  p.visible = true;
  p.rtc_vertices.reserve(world_points_.size());
  for (const auto& w : world_points_) p.rtc_vertices.push_back(origin.worldToLocal(w));
  return p;
}

void PolygonTool::cancel() {
  world_points_.clear();
  drawing_ = false;
  completed_.reset();
}

// =============================================================================
// SymbolTool
// =============================================================================

void SymbolTool::tick(const DrawInputState& input,
                      const renderer::CameraBase& cam,
                      AnnotationMode mode,
                      const PickingService* picking) {
  if (input.escape_pressed) { cancel(); return; }
  if (!input.left_clicked) return;

  glm::dvec3 hit;
  if (!pickWorldHit(input, cam, picking, hit)) return;

  AnnotationStyle s;
  s.symbol_index = symbol_index_;

  if (mode == AnnotationMode::Geo) {
    auto a = std::make_unique<GeoSymbolAnnotation>();
    a->setAnchor(coordinate::ecefToGeodetic(hit));
    a->setStyle(s);
    completed_ = std::move(a);
  } else {
    auto a = std::make_unique<LocalSymbolAnnotation>();
    a->setAnchor(hit);
    a->setStyle(s);
    completed_ = std::move(a);
  }
}

std::unique_ptr<Annotation> SymbolTool::takeCompleted() {
  return std::move(completed_);
}

// =============================================================================
// TextTool
// =============================================================================

void TextTool::tick(const DrawInputState& input,
                    const renderer::CameraBase& cam,
                    AnnotationMode mode,
                    const PickingService* picking) {
  if (input.escape_pressed) { cancel(); return; }
  if (!input.left_clicked) return;

  glm::dvec3 hit;
  if (!pickWorldHit(input, cam, picking, hit)) return;

  AnnotationStyle s;
  s.text = text_;

  if (mode == AnnotationMode::Geo) {
    auto a = std::make_unique<GeoTextAnnotation>();
    a->setAnchor(coordinate::ecefToGeodetic(hit));
    a->setStyle(s);
    completed_ = std::move(a);
  } else {
    auto a = std::make_unique<LocalTextAnnotation>();
    a->setAnchor(hit);
    a->setStyle(s);
    completed_ = std::move(a);
  }
}

std::unique_ptr<Annotation> TextTool::takeCompleted() {
  return std::move(completed_);
}

// =============================================================================
// ArrowTool
// =============================================================================

void ArrowTool::tick(const DrawInputState& input,
                     const renderer::CameraBase& cam,
                     AnnotationMode mode,
                     const PickingService* picking) {
  mode_ = mode;

  if (input.escape_pressed) { cancel(); return; }
  if (!input.left_clicked) return;

  glm::dvec3 hit;
  if (!pickWorldHit(input, cam, picking, hit)) return;

  if (!start_.has_value()) {
    start_ = hit;
    drawing_ = true;
    return;
  }

  // Second click → emit
  glm::dvec3 end = hit;
  if (mode_ == AnnotationMode::Geo) {
    auto a = std::make_unique<GeoArrowAnnotation>();
    a->setEndpoints(coordinate::ecefToGeodetic(*start_),
                    coordinate::ecefToGeodetic(end));
    completed_ = std::move(a);
  } else {
    auto a = std::make_unique<LocalArrowAnnotation>();
    a->setEndpoints(*start_, end);
    completed_ = std::move(a);
  }
  start_.reset();
  drawing_ = false;
}

std::unique_ptr<Annotation> ArrowTool::takeCompleted() {
  return std::move(completed_);
}

std::optional<AnnotationRenderPayload>
ArrowTool::previewPayload(const coordinate::PrecisionOrigin& origin) const {
  if (!start_.has_value()) return std::nullopt;
  AnnotationRenderPayload p;
  p.kind    = AnnotationKind::Arrow;
  p.visible = true;
  p.rtc_vertices.push_back(origin.worldToLocal(*start_));
  return p;
}

void ArrowTool::cancel() {
  start_.reset();
  drawing_ = false;
  completed_.reset();
}

}  // namespace hongtu::scene
