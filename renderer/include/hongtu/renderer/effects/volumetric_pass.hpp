#pragma once

#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using hongtu::coordinate::dispersion::DensityGrid;

/// VolumetricPass — fullscreen ray-march through a 3D density volume.
///
/// Algorithm per pixel:
///   1. Unproject screen ray via inv(view·proj)
///   2. Slab-intersect with the AABB defined by (aabb_min, aabb_max) in RTC
///   3. March N steps inside,sample the 3D density texture,accumulate
///      transmittance with Beer-Lambert + emissive color
///
/// Coordinate-agnostic: caller passes AABB in RTC space; demo-side
/// `VolumetricEntity` lowers world center + extent → RTC AABB before
/// calling `record`.
///
/// The 3D density texture is built once from a `DensityGrid` (CPU-built by
/// `dispersion::buildGaussianPlumeGrid` etc.) and reused per frame.
class VolumetricPass {
 public:
  struct PushConstants {
    glm::mat4 inv_view_proj;     // 64 — unproject NDC to world ray
    glm::vec4 camera_pos_rtc;    // 16 — .xyz
    glm::vec4 aabb_min_density;  // 16 — .xyz aabb_min, .w density_scale
    glm::vec4 aabb_max_steps;    // 16 — .xyz aabb_max, .w num_steps (cast to int)
    glm::vec4 color_sigma;       // 16 — .rgb emissive color, .a extinction sigma
    // 单光源散射 — `light_dir_intensity.w == 0` 时关闭,只走 emissive。
    // Henyey-Greenstein phase function:`g > 0` 前向散射(雾、烟看着透
    // 太阳光,逆光更亮);`g < 0` 后向散射(夜间雾灯);`g = 0` 各向同性。
    glm::vec4 light_dir_intensity; // 16 — .xyz unit dir → light, .w intensity (0 = off)
    glm::vec4 light_color_hg;      // 16 — .rgb light color, .a HG g (-1..1)
  };
  static_assert(sizeof(PushConstants) == 160,
                "VolumetricPass::PushConstants layout mismatch");

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

  /// Upload a CPU density grid to a new GPU 3D texture + view. Caller
  /// keeps the returned handles and hands them back via `record`.
  /// Format is R8_UNORM(grid values × 255 → uint8)— cheap for static
  /// plumes; switch to R16F if you need higher precision later.
  struct VolumeTexture {
    RHITexturePtr     texture;
    RHITextureViewPtr view;
    glm::uvec3        dims{0};
  };
  VolumeTexture uploadDensity(const DensityGrid& g);

  struct Params {
    glm::mat4  view_proj;          ///< RTC view * proj
    glm::vec3  camera_pos_rtc;
    glm::vec3  aabb_min;           ///< RTC space
    glm::vec3  aabb_max;
    glm::vec3  color;              ///< 体积发光颜色(毒气黄绿、烟雾灰白)
    float      density_scale = 1.0f;  ///< 放大 grid 采样值
    float      sigma         = 1.0f;  ///< extinction(consumption per meter)
    int        num_steps     = 48;    ///< march 步数 — 越多越细致也越慢

    // Henyey-Greenstein 单光源散射(intensity=0 关掉,只走 emissive)
    glm::vec3  light_dir       = {0.0f, 0.0f, 1.0f};  ///< unit vector → light
    float      light_intensity = 0.0f;                 ///< 0 = no scattering
    glm::vec3  light_color     = {1.0f, 0.95f, 0.85f}; ///< 太阳光默认暖白
    float      hg_g            = 0.4f;                  ///< 散射各向异性 (-1..1)
  };

  /// Record draws into `cmd`. Caller is responsible for having begun the
  /// scene render pass + bound viewport. The 3D density `view` + sampler
  /// are bound per call (so the same pass can run on multiple volumes in
  /// a frame with different textures).
  void record(const RHICommandBufferPtr& cmd, const Params& p,
              const RHITextureViewPtr& density_view);

 private:
  bool createPipeline(RHIRenderPassPtr render_pass);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_, fs_;
  RHIDescriptorSetLayoutPtr desc_layout_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;
  RHISamplerPtr        density_sampler_;
};

}  // namespace hongtu::renderer
