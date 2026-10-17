#include <hongtu/renderer/vegetation/vegetation_layer.hpp>
#include <hongtu/renderer/pbr/instanced_pbr_pipeline.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <map>

namespace hongtu::renderer {

// ============================================================================
// Pure helpers
// ============================================================================

glm::mat4 VegetationLayer::composeInstanceMatrix(const VegetationInstance& inst) {
  glm::mat4 m(1.0f);
  m = glm::translate(m, inst.pos);
  m = glm::rotate(m, inst.yaw, glm::vec3(0, 0, 1));
  // Y-up glTF asset → Z-up local world.
  m = glm::rotate(m, glm::half_pi<float>(), glm::vec3(1, 0, 0));
  m = glm::scale(m, glm::vec3(inst.scale));
  return m;
}

glm::vec4 VegetationLayer::tintToColor(float tint) {
  // Subtle, deterministic variation: brightness 1.0 → 0.82 and a slight
  // yellow-green hue shift as tint grows. Keeps a planted forest from
  // reading as copy-paste without looking painted.
  float t = glm::clamp(tint, 0.0f, 1.0f);
  float brightness = 1.0f - 0.18f * t;
  return glm::vec4(brightness * (1.0f + 0.06f * t),   // r — slightly warmer
                   brightness,                         // g
                   brightness * (1.0f - 0.10f * t),   // b — less blue
                   1.0f);
}

glm::ivec2 VegetationLayer::cellKey(const glm::vec3& pos, float cell_size) {
  return {static_cast<int>(std::floor(pos.x / cell_size)),
          static_cast<int>(std::floor(pos.y / cell_size))};
}

// ============================================================================
// Lifecycle / archetypes
// ============================================================================

bool VegetationLayer::initialize(RHIDevice* device,
                                 PbrMaterialFactory* material_factory,
                                 RHIDescriptorSetLayoutPtr shadow_set_layout,
                                 RHIRenderPassPtr           render_pass,
                                 RHIDescriptorSetLayoutPtr ibl_set_layout) {
  device_           = device;
  material_factory_ = material_factory;
  if (!pipeline_.initialize(device, material_factory->getDescriptorSetLayout(),
                            shadow_set_layout, render_pass, ibl_set_layout)) {
    spdlog::error("VegetationLayer: pipeline init failed");
    return false;
  }
  return true;
}

int VegetationLayer::addArchetype(const hongtu::asset::ModelData& model) {
  if (!device_ || !material_factory_) return -1;

  Archetype a;

  // ---- materials ----------------------------------------------------------
  auto fb = material_factory_->createFallback();
  if (!fb) return -1;
  a.fallback = std::move(*fb);

  auto pickTex = [&](int idx) -> const hongtu::asset::TextureData* {
    if (idx < 0 || idx >= static_cast<int>(model.textures.size())) return nullptr;
    const auto& t = model.textures[idx];
    if (t.pixels.empty() || t.width <= 0 || t.height <= 0) return nullptr;
    return &t;
  };
  for (const auto& mat : model.materials) {
    const auto* base   = pickTex(mat.base_color_texture);
    const auto* normal = pickTex(mat.normal_texture);
    const auto* mr     = pickTex(mat.metallic_roughness_texture);
    const auto* emis   = pickTex(mat.emissive_texture);   // 路灯灯泡等自发光 → 夜间发亮
    spdlog::info("VegetationLayer: mat '{}' base={}({}x{}) nor={} mr={} emis={}",
                 mat.name, mat.base_color_texture, base ? base->width : 0,
                 base ? base->height : 0, mat.normal_texture,
                 mat.metallic_roughness_texture, mat.emissive_texture);
    auto pm = material_factory_->create(
        base   ? base->pixels.data()   : nullptr, base   ? base->width   : 0,
        base   ? base->height   : 0,
        normal ? normal->pixels.data() : nullptr, normal ? normal->width : 0,
        normal ? normal->height : 0,
        mr     ? mr->pixels.data()     : nullptr, mr     ? mr->width     : 0,
        mr     ? mr->height     : 0,
        mat.base_color_factor, mat.metallic_factor, mat.roughness_factor,
        emis   ? emis->pixels.data()   : nullptr, emis   ? emis->width   : 0,
        emis   ? emis->height   : 0,   mat.emissive_factor);
    if (!pm) {
      spdlog::warn("VegetationLayer: material '{}' upload failed; fallback", mat.name);
      a.materials.push_back(a.fallback);
    } else {
      a.materials.push_back(std::move(*pm));
    }
  }

  // ---- merged VBO/IBO with per-primitive index ranges ---------------------
  glm::vec3 bmin, bmax;
  if (!uploadGeometry(model, a.vbo, a.ibo, a.prims, &bmin, &bmax)) {
    spdlog::warn("VegetationLayer: archetype has no geometry");
    return -1;
  }

  // Asset is Y-up: height = Y extent, footprint radius from XZ.
  a.height_m = bmax.y - bmin.y;
  a.radius_m = 0.5f * glm::max(bmax.x - bmin.x, bmax.z - bmin.z);

  archetypes_.push_back(std::move(a));
  spdlog::info("VegetationLayer: archetype {} — {} mats, h={:.1f}m r={:.1f}m",
               archetypes_.size() - 1, model.materials.size(),
               archetypes_.back().height_m, archetypes_.back().radius_m);
  return static_cast<int>(archetypes_.size()) - 1;
}

bool VegetationLayer::uploadGeometry(const hongtu::asset::ModelData& model,
                                     RHIBufferPtr& vbo, RHIBufferPtr& ibo,
                                     std::vector<Primitive>& prims,
                                     glm::vec3* bmin_out, glm::vec3* bmax_out) {
  std::vector<MeshVertex> verts;
  std::vector<u32>        idx;
  glm::vec3 bmin(std::numeric_limits<float>::max());
  glm::vec3 bmax(-std::numeric_limits<float>::max());
  for (const auto& m : model.meshes) {
    const bool has_n = m.normals.size() == m.positions.size();
    const bool has_t = m.uvs.size() == m.positions.size();
    const u32  base  = static_cast<u32>(verts.size());
    for (size_t i = 0; i < m.positions.size(); ++i) {
      MeshVertex v;
      v.position = m.positions[i];
      v.normal   = has_n ? m.normals[i] : glm::vec3(0, 1, 0);
      v.uv       = has_t ? m.uvs[i] : glm::vec2(0);
      bmin = glm::min(bmin, v.position);
      bmax = glm::max(bmax, v.position);
      verts.push_back(v);
    }
    Primitive p;
    p.first_index = static_cast<u32>(idx.size());
    p.index_count = static_cast<u32>(m.indices.size());
    p.material_id = m.material_id;
    prims.push_back(p);
    for (u32 i : m.indices) idx.push_back(base + i);
  }
  if (verts.empty() || idx.empty()) return false;

  BufferDesc vd;
  vd.size             = verts.size() * sizeof(MeshVertex);
  vd.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  vd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  vbo                 = device_->createBuffer(vd);
  BufferDesc id;
  id.size             = idx.size() * sizeof(u32);
  id.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
  id.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  ibo                 = device_->createBuffer(id);
  if (!vbo || !ibo) return false;
  std::memcpy(vbo->map(), verts.data(), vd.size);
  vbo->unmap();
  std::memcpy(ibo->map(), idx.data(), id.size);
  ibo->unmap();
  if (bmin_out) *bmin_out = bmin;
  if (bmax_out) *bmax_out = bmax;
  return true;
}

bool VegetationLayer::setArchetypeLod(int archetype_id,
                                      const hongtu::asset::ModelData& lod_model,
                                      float lod_distance) {
  if (archetype_id < 0 || archetype_id >= static_cast<int>(archetypes_.size()))
    return false;
  Archetype& a = archetypes_[archetype_id];
  // The LOD shares the base mesh's materials — it must come from the same
  // source asset (same material order); only geometry is uploaded.
  if (!uploadGeometry(lod_model, a.lod_vbo, a.lod_ibo, a.lod_prims)) {
    spdlog::warn("VegetationLayer: LOD upload failed for archetype {}", archetype_id);
    return false;
  }
  a.lod_distance = lod_distance;
  spdlog::info("VegetationLayer: archetype {} LOD attached (switch at {:.0f} m)",
               archetype_id, lod_distance);
  return true;
}

void VegetationLayer::setMaxDrawDistance(int archetype_id, float distance) {
  if (archetype_id < 0 || archetype_id >= static_cast<int>(archetypes_.size()))
    return;
  archetypes_[archetype_id].max_draw_distance = distance;
}

void VegetationLayer::setCastsShadow(int archetype_id, bool casts) {
  if (archetype_id < 0 || archetype_id >= static_cast<int>(archetypes_.size()))
    return;
  archetypes_[archetype_id].casts_shadow = casts;
}

void VegetationLayer::setSwayAmplitude(int archetype_id, float metres) {
  if (archetype_id < 0 || archetype_id >= static_cast<int>(archetypes_.size()))
    return;
  archetypes_[archetype_id].sway_amp_m = metres;
}

void VegetationLayer::setInstances(int archetype_id,
                                   const std::vector<VegetationInstance>& instances) {
  if (archetype_id < 0 || archetype_id >= static_cast<int>(archetypes_.size())) return;
  Archetype& a = archetypes_[archetype_id];

  // Bucket by cell, then bake InstanceVertex contiguously per cell so the
  // per-frame visible rebuild is one memcpy per visible cell.
  std::map<std::pair<int, int>, std::vector<const VegetationInstance*>> buckets;
  for (const auto& inst : instances) {
    auto k = cellKey(inst.pos, kCellSize);
    buckets[{k.x, k.y}].push_back(&inst);
  }

  a.baked.clear();
  a.baked.reserve(instances.size());
  a.cells.clear();
  for (auto& [key, list] : buckets) {
    Cell c;
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (const auto* inst : list) {
      VegetationPipeline::InstanceVertex iv;
      iv.model      = composeInstanceMatrix(*inst);
      iv.base_color = inst->base_color * tintToColor(inst->tint);
      iv.mr_params  = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);  // non-metal, rough from tex
      c.indices.push_back(static_cast<u32>(a.baked.size()));
      a.baked.push_back(iv);
      const float pad = a.radius_m * inst->scale;
      const float top = a.height_m * inst->scale;
      mn = glm::min(mn, inst->pos - glm::vec3(pad, pad, 0.0f));
      mx = glm::max(mx, inst->pos + glm::vec3(pad, pad, top));
    }
    c.bounds = {mn, mx};
    a.cells.push_back(std::move(c));
  }
  a.dirty = true;
}

// ============================================================================
// Per-frame
// ============================================================================

void VegetationLayer::rebuildVisible(Archetype& a, const Frustum& fr,
                                     const glm::vec3& eye) {
  // Gather visible instances cell-granularly, near-LOD first so the instance
  // VBO is [near..., far...] and record() draws each range with firstInstance.
  static thread_local std::vector<VegetationPipeline::InstanceVertex> near_v;
  static thread_local std::vector<VegetationPipeline::InstanceVertex> far_v;
  near_v.clear();
  far_v.clear();
  const bool has_lod = a.lod_vbo != nullptr && a.lod_distance > 0.0f;
  for (const auto& cell : a.cells) {
    if (!InstanceBuffer::isAABBVisible(cell.bounds, fr)) continue;
    const glm::vec3 center = cell.bounds.center();
    const float dist = glm::length(glm::vec2(center) - glm::vec2(eye));
    if (a.max_draw_distance > 0.0f && dist > a.max_draw_distance) continue;
    auto& bucket = (has_lod && dist > a.lod_distance) ? far_v : near_v;
    for (u32 i : cell.indices) bucket.push_back(a.baked[i]);
  }
  a.near_count    = static_cast<u32>(near_v.size());
  a.visible_count = a.near_count + static_cast<u32>(far_v.size());
  static thread_local std::vector<VegetationPipeline::InstanceVertex> visible;
  visible.clear();
  visible.insert(visible.end(), near_v.begin(), near_v.end());
  visible.insert(visible.end(), far_v.begin(), far_v.end());
  if (visible.empty()) return;

  if (!a.instance_vbo || a.instance_capacity < visible.size()) {
    a.instance_capacity = visible.size() + visible.size() / 2;  // 1.5× headroom
    BufferDesc bd;
    bd.size  = a.instance_capacity * sizeof(VegetationPipeline::InstanceVertex);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties =
        MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    a.instance_vbo = device_->createBuffer(bd);
    if (!a.instance_vbo) {
      a.visible_count = 0;
      return;
    }
  }
  std::memcpy(a.instance_vbo->map(), visible.data(),
              visible.size() * sizeof(VegetationPipeline::InstanceVertex));
  a.instance_vbo->unmap();
}

void VegetationLayer::update(const glm::mat4& view_proj, const glm::vec3& eye) {
  const Frustum fr = InstanceBuffer::extractFrustum(view_proj);
  stats_ = {};
  for (auto& a : archetypes_) {
    stats_.total_instances += static_cast<u32>(a.baked.size());
    // Culling changes with the camera every frame; the rebuild is a memcpy
    // of visible cells (thousands of 96-byte structs — cheap).
    rebuildVisible(a, fr, eye);
    a.dirty = false;
    stats_.visible_instances += a.visible_count;
  }
}

bool VegetationLayer::initializeShadow(RHIRenderPassPtr shadow_render_pass) {
  if (!device_ || !shadow_render_pass) return false;

  HongTu::Asset::ShaderCompiler compiler;
  // Depth-only instanced vertex shader: instance model from binding=1
  // (locations 3..6, same layout as the lit pipeline so the instance VBO is
  // shared verbatim), no fragment output. Wind sway is skipped — a swaying
  // shadow needs the wind params + time plumbed through; the static
  // silhouette reads fine and keeps the push at one matrix.
  const char* vsrc =
      "layout(location = 0) in vec3 inPosition;\n"
      "layout(location = 1) in vec3 inNormal;\n"
      "layout(location = 2) in vec2 inUv;\n"
      "layout(location = 3) in vec4 inModelCol0;\n"
      "layout(location = 4) in vec4 inModelCol1;\n"
      "layout(location = 5) in vec4 inModelCol2;\n"
      "layout(location = 6) in vec4 inModelCol3;\n"
      "layout(location = 7) in vec4 inBaseColor;\n"
      "layout(location = 8) in vec4 inMRParams;\n"
      "layout(push_constant) uniform PC { mat4 light_vp; } pc;\n"
      "void main() {\n"
      "  mat4 model = mat4(inModelCol0, inModelCol1, inModelCol2, inModelCol3);\n"
      "  gl_Position = pc.light_vp * model * vec4(inPosition, 1.0);\n"
      "}\n";
  const char* fsrc = "void main() {}\n";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("VegetationLayer: shadow shader compile failed: {}",
                  vr.success ? fr.errorMessage : vr.errorMessage);
    return false;
  }
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  shadow_vs_ = device_->createShader(vd);
  shadow_fs_ = device_->createShader(fd);
  if (!shadow_vs_ || !shadow_fs_) return false;

  PipelineLayoutDesc pl;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
  pcr.offset = 0;
  pcr.size   = sizeof(glm::mat4);
  pl.pushConstantRanges.push_back(pcr);
  shadow_layout_ = device_->createPipelineLayout(pl);
  if (!shadow_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders = {shadow_vs_, shadow_fs_};
  pd.vertexInput.bindings   = {MeshVertex::binding(),
                               InstancedPbrPipeline::getInstanceVertexBinding()};
  pd.vertexInput.attributes = MeshVertex::attributes();
  for (const auto& a : InstancedPbrPipeline::getInstanceVertexAttributes()) {
    pd.vertexInput.attributes.push_back(a);
  }
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = shadow_render_pass;
  pd.pipelineLayout                = shadow_layout_;
  shadow_pipeline_ = device_->createGraphicsPipeline(pd);
  if (!shadow_pipeline_) {
    spdlog::error("VegetationLayer: shadow pipeline creation failed");
    return false;
  }
  return true;
}

void VegetationLayer::recordShadow(const RHICommandBufferPtr& cmd,
                                   const glm::mat4& light_vp) {
  if (!shadow_pipeline_) return;
  bool bound = false;
  for (auto& a : archetypes_) {
    if (a.visible_count == 0 || !a.instance_vbo || !a.casts_shadow) continue;
    if (!bound) {
      cmd->bindPipeline(shadow_pipeline_);
      cmd->pushConstants(shadow_layout_,
                         static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
                         0, sizeof(glm::mat4), &light_vp);
      bound = true;
    }
    cmd->bindVertexBuffer(1, a.instance_vbo);
    auto drawRange = [&](const RHIBufferPtr& vbo, const RHIBufferPtr& ibo,
                         const std::vector<Primitive>& prims, u32 first, u32 count) {
      if (count == 0 || !vbo || !ibo) return;
      cmd->bindVertexBuffer(0, vbo);
      cmd->bindIndexBuffer(ibo);
      for (const auto& p : prims) {
        cmd->drawIndexed(p.index_count, count, p.first_index, 0, first);
      }
    };
    drawRange(a.vbo, a.ibo, a.prims, 0u, a.near_count);
    drawRange(a.lod_vbo, a.lod_ibo, a.lod_prims, a.near_count,
              a.visible_count - a.near_count);
  }
}

void VegetationLayer::record(const RHICommandBufferPtr& cmd,
                             VegetationPipeline::PushConstants pc,
                             const RHIDescriptorSetPtr& shadow_set,
                             const RHIDescriptorSetPtr& ibl_set) {
  if (!pipeline_.getPipeline()) return;
  bool bound = false;
  const ShaderStageFlags stages =
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);

  for (auto& a : archetypes_) {
    if (a.visible_count == 0 || !a.instance_vbo) continue;
    if (!bound) {
      cmd->bindPipeline(pipeline_.getPipeline());
      if (shadow_set) {
        cmd->bindDescriptorSet(1u, shadow_set, pipeline_.getPipelineLayout(),
                               PipelineBindPoint::Graphics);
      }
      if (pipeline_.hasIbl() && ibl_set) {
        cmd->bindDescriptorSet(2u, ibl_set, pipeline_.getPipelineLayout(),
                               PipelineBindPoint::Graphics);
      }
      bound = true;
    }
    pc.archetype = glm::vec4(a.height_m, a.sway_amp_m, 0.0f, 0.0f);
    cmd->pushConstants(pipeline_.getPipelineLayout(), stages, 0,
                       sizeof(pc), &pc);
    cmd->bindVertexBuffer(1, a.instance_vbo);

    // The instance VBO is [near..., far...]; each range draws its mesh via
    // firstInstance — no per-instance rebinds, two index buffers total.
    auto drawRange = [&](const RHIBufferPtr& vbo, const RHIBufferPtr& ibo,
                         const std::vector<Primitive>& prims, u32 first, u32 count) {
      if (count == 0 || !vbo || !ibo) return;
      cmd->bindVertexBuffer(0, vbo);
      cmd->bindIndexBuffer(ibo);
      for (const auto& p : prims) {
        const PbrMaterial& m =
            (p.material_id >= 0 && p.material_id < static_cast<int>(a.materials.size()))
                ? a.materials[p.material_id]
                : a.fallback;
        if (m.desc_set) {
          cmd->bindDescriptorSet(0u, m.desc_set, pipeline_.getPipelineLayout(),
                                 PipelineBindPoint::Graphics);
        }
        cmd->drawIndexed(p.index_count, count, p.first_index, 0, first);
        ++stats_.draw_calls;
      }
    };
    drawRange(a.vbo, a.ibo, a.prims, 0u, a.near_count);
    drawRange(a.lod_vbo, a.lod_ibo, a.lod_prims, a.near_count,
              a.visible_count - a.near_count);
  }
}

}  // namespace hongtu::renderer
