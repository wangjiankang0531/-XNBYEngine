#pragma once

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/renderer/instancing/instance_buffer.hpp>  // Frustum / AABB
#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/renderer/vegetation/vegetation_pipeline.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// One planted vegetation instance. Z-up world; the layer composes the
/// instance matrix as T(pos) · Rz(yaw) · Rx(+90°) · S(scale) — the Rx
/// converts the Y-up glTF asset into the Z-up local world.
struct VegetationInstance {
  glm::vec3 pos{0.0f};
  float     yaw   = 0.0f;  ///< radians around world Z
  float     scale = 1.0f;
  /// Albedo tint variation [0..1]: 0 = source color, 1 = strongest shift.
  /// The layer maps it to a subtle hue/brightness spread so a thousand
  /// copies of one tree don't read as clones.
  float     tint  = 0.0f;
  /// Explicit albedo multiplier (RGBA). Default white = no change (foliage uses
  /// `tint` only). Set to a solid colour for schematic instances (e.g. drill
  /// 人/车 占位模型),the shader uses `base_color * tintToColor(tint)`.
  glm::vec4 base_color{1.0f};
};

/// Instanced vegetation rendering: a few mesh "archetypes" (tree / shrub /
/// grass-clump glbs), each drawn as ONE drawIndexed(instances=N) per
/// primitive. Grid-bucketed CPU frustum culling keeps thousand-instance
/// scenes cheap; the wind/foliage shading lives in VegetationPipeline.
///
/// Pure-math helpers (instance matrix compose, cell bucketing) are static
/// and unit-tested without a GPU.
class VegetationLayer {
 public:
  bool initialize(RHIDevice* device,
                  PbrMaterialFactory* material_factory,
                  RHIDescriptorSetLayoutPtr shadow_set_layout,
                  RHIRenderPassPtr           render_pass,
                  RHIDescriptorSetLayoutPtr ibl_set_layout = nullptr);

  /// Upload a glb model as an archetype (meshes + PBR materials). Returns
  /// the archetype id, or -1 on failure.
  int addArchetype(const hongtu::asset::ModelData& model);

  /// Attach a far-LOD mesh (aggressively decimated copy of the SAME source
  /// asset — material indices must match). Cells whose center is beyond
  /// `lod_distance` draw the LOD geometry instead of the full mesh.
  bool setArchetypeLod(int archetype_id, const hongtu::asset::ModelData& lod_model,
                       float lod_distance);

  /// Cells entirely beyond this camera distance are skipped (grass / shrubs
  /// shouldn't draw to the horizon). Default: unlimited.
  void setMaxDrawDistance(int archetype_id, float distance);

  /// Ground-cover (grass clumps) shouldn't cast: thousands of tiny shadows
  /// just darken the lawn into a visible planted-region rectangle.
  void setCastsShadow(int archetype_id, bool casts);

  /// Wind sway amplitude in metres at the canopy top (default 0.35). Set 0
  /// for rigid props (lamps, benches) rendered through the layer.
  void setSwayAmplitude(int archetype_id, float metres);

  /// Replace the instance set of an archetype. Instances are bucketed into
  /// grid cells once here; per-frame culling walks cells, not instances.
  void setInstances(int archetype_id, const std::vector<VegetationInstance>& instances);

  /// Per-frame: frustum-cull cells (+ distance LOD split) and rebuild each
  /// archetype's visible instance VBO. `eye` drives LOD/max-distance.
  void update(const glm::mat4& view_proj, const glm::vec3& eye);

  /// Record draws. Caller supplies the shared shadow descriptor set and the
  /// frame's lighting push constants; the layer fills the per-archetype
  /// `archetype` vec4 (asset height / sway amplitude) itself.
  void record(const RHICommandBufferPtr& cmd,
              VegetationPipeline::PushConstants pc,
              const RHIDescriptorSetPtr& shadow_set,
              const RHIDescriptorSetPtr& ibl_set = nullptr);

  /// Optional shadow casting: build a depth-only instanced pipeline against
  /// the shadow pass, then record the camera-visible instances into it via
  /// ShadowPass::setExternalShadowDraw. (Uses the camera-culled instance
  /// VBOs — off-screen casters can pop; light-frustum culling is a later
  /// refinement.)
  bool initializeShadow(RHIRenderPassPtr shadow_render_pass);
  void recordShadow(const RHICommandBufferPtr& cmd, const glm::mat4& light_vp);

  struct Stats {
    u32 total_instances   = 0;
    u32 visible_instances = 0;
    u32 draw_calls        = 0;
  };
  const Stats& stats() const { return stats_; }

  // ---- pure helpers (unit-tested) ----------------------------------------

  /// T(pos) · Rz(yaw) · Rx(+90°) · S — see VegetationInstance.
  static glm::mat4 composeInstanceMatrix(const VegetationInstance& inst);

  /// Map tint [0..1] to an albedo multiplier: subtle green-hue/brightness
  /// spread, deterministic.
  static glm::vec4 tintToColor(float tint);

  /// Bucket key for a position on a `cell_size` grid.
  static glm::ivec2 cellKey(const glm::vec3& pos, float cell_size);

 private:
  struct Primitive {
    u32 first_index = 0;
    u32 index_count = 0;
    int material_id = -1;  ///< index into Archetype::materials, -1 = fallback
  };

  struct Cell {
    AABB             bounds;   ///< instance positions ± archetype radius·scale
    std::vector<u32> indices;  ///< into Archetype::baked
  };

  struct Archetype {
    RHIBufferPtr             vbo;        ///< all primitives share one VBO/IBO
    RHIBufferPtr             ibo;
    std::vector<Primitive>   prims;
    std::vector<PbrMaterial> materials;  ///< by ModelData material id
    PbrMaterial              fallback;
    float height_m = 10.0f;   ///< asset-local Y extent (wind weight)
    float radius_m = 5.0f;    ///< XZ footprint radius (cull padding)

    std::vector<VegetationPipeline::InstanceVertex> baked;  ///< all instances
    std::vector<Cell> cells;
    RHIBufferPtr instance_vbo;
    size_t       instance_capacity = 0;  ///< in instances
    u32          visible_count     = 0;  ///< near + far
    bool         dirty             = true;

    // Far-LOD mesh (optional). Shares `materials` with the base mesh.
    RHIBufferPtr           lod_vbo, lod_ibo;
    std::vector<Primitive> lod_prims;
    float lod_distance      = 0.0f;   ///< 0 = no LOD
    float max_draw_distance = 0.0f;   ///< 0 = unlimited
    bool  casts_shadow      = true;
    float sway_amp_m        = 0.35f;
    u32   near_count        = 0;      ///< instances [0, near) use the base mesh
  };

  void rebuildVisible(Archetype& a, const Frustum& fr, const glm::vec3& eye);
  bool uploadGeometry(const hongtu::asset::ModelData& model, RHIBufferPtr& vbo,
                      RHIBufferPtr& ibo, std::vector<Primitive>& prims,
                      glm::vec3* bmin_out = nullptr, glm::vec3* bmax_out = nullptr);

  RHIDevice*          device_           = nullptr;
  PbrMaterialFactory* material_factory_ = nullptr;
  VegetationPipeline  pipeline_;
  // Depth-only instanced pipeline for shadow casting (optional).
  RHIShaderPtr         shadow_vs_;
  RHIShaderPtr         shadow_fs_;
  RHIPipelineLayoutPtr shadow_layout_;
  RHIPipelinePtr       shadow_pipeline_;
  std::vector<Archetype> archetypes_;
  Stats stats_;

  static constexpr float kCellSize = 24.0f;  // metres
};

}  // namespace hongtu::renderer
