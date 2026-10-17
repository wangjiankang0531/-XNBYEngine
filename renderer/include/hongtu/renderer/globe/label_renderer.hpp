#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace hongtu::renderer {

class GlobeCamera;
using namespace HongTu::RHI;

struct Label {
  coordinate::Geodetic position;  ///< Geodetic position (lat/lon in radians, alt in meters)
  std::string text;               ///< Label text (rendered as colored quad placeholder)
  glm::vec4 color{1.0f, 1.0f, 0.0f, 1.0f};  ///< Quad color (RGBA)
  f32 size_meters = 50000.0f;     ///< Billboard size in world meters (visible from space)
  f32 priority = 1.0f;            ///< Higher priority = drawn on top (not yet implemented)
};

/// Renders colored billboard quads at geodetic positions.
/// Placeholder for future SDF text rendering.
class LabelRenderer {
 public:
  LabelRenderer() = default;
  ~LabelRenderer() { cleanup(); }

  LabelRenderer(const LabelRenderer&) = delete;
  LabelRenderer& operator=(const LabelRenderer&) = delete;

  bool init(RHIDevice* device, RHIRenderPassPtr render_pass);
  void cleanup();

  void addLabel(const Label& label);
  void clearLabels();

  /// Rebuild GPU buffers (call after labels change or RTC origin shifts).
  void update(const GlobeCamera& camera);

  /// Draw all labels.
  void render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect);

 private:
  struct PushData {
    glm::mat4 mvp;    // 64 bytes
    glm::vec4 color;  // 16 bytes
  };  // total: 80 bytes

  struct BillboardVertex {
    glm::vec3 position;  // RTC space
  };

  void rebuildBuffers(const GlobeCamera& camera);
  bool createPipeline(RHIRenderPassPtr render_pass);

  RHIDevice* device_ = nullptr;
  RHIShaderPtr vs_;
  RHIShaderPtr fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr pipeline_;

  std::vector<Label> labels_;
  RHIBufferPtr vertex_buffer_;
  RHIBufferPtr index_buffer_;
  u32 index_count_ = 0;

  glm::dvec3 cached_rtc_origin_{0.0};
  bool initialized_ = false;
  bool dirty_ = true;
};

}  // namespace hongtu::renderer
