#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

// =============================================================================
// Pure billboard helpers — atlas UV computation. Lives here so unit tests can
// drive them without a Vulkan device.
// =============================================================================

/// One billboard slot in an atlas texture. The icon JSON file lists these by
/// name; runtime callers look them up via `BillboardAtlas::lookup(name)` and
/// stuff the resulting UV rectangle into `BillboardDraw::atlas_uv`.
struct AtlasSlot {
  glm::vec2 uv_min{0.0f, 0.0f};   ///< top-left UV in [0,1]
  glm::vec2 uv_max{1.0f, 1.0f};   ///< bottom-right UV in [0,1]
};

/// Build an `AtlasSlot` from pixel coordinates within an atlas of size
/// `atlas_px`. Used by the JSON loader and unit tests.
inline AtlasSlot atlasSlotFromPixels(uint32_t px_x, uint32_t px_y,
                                     uint32_t px_w, uint32_t px_h,
                                     uint32_t atlas_w, uint32_t atlas_h) {
  AtlasSlot s;
  s.uv_min.x = static_cast<float>(px_x) / static_cast<float>(atlas_w);
  s.uv_min.y = static_cast<float>(px_y) / static_cast<float>(atlas_h);
  s.uv_max.x = static_cast<float>(px_x + px_w) / static_cast<float>(atlas_w);
  s.uv_max.y = static_cast<float>(px_y + px_h) / static_cast<float>(atlas_h);
  return s;
}

/// Compute the fade alpha factor for a billboard at `dist` meters from the
/// camera. Below `near`: fully visible (1.0). Above `far`: fully faded (0.0).
/// In between: smoothstep. This is pure math — fragment shader uses the same
/// formula at runtime, but the test exercises it without a GPU.
inline float billboardFadeAlpha(float dist, float fade_near, float fade_far) {
  if (fade_far <= fade_near) return 1.0f;  // disabled
  if (dist <= fade_near) return 1.0f;
  if (dist >= fade_far) return 0.0f;
  float t = (dist - fade_near) / (fade_far - fade_near);
  // smoothstep: 3t² - 2t³
  return 1.0f - (t * t * (3.0f - 2.0f * t));
}

/// Atlas slot directory loaded from JSON. Maps slot names → UV rect (already
/// in 0..1 range, ready to drop into a `BillboardDraw::atlas_uv` vec4).
///
/// JSON format (`texture/icons/park.json`):
///   {
///     "atlas": "park.png",
///     "width": 256, "height": 256,
///     "slots": {
///       "warning":  {"x":0,  "y":0,  "w":64, "h":64},
///       "danger":   {"x":64, "y":0,  "w":64, "h":64},
///       ...
///     }
///   }
class BillboardAtlas {
 public:
  /// Parse a JSON manifest into an atlas. Returns false on any malformed
  /// field or missing slot count. After loading, use `lookup(name)`.
  bool loadFromJson(const std::string& json_text);

  /// Load convenience — read the JSON file from disk.
  bool loadFromFile(const std::string& json_path);

  /// Look up a slot by name. Returns the default whole-texture slot
  /// (uv 0..1) if `name` is unknown — so a missing icon shows the atlas
  /// background rather than crashing.
  AtlasSlot lookup(const std::string& name) const;

  /// Same as `lookup` but writes into a vec4 `(u_min, v_min, u_max, v_max)`.
  glm::vec4 lookupUVRect(const std::string& name) const;

  uint32_t getWidth()  const { return atlas_w_; }
  uint32_t getHeight() const { return atlas_h_; }
  const std::string& getAtlasPath() const { return atlas_path_; }
  size_t getSlotCount() const { return slots_.size(); }

 private:
  std::string atlas_path_;
  uint32_t    atlas_w_ = 0;
  uint32_t    atlas_h_ = 0;
  std::unordered_map<std::string, AtlasSlot> slots_;
};

// =============================================================================
// BillboardRenderer
// =============================================================================
//
// Coordinate-agnostic: `world_pos` is whatever space the caller's
// `view * proj` matrices use (typically RTC float32). Each instance carries
// its own atlas UV rectangle so the same draw can mix dozens of icon types
// reading from a single atlas texture.

class BillboardRenderer {
 public:
  /// One billboard to draw. All fields are per-instance; the renderer
  /// gathers them into a tightly packed instance VBO and issues one draw.
  struct BillboardDraw {
    glm::vec3 world_pos;          ///< RTC-space center
    glm::vec2 size_m;             ///< world-space width / height in meters
    glm::vec4 atlas_uv;           ///< (u_min, v_min, u_max, v_max)
    glm::vec4 color    {1, 1, 1, 1};  ///< per-instance tint
    /// POI 特效:0=静态 1=脉冲(缩放+呼吸) 2=跳动 3=扩散波涟漪环(忽略
    /// atlas,程序化画扩散环)。`phase` = 频率(次/秒)。time 来自 Params。
    float     effect   = 0.0f;
    float     phase    = 1.0f;
    /// 自定义贴图槽(ensureCustomTexture 返回值)。-1 = 用内置 atlas + atlas_uv;
    /// >=0 = 改绑该自定义贴图整张(atlas_uv 取 (0,0,1,1))。同 polyline 自定义贴图。
    int       tex_slot = -1;
  };

  /// Per-frame parameters needed to billboard-orient every quad.
  struct Params {
    glm::mat4 view_proj;          ///< RTC-relative
    glm::vec3 camera_right;       ///< column 0 of inv(view), unit
    glm::vec3 camera_up;          ///< column 1 of inv(view), unit
    glm::vec3 camera_pos_rtc;     ///< RTC camera position (for fade distance)
    float     fade_near_m = 0.0f; ///< 0 = no fade (always full alpha)
    float     fade_far_m  = 0.0f; ///< 0 = no fade
    float     time_s = 0.0f;      ///< POI 特效动画时基(脉冲/跳动/扩散)
  };

  bool initialize(RHIDevice* device,
                  RHIRenderPassPtr scene_render_pass,
                  RHITextureViewPtr atlas_view,
                  RHISamplerPtr     atlas_sampler);

  /// Swap the atlas texture (call when JSON-driven atlas changes).
  void setAtlas(RHITextureViewPtr atlas_view, RHISamplerPtr atlas_sampler);

  /// Register a custom RGBA icon texture (keyed by abs path). Returns a slot
  /// for BillboardDraw::tex_slot, or the existing slot if already loaded; -1 on
  /// failure. Mirrors PolylineRenderer::ensureCustomPattern.
  int ensureCustomTexture(const std::string& key, const uint8_t* rgba,
                          int w, int h);
  int getCustomTextureSlot(const std::string& key) const;

  /// Record draws into `cmd`. Caller must have already begun the scene
  /// render pass. No-op if `instances` is empty.
  void record(const RHICommandBufferPtr& cmd, const Params& p,
              const std::vector<BillboardDraw>& instances);

 private:
  // Push constants — view_proj (64) + camera_right_pad (16) + camera_up_pad (16)
  //                  + fade_pad (16) = 112 bytes.
  // camera_pos isn't needed in the shader: we already know per-vertex world_pos
  // and we pass the fade range; fade distance is computed in the fragment
  // shader from `pos_world - camera_pos` reconstructed via inv-VP, but we
  // shortcut it by passing the camera position too. Easier just to pre-compute
  // fade alpha on the CPU and put it in the per-instance color.a — that's what
  // we do, so the shader stays simple.
  struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 camera_right;       ///< xyz + padding
    glm::vec4 camera_up;          ///< xyz + padding
    glm::vec4 fx;                 ///< x=time_s
  };
  static_assert(sizeof(PushConstants) == 112,
                "BillboardRenderer::PushConstants layout mismatch");

  // Per-instance vertex data — matches the GPU layout. Position + size +
  // atlas UV + premultiplied color (alpha already includes fade) + 特效.
  struct InstanceData {
    glm::vec3 world_pos;  // 0
    float     size_x;     // 12
    float     size_y;     // 16
    float     effect;     // 20  (0 静态/1 脉冲/2 跳动/3 涟漪)
    glm::vec4 atlas_uv;   // 24
    glm::vec4 color;      // 40
    float     phase;      // 56  (特效频率)
    float     _pad;       // 60
  };
  static_assert(sizeof(InstanceData) == 64,
                "BillboardRenderer::InstanceData layout mismatch");

  bool createDescriptorSet();
  bool createPipeline(RHIRenderPassPtr render_pass);
  void uploadInstances(const std::vector<InstanceData>& instances);

  RHIDevice*               device_ = nullptr;

  // Atlas texture binding.
  RHITextureViewPtr        atlas_view_;
  RHISamplerPtr            atlas_sampler_;
  RHIDescriptorSetLayoutPtr atlas_set_layout_;
  RHIDescriptorSetPtr      atlas_set_;

  // 自定义图标贴图(每张一个描述符集,复用 atlas 的 set layout + sampler)。
  std::vector<RHITexturePtr>       custom_tex_;
  std::vector<RHITextureViewPtr>   custom_view_;
  std::vector<RHIDescriptorSetPtr> custom_set_;
  std::unordered_map<std::string, int> custom_index_;

  RHIShaderPtr             vs_;
  RHIShaderPtr             fs_;
  RHIPipelineLayoutPtr     pipeline_layout_;
  RHIPipelinePtr           pipeline_;

  // Per-frame instance VBO. Grows on demand.
  RHIBufferPtr             inst_vbo_;
  uint32_t                 inst_vbo_capacity_ = 0;
};

}  // namespace hongtu::renderer
