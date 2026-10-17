#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <glm/glm.hpp>

#include <map>
#include <vector>

namespace hongtu::renderer {

class GlobeCamera;

using namespace HongTu::RHI;

/// Visual style of one water body.
struct WaterStyle {
  /// Base water color (linear) and opacity. Alpha is the near-nadir opacity;
  /// the shader adds a Fresnel boost so grazing views read more reflective.
  glm::vec4 color{0.05f, 0.18f, 0.24f, 0.62f};
};

/// Build the RTC triangle list (fan) for a water-body boundary. The boundary's
/// per-vertex `alt` is the water level — lakes are flat, so callers normally
/// set one altitude on every vertex. Pure function for unit tests; the
/// renderer calls it on RTC-origin changes.
///
/// Same P0 contract as `triangulateFan`: convex or star-shaped around the
/// first vertex. Returns empty for fewer than 3 vertices.
std::vector<glm::vec3> buildWaterSurfaceRtcVertices(
    const std::vector<coordinate::Geodetic>& boundary,
    const coordinate::RTCOrigin& origin,
    const coordinate::Ellipsoid& ellipsoid = coordinate::WGS84);

/// Renders local water bodies (lakes / rivers / flood polygons) as
/// alpha-blended, water-shaded surfaces on the globe. M3 of the geo-water
/// design (see docs/superpowers/specs/2026-06-09-geo-water-design.md).
///
/// Owned by GlobeRenderer (same pattern as AtmosphereRenderer): callers add
/// explicit geodetic boundary polygons (footprints — typically from polygon
/// annotations or GIS data); each is fan-triangulated at its water level and
/// shaded with the shared water core (Fresnel sky reflection + sun glint) at
/// close-range ripple scale. mask = 1 everywhere — the footprint IS the
/// water authority, no color classification.
class WaterSurfaceRenderer {
 public:
  WaterSurfaceRenderer() = default;
  ~WaterSurfaceRenderer();

  WaterSurfaceRenderer(const WaterSurfaceRenderer&) = delete;
  WaterSurfaceRenderer& operator=(const WaterSurfaceRenderer&) = delete;

  bool init(RHIDevice* device, RHIRenderPassPtr render_pass);

  /// Add a water body. Returns its id (>= 0), or -1 for a degenerate boundary.
  int  addWaterBody(std::vector<coordinate::Geodetic> boundary,
                    const WaterStyle& style = {});
  void removeWaterBody(int id);
  void clearWaterBodies();
  bool empty() const { return bodies_.empty(); }

  /// Rebuild RTC meshes when the camera RTC origin has moved.
  void update(const GlobeCamera& camera);

  /// Record draws. Call inside the render pass, after the opaque globe/tiles
  /// (the surface alpha-blends over them, depth-tested, no depth write).
  void render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect,
              const glm::vec3& sun_dir_ecef, f32 time_s);

  void cleanup();

 private:
  struct Body {
    std::vector<coordinate::Geodetic> boundary;
    WaterStyle   style;
    RHIBufferPtr vbo;
    u32          vertex_count = 0;
    bool         dirty        = true;  // (re)build vbo on next update
  };

  struct WaterPushData {
    glm::mat4 mvp;           // 64 B
    glm::vec4 sun_dir_time;  // xyz = sun dir ECEF, w = time (s)
    glm::vec4 cam_rtc;       // xyz = camera in RTC
    glm::vec4 rtc_origin;    // xyz = RTC origin in ECEF
    glm::vec4 color;         // rgb = water color, a = base opacity
  };
  static_assert(sizeof(WaterPushData) == 128, "must fit Vulkan 128B push range");

  void rebuildBody(Body& body, const coordinate::RTCOrigin& origin);

  RHIDevice* device_ = nullptr;
  bool initialized_  = false;

  RHIShaderPtr         vs_;
  RHIShaderPtr         fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;

  std::map<int, Body> bodies_;
  int        next_id_ = 0;
  glm::dvec3 cached_rtc_origin_{0.0};
};

}  // namespace hongtu::renderer
