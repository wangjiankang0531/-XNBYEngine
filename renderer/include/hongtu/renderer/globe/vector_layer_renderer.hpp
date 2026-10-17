#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/geometry.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <vector>

namespace hongtu::renderer {

class GlobeCamera;

using namespace HongTu::RHI;

/// Visual style for a vector layer.
struct VectorStyle {
  glm::vec4 line_color{1.0f, 1.0f, 0.0f, 1.0f};   ///< Outline / line color (default yellow)
  glm::vec4 fill_color{1.0f, 1.0f, 0.0f, 0.25f};  ///< Fill color for polygons (translucent)
  bool show_fill = false;                             ///< Enable filled polygon rendering
};

/// Renders GIS vector features (Point, LineString, Polygon) on the globe surface.
///
/// Geographic coordinates (lon/lat degrees) are converted to ECEF via WGS84 and
/// stored in RTC space for GPU float32 precision. GPU buffers are rebuilt whenever
/// the RTC origin changes.
///
/// Two pipelines share a single vertex format (position-only):
///   - Line pipeline  (LineList)     — outlines and line strings
///   - Fill pipeline  (TriangleList) — polygon fills via triangle fan
///
/// Both render with depth-test enabled and depth-write disabled so they composite
/// on top of the globe without disturbing the depth buffer.
class VectorLayerRenderer {
 public:
  VectorLayerRenderer() = default;
  ~VectorLayerRenderer() { cleanup(); }

  VectorLayerRenderer(const VectorLayerRenderer&) = delete;
  VectorLayerRenderer& operator=(const VectorLayerRenderer&) = delete;

  bool init(RHIDevice* device, RHIRenderPassPtr render_pass);

  /// Add a feature collection as a named layer. Returns the layer ID.
  int addLayer(const gis::FeatureCollection& fc, const VectorStyle& style = {});

  /// Remove a layer by ID.
  void removeLayer(int layer_id);

  /// Remove all layers.
  void clearLayers();

  /// Rebuild GPU buffers if the RTC origin has changed.
  void update(const GlobeCamera& camera);

  /// Record draw commands. Call after globe, before atmosphere.
  void render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect);

  void cleanup();

 private:
  struct GPULayer {
    int id = -1;
    gis::FeatureCollection features;
    VectorStyle style;

    RHIBufferPtr line_vbuf;
    u32 line_vertex_count = 0;

    RHIBufferPtr fill_vbuf;
    u32 fill_vertex_count = 0;
  };

  struct PushData {
    glm::mat4 mvp;    // 64 bytes
    glm::vec4 color;  // 16 bytes
  };  // total: 80 bytes

  void buildLayer(GPULayer& layer, const glm::dvec3& rtc_origin);
  void buildGeometry(const gis::Geometry& geom, const glm::dvec3& rtc_origin,
                     std::vector<glm::vec3>& line_verts,
                     std::vector<glm::vec3>& fill_verts);
  RHIBufferPtr uploadBuffer(const std::vector<glm::vec3>& verts);
  bool createPipelines(RHIRenderPassPtr render_pass);

  RHIDevice* device_ = nullptr;

  RHIShaderPtr vs_;
  RHIShaderPtr fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr line_pipeline_;
  RHIPipelinePtr fill_pipeline_;

  std::vector<GPULayer> layers_;
  glm::dvec3 cached_rtc_origin_{0.0};
  int next_id_ = 0;
  bool initialized_ = false;
};

}  // namespace hongtu::renderer
