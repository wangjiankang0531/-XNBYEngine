#include <hongtu/renderer/pbr/ground_plane_pass.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/effects/shading_glsl.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;
using ShadingGLSL = effects::ShadingGLSL;

bool GroundPlanePass::initialize(RHIDevice* device,
                                 RHIDescriptorSetLayoutPtr material_set_layout,
                                 RHIDescriptorSetLayoutPtr shadow_set_layout,
                                 RHIRenderPassPtr           render_pass,
                                 RHIDescriptorSetLayoutPtr ibl_set_layout,
                                 RHIDescriptorSetLayoutPtr lights_set_layout) {
  device_  = device;
  has_ibl_    = (ibl_set_layout != nullptr);
  has_lights_ = (ibl_set_layout != nullptr && lights_set_layout != nullptr);
  if (!createShaders()) return false;

  PipelineLayoutDesc pl;
  pl.setLayouts.push_back(material_set_layout);                   // set 0 lawn
  pl.setLayouts.push_back(shadow_set_layout);                     // set 1 shadow
  if (ibl_set_layout) pl.setLayouts.push_back(ibl_set_layout);   // set 2 IBL
  // 点光源 UBO set=3:只在 IBL 也非空时才追加,保持 set 索引连续。
  if (ibl_set_layout && lights_set_layout)
    pl.setLayouts.push_back(lights_set_layout);                   // set 3 lights
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size   = sizeof(PushConstants);
  pl.pushConstantRanges.push_back(pcr);
  layout_ = device_->createPipelineLayout(pl);
  if (!layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings   = {MeshVertex::binding()};
  pd.vertexInput.attributes = MeshVertex::attributes();
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;  // 单平面,不赌绕序
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = render_pass;
  pd.pipelineLayout                = layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("GroundPlanePass: pipeline creation failed");
    return false;
  }
  return true;
}

bool GroundPlanePass::createShaders() {
  ShaderCompiler compiler;

  static const char* kPush =
      "layout(push_constant) uniform PC {\n"
      "  mat4 view_proj;\n"
      "  mat4 light_vp;\n"
      "  vec4 camera_pos_ws;\n"
      "  vec4 light_dir_ws;\n"
      "  vec4 env_params;\n"
      "  vec4 env_params2;\n"
      "  vec4 tiling;\n"   // .x detail uv/m, .y macro uv/m, .z near, .w far
      "} pc;\n";

  const std::string vsrc = std::string(
      "layout(location = 0) in vec3 inPosition;\n"
      "layout(location = 1) in vec3 inNormal;\n"
      "layout(location = 2) in vec2 inUv;\n"
      "layout(location = 0) out vec3 vWorldPos;\n"
      "layout(location = 1) out vec4 vLightSpacePos;\n")
      + kPush +
      "void main() {\n"
      "  vWorldPos      = inPosition;\n"
      "  vLightSpacePos = pc.light_vp * vec4(inPosition, 1.0);\n"
      "  gl_Position    = pc.view_proj * vec4(inPosition, 1.0);\n"
      "}\n";

  const std::string ibl_decl = has_ibl_
      ? "layout(set = 2, binding = 0) uniform samplerCube envDiffuseCube;\n"
        "layout(set = 2, binding = 1) uniform samplerCube envSpecCube;\n"
      : "";

  const std::string fsrc = std::string(
      "layout(set = 0, binding = 0) uniform sampler2D baseColorTex;\n"
      "layout(set = 0, binding = 1) uniform sampler2D normalTex;\n"
      "layout(set = 0, binding = 2) uniform sampler2D mrTex;\n"
      "layout(set = 1, binding = 0) uniform sampler2D shadowMap;\n")
      + ibl_decl
      + std::string(
      "layout(location = 0) in vec3 vWorldPos;\n"
      "layout(location = 1) in vec4 vLightSpacePos;\n"
      "layout(location = 0) out vec4 outColor;\n")
      + kPush
      // BRDF helper 须先于 pointLights(pointLights 调用 fresnelSchlick 等)。
      + ShadingGLSL::fresnelSchlick()
      + ShadingGLSL::distributionGGX()
      + ShadingGLSL::geometrySmith()
      + ShadingGLSL::analyticEnv()
      + ShadingGLSL::envBRDFApprox()
      + ShadingGLSL::evalAnalyticIBL()
      + ShadingGLSL::shadowPCF()
      // set=3 点光源 UBO 声明只在 layout 真有 set=3 时拼入(同 has_lights_)。
      + (has_lights_ ? ShadingGLSL::pointLights() : std::string()) +
      // 低频 value noise — 宏观明暗变化打破平铺感。
      "float gp_hash(vec2 p) {\n"
      "  p = fract(p * vec2(123.34, 456.21));\n"
      "  p += dot(p, p + 45.32);\n"
      "  return fract(p.x * p.y);\n"
      "}\n"
      "float gp_noise(vec2 p) {\n"
      "  vec2 i = floor(p), f = fract(p);\n"
      "  f = f * f * (3.0 - 2.0 * f);\n"
      "  return mix(mix(gp_hash(i), gp_hash(i + vec2(1, 0)), f.x),\n"
      "             mix(gp_hash(i + vec2(0, 1)), gp_hash(i + vec2(1, 1)), f.x),\n"
      "             f.y);\n"
      "}\n"
      "void main() {\n"
      "  float dist = length(pc.camera_pos_ws.xyz - vWorldPos);\n"
      "  // 双尺度反平铺:近 = 细节铺(看得见草叶),远 = 宏观铺(重复周期\n"
      "  // 放大 8 倍,几百米外不再读出网格状重复)。\n"
      "  vec2 uvd = vWorldPos.xy * pc.tiling.x;\n"
      "  vec2 uvm = vWorldPos.xy * pc.tiling.y;\n"
      "  float k = smoothstep(pc.tiling.z, pc.tiling.w, dist);\n"
      "  vec3 albedo = mix(texture(baseColorTex, uvd).rgb,\n"
      "                    texture(baseColorTex, uvm).rgb, k);\n"
      "  // 宏观色斑(~25m 周期)±8% 明暗,打破大面积均匀感。\n"
      "  float v = gp_noise(vWorldPos.xy * 0.04);\n"
      "  albedo *= 0.92 + 0.16 * v;\n"
      "  vec3 N = vec3(0.0, 0.0, 1.0);\n"
      "  vec3 V = normalize(pc.camera_pos_ws.xyz - vWorldPos);\n"
      "  vec3 L = normalize(pc.light_dir_ws.xyz);\n"
      "  float NdotL = max(dot(N, L), 0.0);\n"
      "  vec3 Lo = albedo / 3.14159265 * pc.light_dir_ws.w * NdotL;\n"
      "  vec3 lsCoords = vLightSpacePos.xyz / vLightSpacePos.w;\n"
      "  lsCoords.xy = lsCoords.xy * 0.5 + 0.5;\n"
      "  float bias = max(0.006 * (1.0 - NdotL), 0.0015);\n"
      "  float shadow = mix(1.0, sampleShadowPCF(shadowMap, lsCoords, bias),\n"
      "                     pc.env_params.w);\n"
      "  Lo *= shadow;\n"
      + std::string(has_ibl_ ?
      "  vec3 ibl = texture(envDiffuseCube, N).rgb * albedo * pc.env_params.y;\n"
      :
      "  vec3 ibl = evalAnalyticIBL(N, V, vec3(0.04), albedo, 1.0, 0.0,\n"
      "                             pc.env_params.x) * pc.env_params.y;\n") +
      // 点光源:地面固定 metallic=0、roughness=0.9、F0=0.04(草地漫反射主导)。
      // 仅 layout 有 set=3 时调用(否则 shader 引用缺失的 set → MoltenVK 崩)。
      (has_lights_
             ? std::string("  Lo += evalPointLights(vWorldPos, N, V, albedo, "
                           "0.0, 0.9, vec3(0.04));\n")
             : std::string()) +
      "  vec3 hdr = (Lo + ibl) * pc.env_params.z;\n"
      "  outColor = vec4(hdr, 1.0);\n"
      "}\n";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("GroundPlanePass shader: {}",
                  vr.success ? fr.errorMessage : vr.errorMessage);
    return false;
  }
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  return vs_ && fs_;
}

bool GroundPlanePass::buildPlane(float half_size, int divisions) {
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  for (int j = 0; j <= divisions; ++j) {
    for (int i = 0; i <= divisions; ++i) {
      MeshVertex v;
      v.position = {-half_size + 2.0f * half_size * i / divisions,
                    -half_size + 2.0f * half_size * j / divisions, 0.0f};
      v.normal   = {0, 0, 1};
      v.uv       = {0, 0};  // 着色器用世界坐标推 UV,网格 UV 不用
      verts.push_back(v);
    }
  }
  for (int j = 0; j < divisions; ++j) {
    for (int i = 0; i < divisions; ++i) {
      uint32_t a = j * (divisions + 1) + i, b = a + 1,
               c = a + divisions + 1, d = c + 1;
      idx.insert(idx.end(), {a, c, b, b, c, d});  // -Z 叉积绕序
    }
  }
  BufferDesc vd;
  vd.size  = verts.size() * sizeof(MeshVertex);
  vd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  vd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                      | MemoryPropertyFlagBits::HostCoherent;
  vbo_ = device_->createBuffer(vd);
  BufferDesc id;
  id.size  = idx.size() * sizeof(uint32_t);
  id.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
  id.memoryProperties = MemoryPropertyFlagBits::HostVisible
                      | MemoryPropertyFlagBits::HostCoherent;
  ibo_ = device_->createBuffer(id);
  if (!vbo_ || !ibo_) return false;
  std::memcpy(vbo_->map(), verts.data(), vd.size);
  vbo_->unmap();
  std::memcpy(ibo_->map(), idx.data(), id.size);
  ibo_->unmap();
  index_count_ = static_cast<uint32_t>(idx.size());
  built_half_  = half_size;
  return true;
}

void GroundPlanePass::recordMesh(const RHICommandBufferPtr& cmd,
                                 const PushConstants& pc,
                                 const PbrMaterial& material,
                                 const RHIDescriptorSetPtr& shadow_set,
                                 const RHIBufferPtr& vbo, const RHIBufferPtr& ibo,
                                 uint32_t index_count,
                                 const RHIDescriptorSetPtr& ibl_set,
                                 uint32_t index_offset,
                                 const RHIDescriptorSetPtr& lights_set) {
  if (!pipeline_ || !vbo || !ibo || index_count == 0 || !material.desc_set) return;
  cmd->bindPipeline(pipeline_);
  cmd->bindDescriptorSet(0u, material.desc_set, layout_,
                         PipelineBindPoint::Graphics);
  if (shadow_set) {
    cmd->bindDescriptorSet(1u, shadow_set, layout_, PipelineBindPoint::Graphics);
  }
  if (has_ibl_ && ibl_set) {
    cmd->bindDescriptorSet(2u, ibl_set, layout_, PipelineBindPoint::Graphics);
  }
  if (has_lights_ && lights_set) {
    cmd->bindDescriptorSet(3u, lights_set, layout_, PipelineBindPoint::Graphics);
  }
  cmd->pushConstants(layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->bindVertexBuffer(0, vbo);
  cmd->bindIndexBuffer(ibo);
  cmd->drawIndexed(index_count, 1u, index_offset);
}

void GroundPlanePass::record(const RHICommandBufferPtr& cmd,
                             const PushConstants& pc,
                             const PbrMaterial& material,
                             const RHIDescriptorSetPtr& shadow_set,
                             const RHIDescriptorSetPtr& ibl_set,
                             const RHIDescriptorSetPtr& lights_set) {
  if (!pipeline_ || !vbo_ || !material.desc_set) return;
  cmd->bindPipeline(pipeline_);
  cmd->bindDescriptorSet(0u, material.desc_set, layout_,
                         PipelineBindPoint::Graphics);
  if (shadow_set) {
    cmd->bindDescriptorSet(1u, shadow_set, layout_, PipelineBindPoint::Graphics);
  }
  if (has_ibl_ && ibl_set) {
    cmd->bindDescriptorSet(2u, ibl_set, layout_, PipelineBindPoint::Graphics);
  }
  if (has_lights_ && lights_set) {
    cmd->bindDescriptorSet(3u, lights_set, layout_, PipelineBindPoint::Graphics);
  }
  cmd->pushConstants(layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->bindVertexBuffer(0, vbo_);
  cmd->bindIndexBuffer(ibo_);
  cmd->drawIndexed(index_count_);
}

}  // namespace hongtu::renderer
