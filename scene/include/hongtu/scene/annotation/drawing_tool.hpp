#pragma once

#include <hongtu/scene/annotation/annotation.hpp>
#include <hongtu/scene/picking/picking_service.hpp>
#include <hongtu/renderer/camera/camera_base.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace hongtu::scene {

/// Per-frame mouse/keyboard snapshot consumed by drawing tools. Decoupled
/// from SDL2/Qt — caller fills it from whatever windowing layer they use.
///
/// Boolean fields are level-triggered ("currently held") except for
/// `*_clicked` which are edge-triggered ("became up this frame").
struct DrawInputState {
  int  mouse_x         = 0;
  int  mouse_y         = 0;
  bool left_down       = false;
  bool left_clicked    = false;   ///< released this frame after press
  bool right_clicked   = false;   ///< right click — completes polygon / cancels
  bool escape_pressed  = false;
  bool enter_pressed   = false;
  int  viewport_w      = 1280;
  int  viewport_h      = 720;
};

/// State machine for building a single annotation through mouse / keyboard
/// input. Concrete subclasses correspond to the 5 annotation kinds.
///
/// Flow:
///   1. Caller `tick()`s the tool each frame with the latest input.
///   2. The tool consumes the input, possibly emitting a finished annotation
///      that the caller picks up via `takeCompleted()`.
///   3. While in-progress, the tool exposes a preview render payload so the
///      demo can show a ghost annotation under the cursor.
class DrawingTool {
 public:
  virtual ~DrawingTool() = default;

  virtual AnnotationKind getKind() const = 0;

  /// Per-frame advance. The tool may convert screen-clicks into world points
  /// via `camera + viewport` and either pickGround (if `picking == nullptr`)
  /// or attempt entity picking through `picking` first.
  ///
  /// `mode` decides how world hits become annotation vertices:
  ///   - Geo   → ECEF → ecefToGeodetic → store as Geodetic
  ///   - Local → store world hit (dvec3) directly
  virtual void tick(const DrawInputState& input,
                    const renderer::CameraBase& cam,
                    AnnotationMode mode,
                    const PickingService* picking = nullptr) = 0;

  /// Pull a finished annotation out of the tool (returns nullptr if none
  /// has been emitted yet). Caller takes ownership; the tool resets to idle.
  virtual std::unique_ptr<Annotation> takeCompleted() { return nullptr; }

  /// Optional preview — the tool's in-progress geometry, ready to render
  /// as a ghost annotation. Returns nullopt when idle.
  virtual std::optional<AnnotationRenderPayload>
      previewPayload(const coordinate::PrecisionOrigin& origin) const {
    (void)origin; return std::nullopt;
  }

  /// ESC pressed — drop in-progress geometry without emitting.
  virtual void cancel() = 0;

  bool isDrawing() const { return drawing_; }

 protected:
  DrawingTool() = default;
  bool drawing_ = false;
};

// =============================================================================
// 5 concrete tools — each tied to one AnnotationKind.
//
// Common pattern:
//   - PolylineTool / PolygonTool: click N points; right-click / Enter close;
//     ESC cancel
//   - SymbolTool / TextTool: single click → emit
//   - ArrowTool: 2 clicks (start, end) → emit
// =============================================================================

class PolylineTool : public DrawingTool {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Polyline; }
  void tick(const DrawInputState& input,
            const renderer::CameraBase& cam,
            AnnotationMode mode,
            const PickingService* picking = nullptr) override;
  std::unique_ptr<Annotation> takeCompleted() override;
  std::optional<AnnotationRenderPayload>
      previewPayload(const coordinate::PrecisionOrigin& origin) const override;
  void cancel() override;

 private:
  std::vector<glm::dvec3>  world_points_;     ///< accumulated world clicks
  AnnotationMode           mode_ = AnnotationMode::Local;
  std::unique_ptr<Annotation> completed_;
};

class PolygonTool : public DrawingTool {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Polygon; }
  void tick(const DrawInputState& input,
            const renderer::CameraBase& cam,
            AnnotationMode mode,
            const PickingService* picking = nullptr) override;
  std::unique_ptr<Annotation> takeCompleted() override;
  std::optional<AnnotationRenderPayload>
      previewPayload(const coordinate::PrecisionOrigin& origin) const override;
  void cancel() override;

 private:
  std::vector<glm::dvec3>     world_points_;
  AnnotationMode              mode_ = AnnotationMode::Local;
  std::unique_ptr<Annotation> completed_;
};

class SymbolTool : public DrawingTool {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Symbol; }
  void tick(const DrawInputState& input,
            const renderer::CameraBase& cam,
            AnnotationMode mode,
            const PickingService* picking = nullptr) override;
  std::unique_ptr<Annotation> takeCompleted() override;
  void cancel() override { drawing_ = false; completed_.reset(); }

  void setSymbolIndex(uint32_t i) { symbol_index_ = i; }

 private:
  uint32_t symbol_index_ = 0;
  std::unique_ptr<Annotation> completed_;
};

class TextTool : public DrawingTool {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Text; }
  void tick(const DrawInputState& input,
            const renderer::CameraBase& cam,
            AnnotationMode mode,
            const PickingService* picking = nullptr) override;
  std::unique_ptr<Annotation> takeCompleted() override;
  void cancel() override { drawing_ = false; completed_.reset(); }

  /// Demo / editor must supply the text BEFORE the next click (e.g. show a
  /// prompt). On click the tool emits an annotation with this text.
  void setText(std::string t) { text_ = std::move(t); }

 private:
  std::string text_ = "Label";
  std::unique_ptr<Annotation> completed_;
};

class ArrowTool : public DrawingTool {
 public:
  AnnotationKind getKind() const override { return AnnotationKind::Arrow; }
  void tick(const DrawInputState& input,
            const renderer::CameraBase& cam,
            AnnotationMode mode,
            const PickingService* picking = nullptr) override;
  std::unique_ptr<Annotation> takeCompleted() override;
  std::optional<AnnotationRenderPayload>
      previewPayload(const coordinate::PrecisionOrigin& origin) const override;
  void cancel() override;

 private:
  std::optional<glm::dvec3>   start_;          ///< populated after first click
  AnnotationMode              mode_ = AnnotationMode::Local;
  std::unique_ptr<Annotation> completed_;
};

}  // namespace hongtu::scene
