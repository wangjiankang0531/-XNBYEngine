#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// GPU-side PBR material — three sampled textures (baseColor sRGB, normal
/// UNORM, metallicRoughness UNORM, glTF G=roughness/B=metallic), plus the
/// scalar factors that modulate them in the shader. All bound through a
/// single descriptor set; the layout is owned by `PbrMaterialFactory`.
struct PbrMaterial {
  RHITexturePtr       base_tex,   normal_tex,   mr_tex;
  RHITextureViewPtr   base_view,  normal_view,  mr_view;
  RHITexturePtr       emissive_tex;
  RHITextureViewPtr   emissive_view;
  RHIDescriptorSetPtr desc_set;
  glm::vec4           base_color{1.0f, 1.0f, 1.0f, 1.0f};
  float               metallic  = 0.0f;
  float               roughness = 1.0f;
  glm::vec3           emissive{0.0f};
  // 是否携带真实 emissive 贴图(非 1×1 黑占位)。驱动"夜窗门控":有 emissive
  // 贴图的材质,其自发光视作美术画好的窗灯 —— 夜亮昼灭、且压掉程序化兜底。
  bool                has_emissive = false;
};

/// Owns the shared sampler + descriptor-set layout that every `PbrMaterial`
/// is built against. Upload helpers run a CPU box-filter mip chain
/// (anti-aliasing far-distance views) and wire up the descriptor set.
class PbrMaterialFactory {
 public:
  bool initialize(RHIDevice* device);

  RHIDescriptorSetLayoutPtr getDescriptorSetLayout() const { return desc_layout_; }
  RHISamplerPtr             getSampler()             const { return sampler_; }

  /// Build a single material from raw RGBA8 pixel buffers + factors. Any
  /// of `base_pixels` / `normal_pixels` / `mr_pixels` may be null — the
  /// missing slot gets a sensible 1×1 fallback (white / flat normal / white).
  std::optional<PbrMaterial> create(
      const uint8_t* base_pixels,   int base_w,   int base_h,
      const uint8_t* normal_pixels, int normal_w, int normal_h,
      const uint8_t* mr_pixels,     int mr_w,     int mr_h,
      const glm::vec4& base_color_factor,
      float metallic_factor, float roughness_factor,
      const uint8_t* emissive_pixels = nullptr, int em_w = 0, int em_h = 0,
      const glm::vec3& emissive_factor = glm::vec3(0.0f));

  /// 1×1 white-pixel material (no textures, identity factors). Used as the
  /// fallback when a primitive has no material assigned.
  std::optional<PbrMaterial> createFallback();

 private:
  bool uploadTexture(const uint8_t* pixels, int w, int h, Format format,
                      RHITexturePtr& out_tex, RHITextureViewPtr& out_view);
  bool buildDescriptorSet(PbrMaterial& m);

  RHIDevice*                device_ = nullptr;
  RHISamplerPtr             sampler_;
  RHIDescriptorSetLayoutPtr desc_layout_;
};

}  // namespace hongtu::renderer
