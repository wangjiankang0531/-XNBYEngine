#include <hongtu/renderer/effects/billboard_renderer.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

// =============================================================================
// BillboardAtlas — JSON manifest loader
// =============================================================================

bool BillboardAtlas::loadFromJson(const std::string& json_text) {
  try {
    auto j = nlohmann::json::parse(json_text);
    atlas_path_ = j.value("atlas", std::string{});
    atlas_w_    = j.value("width",  0u);
    atlas_h_    = j.value("height", 0u);
    if (atlas_w_ == 0 || atlas_h_ == 0) {
      spdlog::error("BillboardAtlas: missing width/height in manifest");
      return false;
    }
    slots_.clear();
    if (j.contains("slots") && j["slots"].is_object()) {
      for (auto it = j["slots"].begin(); it != j["slots"].end(); ++it) {
        const auto& v = it.value();
        if (!v.contains("x") || !v.contains("y") ||
            !v.contains("w") || !v.contains("h")) continue;
        slots_[it.key()] = atlasSlotFromPixels(
            v["x"].get<uint32_t>(),  v["y"].get<uint32_t>(),
            v["w"].get<uint32_t>(),  v["h"].get<uint32_t>(),
            atlas_w_, atlas_h_);
      }
    }
    return true;
  } catch (const std::exception& e) {
    spdlog::error("BillboardAtlas: JSON parse failed: {}", e.what());
    return false;
  }
}

bool BillboardAtlas::loadFromFile(const std::string& json_path) {
  std::ifstream f(json_path);
  if (!f) {
    spdlog::error("BillboardAtlas: cannot open {}", json_path);
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return loadFromJson(ss.str());
}

AtlasSlot BillboardAtlas::lookup(const std::string& name) const {
  auto it = slots_.find(name);
  if (it != slots_.end()) return it->second;
  // Default whole-texture slot so unknown lookups don't crash.
  AtlasSlot s;
  s.uv_min = {0.0f, 0.0f};
  s.uv_max = {1.0f, 1.0f};
  return s;
}

glm::vec4 BillboardAtlas::lookupUVRect(const std::string& name) const {
  AtlasSlot s = lookup(name);
  return {s.uv_min.x, s.uv_min.y, s.uv_max.x, s.uv_max.y};
}

// =============================================================================
// BillboardRenderer
// =============================================================================
//
// Instance-rendered screen-aligned quads. Each instance carries world_pos +
// size + atlas_uv + color (alpha already premultiplied by CPU-side fade).
// The vertex shader generates the 4 quad corners from `gl_VertexIndex` and
// pushes them along the camera's right/up basis vectors.

bool BillboardRenderer::initialize(RHIDevice* device,
                                   RHIRenderPassPtr scene_render_pass,
                                   RHITextureViewPtr atlas_view,
                                   RHISamplerPtr     atlas_sampler) {
  device_        = device;
  atlas_view_    = atlas_view;
  atlas_sampler_ = atlas_sampler;
  if (!createDescriptorSet())             return false;
  if (!createPipeline(scene_render_pass)) return false;
  return true;
}

bool BillboardRenderer::createDescriptorSet() {
  DescriptorSetLayoutDesc dl;
  DescriptorSetLayoutBinding lb;
  lb.binding         = 0u;
  lb.descriptorCount = 1u;
  lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  lb.descriptorType  = DescriptorType::CombinedImageSampler;
  dl.bindings.push_back(lb);
  atlas_set_layout_ = device_->createDescriptorSetLayout(dl);
  if (!atlas_set_layout_) return false;

  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding     = 0u;
  db.type        = DescriptorType::CombinedImageSampler;
  db.textureView = atlas_view_;
  db.sampler     = atlas_sampler_;
  dsd.bindings.push_back(db);
  atlas_set_ = device_->createDescriptorSet(atlas_set_layout_, dsd);
  return atlas_set_ != nullptr;
}

void BillboardRenderer::setAtlas(RHITextureViewPtr atlas_view,
                                 RHISamplerPtr     atlas_sampler) {
  atlas_view_    = atlas_view;
  atlas_sampler_ = atlas_sampler;
  // Recreate the descriptor set so the next draw picks up the new binding.
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding     = 0u;
  db.type        = DescriptorType::CombinedImageSampler;
  db.textureView = atlas_view_;
  db.sampler     = atlas_sampler_;
  dsd.bindings.push_back(db);
  atlas_set_ = device_->createDescriptorSet(atlas_set_layout_, dsd);
}

int BillboardRenderer::getCustomTextureSlot(const std::string& key) const {
  auto it = custom_index_.find(key);
  return it == custom_index_.end() ? -1 : it->second;
}

int BillboardRenderer::ensureCustomTexture(const std::string& key,
                                           const uint8_t* rgba, int w, int h) {
  auto it = custom_index_.find(key);
  if (it != custom_index_.end()) return it->second;
  if (!device_ || !rgba || w <= 0 || h <= 0 || !atlas_set_layout_ || !atlas_sampler_)
    return -1;
  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
  td.mipLevels   = 1u; td.arrayLayers = 1u;
  td.format      = Format::R8G8B8A8_UNORM;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  auto tex = device_->createTexture(td);
  if (!tex) return -1;
  RHIDevice::MipData m0;
  m0.width = static_cast<uint32_t>(w); m0.height = static_cast<uint32_t>(h);
  m0.data = const_cast<uint8_t*>(rgba);
  m0.dataSize = static_cast<size_t>(w) * h * 4u;
  device_->uploadTextureData(tex, {m0}, 0u);
  auto view = tex->createView();
  if (!view) return -1;
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding = 0u; db.type = DescriptorType::CombinedImageSampler;
  db.textureView = view; db.sampler = atlas_sampler_;
  dsd.bindings.push_back(db);
  auto set = device_->createDescriptorSet(atlas_set_layout_, dsd);
  if (!set) return -1;
  int slot = static_cast<int>(custom_set_.size());
  custom_tex_.push_back(std::move(tex));
  custom_view_.push_back(std::move(view));
  custom_set_.push_back(std::move(set));
  custom_index_[key] = slot;
  return slot;
}

bool BillboardRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // VS — generate quad corners from gl_VertexIndex; offset along camera_right
  // / camera_up; emit UV interpolated between atlas_uv.xy and atlas_uv.zw.
  // Instance data is fetched via vertex attributes location 1..4.
  const char* vsrc = R"glsl(
    layout(location = 1) in vec3  inWorldPos;
    layout(location = 2) in vec2  inSize;
    layout(location = 3) in vec4  inAtlasUV;     // xy = min, zw = max
    layout(location = 4) in vec4  inColor;       // a includes fade
    layout(location = 5) in float inEffect;      // 0静态 1脉冲 2跳动 3涟漪
    layout(location = 6) in float inPhase;       // 特效频率

    layout(push_constant) uniform PC {
      mat4 view_proj;
      vec4 camera_right;
      vec4 camera_up;
      vec4 fx;   // x=time_s
    } pc;

    layout(location = 0) out vec2  vUV;
    layout(location = 1) out vec4  vColor;
    layout(location = 2) out vec2  vCorner;  // [-0.5,0.5] 象限,涟漪用
    layout(location = 3) out float vEffect;

    void main() {
      vec2 corner = vec2(
          (gl_VertexIndex & 1) != 0 ?  0.5 : -0.5,
          (gl_VertexIndex & 2) != 0 ?  0.5 : -0.5);

      int   eff = int(inEffect + 0.5);
      float t   = pc.fx.x;
      float w   = max(inPhase, 0.05);
      vec2  size = inSize;
      vec3  center = inWorldPos;
      vec4  col = inColor;

      if (eff == 1) {            // 脉冲:缩放 + 透明呼吸
        float s = 0.5 + 0.5 * sin(t * w * 6.2831853);
        size *= 1.0 + 0.18 * s;
        col.a *= 0.55 + 0.45 * s;
      } else if (eff == 2) {     // 跳动:沿相机上方向 bob
        float b = abs(sin(t * w * 3.14159));
        center += pc.camera_up.xyz * (b * inSize.y * 0.5);
      } else if (eff == 3) {     // 涟漪环:整体扩大 3×,FS 画扩散环
        size *= 3.0;
      }

      vec3 offs  = pc.camera_right.xyz * (corner.x * size.x)
                 + pc.camera_up.xyz    * (corner.y * size.y);
      gl_Position = pc.view_proj * vec4(center + offs, 1.0);

      vUV     = mix(inAtlasUV.xy, inAtlasUV.zw, corner + 0.5);
      vColor  = col;
      vCorner = corner;
      vEffect = inEffect;
    }
  )glsl";

  // FS — 普通采 atlas;涟漪(effect 3)程序化画自中心向外扩散的环。
  const char* fsrc = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D uAtlas;
    layout(push_constant) uniform PC {
      mat4 view_proj; vec4 camera_right; vec4 camera_up; vec4 fx;
    } pc;
    layout(location = 0) in vec2  vUV;
    layout(location = 1) in vec4  vColor;
    layout(location = 2) in vec2  vCorner;
    layout(location = 3) in float vEffect;
    layout(location = 0) out vec4 outColor;
    void main() {
      if (int(vEffect + 0.5) == 3) {
        // 扩散波:半径随 time 从 0→1 扩散并淡出的细环(POI 定位)。
        float r = length(vCorner) * 2.0;            // 0 中心 .. 1 边缘
        float phase = fract(pc.fx.x * 0.5);          // 0..1 扩散进度
        float ring  = 1.0 - smoothstep(0.0, 0.08, abs(r - phase));
        float fade  = (1.0 - phase) * (1.0 - smoothstep(0.85, 1.0, r));
        float a = vColor.a * ring * fade;
        if (a < 0.01) discard;
        outColor = vec4(vColor.rgb, a);
        return;
      }
      vec4 tex = texture(uAtlas, vUV);
      outColor = tex * vColor;
      if (outColor.a < 0.01) discard;
    }
  )glsl";

  auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vs_r.success || !fs_r.success) {
    spdlog::error("BillboardRenderer: shader compile failed vs='{}' fs='{}'",
                  vs_r.errorMessage, fs_r.errorMessage);
    return false;
  }

  ShaderDesc vsd;
  vsd.stage = ShaderStageFlagBits::Vertex;
  vsd.spirvCode = vs_r.spirvCode;
  vs_ = device_->createShader(vsd);

  ShaderDesc fsd;
  fsd.stage = ShaderStageFlagBits::Fragment;
  fsd.spirvCode = fs_r.spirvCode;
  fs_ = device_->createShader(fsd);
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc ld;
  ld.setLayouts.push_back(atlas_set_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0u;
  pcr.size = sizeof(PushConstants);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) return false;

  // One vertex binding, per-instance: location 1..4.
  VertexInputBindingDescription inst_bind;
  inst_bind.binding     = 0u;
  inst_bind.stride      = sizeof(InstanceData);
  inst_bind.perInstance = true;

  std::vector<VertexInputAttributeDescription> attrs(6);
  attrs[0].location = 1u;
  attrs[0].binding  = 0u;
  attrs[0].format   = Format::R32G32B32_SFLOAT;
  attrs[0].offset   = offsetof(InstanceData, world_pos);
  attrs[1].location = 2u;
  attrs[1].binding  = 0u;
  attrs[1].format   = Format::R32G32_SFLOAT;
  attrs[1].offset   = offsetof(InstanceData, size_x);
  attrs[2].location = 3u;
  attrs[2].binding  = 0u;
  attrs[2].format   = Format::R32G32B32A32_SFLOAT;
  attrs[2].offset   = offsetof(InstanceData, atlas_uv);
  attrs[3].location = 4u;
  attrs[3].binding  = 0u;
  attrs[3].format   = Format::R32G32B32A32_SFLOAT;
  attrs[3].offset   = offsetof(InstanceData, color);
  attrs[4].location = 5u;
  attrs[4].binding  = 0u;
  attrs[4].format   = Format::R32_SFLOAT;
  attrs[4].offset   = offsetof(InstanceData, effect);
  attrs[5].location = 6u;
  attrs[5].binding  = 0u;
  attrs[5].format   = Format::R32_SFLOAT;
  attrs[5].offset   = offsetof(InstanceData, phase);

  // Alpha-blend on, depth-test on, depth-write off (so overlapping
  // billboards don't self-occlude when sorted incorrectly).
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::Zero;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings = {inst_bind};
  pd.vertexInput.attributes = attrs;
  pd.primitiveTopology = PrimitiveTopology::TriangleStrip;
  pd.rasterization.cullMode = CullMode::None;
  pd.rasterization.fillMode = FillMode::Solid;
  pd.depthStencil.depthTestEnable = true;
  pd.depthStencil.depthWriteEnable = false;
  pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void BillboardRenderer::uploadInstances(const std::vector<InstanceData>& inst) {
  const uint32_t needed = static_cast<uint32_t>(inst.size());
  if (needed == 0) return;
  if (!inst_vbo_ || inst_vbo_capacity_ < needed) {
    const uint32_t grown = std::max(needed * 2u, 64u);
    BufferDesc bd;
    bd.size = static_cast<uint64_t>(grown) * sizeof(InstanceData);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    inst_vbo_ = device_->createBuffer(bd);
    inst_vbo_capacity_ = grown;
  }
  if (!inst_vbo_) return;
  void* dst = inst_vbo_->map();
  std::memcpy(dst, inst.data(), inst.size() * sizeof(InstanceData));
  inst_vbo_->unmap();
}

void BillboardRenderer::record(const RHICommandBufferPtr& cmd,
                               const Params& p,
                               const std::vector<BillboardDraw>& instances) {
  if (instances.empty() || !pipeline_ || !atlas_set_) return;

  // Group by tex_slot so custom-icon billboards bind their own texture. All
  // groups are packed CONTIGUOUSLY into one instance VBO + uploaded once; each
  // group then draws by binding the instance buffer at its byte offset. (Doing
  // a separate uploadInstances() per group would be wrong: command-buffer
  // recording is deferred, so the VBO would hold only the LAST upload at
  // execute time → earlier draws read the wrong data.)
  std::vector<InstanceData> all;
  all.reserve(instances.size());
  struct Group { RHIDescriptorSetPtr set; uint32_t first; uint32_t count; };
  std::vector<Group> groups;
  auto addGroup = [&](const RHIDescriptorSetPtr& set, int slot) {
    const uint32_t first = static_cast<uint32_t>(all.size());
    for (const auto& b : instances) {
      if (b.tex_slot != slot) continue;
      InstanceData d{};
      d.world_pos = b.world_pos;
      d.size_x    = b.size_m.x;
      d.size_y    = b.size_m.y;
      d.effect    = b.effect;
      // 自定义贴图用整张图;内置 atlas 用各自的 sub-rect。
      d.atlas_uv  = slot >= 0 ? glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) : b.atlas_uv;
      d.color     = b.color;
      d.phase     = b.phase;
      d._pad      = 0.0f;
      if (p.fade_far_m > p.fade_near_m && p.fade_far_m > 0.0f) {
        glm::vec3 to_cam = b.world_pos - p.camera_pos_rtc;
        float dist = std::sqrt(glm::dot(to_cam, to_cam));
        d.color.a *= billboardFadeAlpha(dist, p.fade_near_m, p.fade_far_m);
        if (d.color.a < 0.01f) continue;   // skip — fully faded
      }
      all.push_back(d);
    }
    const uint32_t count = static_cast<uint32_t>(all.size()) - first;
    if (count && set) groups.push_back({set, first, count});
  };
  addGroup(atlas_set_, -1);
  for (int s = 0; s < static_cast<int>(custom_set_.size()); ++s)
    addGroup(custom_set_[s], s);
  if (all.empty()) return;

  uploadInstances(all);
  if (!inst_vbo_) return;

  PushConstants pc{};
  pc.view_proj    = p.view_proj;
  pc.camera_right = glm::vec4(p.camera_right, 0.0f);
  pc.camera_up    = glm::vec4(p.camera_up,    0.0f);
  pc.fx           = glm::vec4(p.time_s, 0.0f, 0.0f, 0.0f);

  cmd->bindPipeline(pipeline_);
  cmd->pushConstants(pipeline_layout_,
                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                     | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
                     0, sizeof(pc), &pc);
  // 4 verts per quad as a triangle strip × N instances, one draw per texture
  // group (bind that group's set + instance-buffer byte offset).
  for (const auto& g : groups) {
    cmd->bindDescriptorSet(0u, g.set, pipeline_layout_);
    cmd->bindVertexBuffer(0, inst_vbo_,
                          static_cast<uint64_t>(g.first) * sizeof(InstanceData));
    cmd->draw(4u, g.count, 0u, 0u);
  }
}

}  // namespace hongtu::renderer
