#include <hongtu/renderer/pbr/instanced_pbr_pipeline.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/effects/shading_glsl.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <cstddef>
#include <iostream>
#include <string>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;
using ShadingGLSL = effects::ShadingGLSL;

VertexInputBindingDescription
InstancedPbrPipeline::getInstanceVertexBinding() {
  VertexInputBindingDescription b;
  b.binding     = 1u;
  b.stride      = sizeof(InstanceVertex);
  b.perInstance = true;
  return b;
}

std::vector<VertexInputAttributeDescription>
InstancedPbrPipeline::getInstanceVertexAttributes() {
  // model mat4 spans locations 3..6 (one vec4 per row/col). base_color is 7,
  // mr_params is 8. Standard MeshVertex occupies 0..2 (pos / normal / uv).
  std::vector<VertexInputAttributeDescription> a(6);
  for (uint32_t i = 0; i < 4; ++i) {
    a[i].location = 3u + i;
    a[i].binding  = 1u;
    a[i].format   = Format::R32G32B32A32_SFLOAT;
    a[i].offset   = static_cast<uint32_t>(
                        offsetof(InstanceVertex, model) + i * sizeof(glm::vec4));
  }
  a[4].location = 7u;
  a[4].binding  = 1u;
  a[4].format   = Format::R32G32B32A32_SFLOAT;
  a[4].offset   = offsetof(InstanceVertex, base_color);
  a[5].location = 8u;
  a[5].binding  = 1u;
  a[5].format   = Format::R32G32B32A32_SFLOAT;
  a[5].offset   = offsetof(InstanceVertex, mr_params);
  return a;
}

bool InstancedPbrPipeline::initialize(RHIDevice* device,
                                      RHIDescriptorSetLayoutPtr material_set_layout,
                                      RHIDescriptorSetLayoutPtr shadow_set_layout,
                                      RHIRenderPassPtr           render_pass) {
  device_ = device;
  if (!createShaders()) return false;
  return createPipeline(material_set_layout, shadow_set_layout, render_pass);
}

bool InstancedPbrPipeline::createShaders() {
  ShaderCompiler compiler;

  // Push constants block — view_proj + light + camera + env tunables.
  // Matches `PushConstants` byte-for-byte.
  static const char* kPushBlock =
      "layout(push_constant) uniform PC {\n"
      "  mat4 view_proj;\n"
      "  mat4 light_vp;\n"
      "  vec4 camera_pos_ws;\n"
      "  vec4 light_dir_ws;\n"
      "  vec4 env_params;\n"
      "  vec4 env_params2;\n"
      "} pc;\n";

  // Per-vertex attributes (binding=0) at locations 0..2 — standard MeshVertex.
  // Per-instance attributes (binding=1) at locations 3..8 — InstanceVertex.
  const std::string vsrc = std::string(
      "layout(location = 0) in vec3 inPosition;\n"
      "layout(location = 1) in vec3 inNormal;\n"
      "layout(location = 2) in vec2 inUv;\n"
      // Instance model matrix unpacked from 4 vec4s.
      "layout(location = 3) in vec4 inModelCol0;\n"
      "layout(location = 4) in vec4 inModelCol1;\n"
      "layout(location = 5) in vec4 inModelCol2;\n"
      "layout(location = 6) in vec4 inModelCol3;\n"
      "layout(location = 7) in vec4 inBaseColor;\n"
      "layout(location = 8) in vec4 inMRParams;\n"
      "layout(location = 0) out vec3 vWorldPos;\n"
      "layout(location = 1) out vec3 vWorldNormal;\n"
      "layout(location = 2) out vec2 vUv;\n"
      "layout(location = 3) out vec4 vLightSpacePos;\n"
      "layout(location = 4) out vec4 vBaseColor;\n"
      "layout(location = 5) out vec4 vMRParams;\n")
      + kPushBlock +
      "void main() {\n"
      "  mat4 model = mat4(inModelCol0, inModelCol1, inModelCol2, inModelCol3);\n"
      "  vec4 wp = model * vec4(inPosition, 1.0);\n"
      "  vWorldPos      = wp.xyz;\n"
      "  vWorldNormal   = mat3(model) * inNormal;\n"
      "  vUv            = inUv;\n"
      "  vLightSpacePos = pc.light_vp * wp;\n"
      "  vBaseColor     = inBaseColor;\n"
      "  vMRParams      = inMRParams;\n"
      "  gl_Position    = pc.view_proj * wp;\n"
      "}\n";

  // Fragment — same Cook-Torrance + IBL + PCF shadow as PbrForwardPipeline,
  // but with per-instance base_color / mr_params on inputs (instead of push
  // constants). No debug modes — keep it focused on the instancing demo.
  const std::string fsrc = std::string(
      "layout(set = 0, binding = 0) uniform sampler2D baseColorTex;\n"
      "layout(set = 0, binding = 1) uniform sampler2D normalTex;\n"
      "layout(set = 0, binding = 2) uniform sampler2D mrTex;\n"
      "layout(set = 1, binding = 0) uniform sampler2D shadowMap;\n"
      "layout(location = 0) in vec3 vWorldPos;\n"
      "layout(location = 1) in vec3 vWorldNormal;\n"
      "layout(location = 2) in vec2 vUv;\n"
      "layout(location = 3) in vec4 vLightSpacePos;\n"
      "layout(location = 4) in vec4 vBaseColor;\n"
      "layout(location = 5) in vec4 vMRParams;\n"
      "layout(location = 0) out vec4 outColor;\n")
      + kPushBlock
      + ShadingGLSL::fresnelSchlick()
      + ShadingGLSL::distributionGGX()
      + ShadingGLSL::geometrySmith()
      + ShadingGLSL::tbnFromDerivatives()
      + ShadingGLSL::analyticEnv()
      + ShadingGLSL::envBRDFApprox()
      + ShadingGLSL::evalAnalyticIBL()
      + ShadingGLSL::shadowPCF()
      + ShadingGLSL::acesTonemap() +
      "void main() {\n"
      "  vec4 baseSample = texture(baseColorTex, vUv, pc.env_params2.z);\n"
      "  vec3 albedo     = baseSample.rgb * vBaseColor.rgb;\n"
      "  vec3 nm         = texture(normalTex, vUv).xyz * 2.0 - 1.0;\n"
      "  vec3 mr         = texture(mrTex,     vUv).rgb;\n"
      "  float roughness = clamp(mr.g * vMRParams.y, 0.04, 1.0);\n"
      "  float metallic  = clamp(mr.b * vMRParams.x, 0.0,  1.0);\n"
      "  vec3 N_geom = normalize(vWorldNormal);\n"
      "  if (!gl_FrontFacing) N_geom = -N_geom;\n"
      "  mat3 TBN = cotangentFrame(N_geom, vWorldPos, vUv);\n"
      "  vec3 N = normalize(TBN * nm);\n"
      "  vec3 V = normalize(pc.camera_pos_ws.xyz - vWorldPos);\n"
      "  vec3 L = normalize(pc.light_dir_ws.xyz);\n"
      "  vec3 H = normalize(V + L);\n"
      "  float NdotL = max(dot(N, L), 0.0);\n"
      "  float NdotV = max(dot(N, V), 0.0);\n"
      "  vec3  F0 = mix(vec3(0.04), albedo, metallic);\n"
      "  vec3  F  = fresnelSchlick(max(dot(H, V), 0.0), F0);\n"
      "  float D  = distributionGGX(N, H, roughness);\n"
      "  float G  = geometrySmith   (N, V, L, roughness);\n"
      "  vec3  spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);\n"
      "  vec3  kS = F;\n"
      "  vec3  kD = (1.0 - kS) * (1.0 - metallic);\n"
      "  vec3  Lo_unshad = (kD * albedo / 3.14159265 + spec) * pc.light_dir_ws.w * NdotL;\n"
      "  vec3 lsCoords = vLightSpacePos.xyz / vLightSpacePos.w;\n"
      "  lsCoords.xy = lsCoords.xy * 0.5 + 0.5;\n"
      "  float bias = max(0.006 * (1.0 - NdotL), 0.0015);\n"
      "  float shadowRaw = sampleShadowPCF(shadowMap, lsCoords, bias);\n"
      "  float shadow = mix(1.0, shadowRaw, pc.env_params.w);\n"
      "  vec3 Lo = Lo_unshad * shadow;\n"
      "  vec3 ibl = evalAnalyticIBL(N, V, F0, albedo, roughness, metallic, pc.env_params.x);\n"
      "  ibl *= pc.env_params.y;\n"
      "  vec3 bleed = skylightBleed(N, pc.env_params.x, pc.env_params2.x, metallic);\n"
      "  vec3 hdr = (Lo + ibl + bleed) * pc.env_params.z;\n"
      "  if (pc.env_params2.w > 0.5) hdr = acesFilmic(hdr);\n"
      "  outColor = vec4(hdr, vBaseColor.a);\n"
      "}\n";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  if (!vr.success) {
    std::cerr << "InstancedPbrPipeline vs fail: " << vr.errorMessage << "\n";
    return false;
  }
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!fr.success) {
    std::cerr << "InstancedPbrPipeline fs fail: " << fr.errorMessage << "\n";
    return false;
  }

  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vert_ = device_->createShader(vd);
  frag_ = device_->createShader(fd);
  return vert_ && frag_;
}

bool InstancedPbrPipeline::createPipeline(
    RHIDescriptorSetLayoutPtr material_set_layout,
    RHIDescriptorSetLayoutPtr shadow_set_layout,
    RHIRenderPassPtr           render_pass) {
  PipelineLayoutDesc layoutDesc;
  layoutDesc.setLayouts.push_back(material_set_layout);  // set 0
  layoutDesc.setLayouts.push_back(shadow_set_layout);    // set 1
  PushConstantRange pc;
  pc.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pc.offset = 0;
  pc.size   = sizeof(PushConstants);
  layoutDesc.pushConstantRanges.push_back(pc);
  pipeline_layout_ = device_->createPipelineLayout(layoutDesc);
  if (!pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders = {vert_, frag_};
  // Two vertex bindings: 0 = per-vertex MeshVertex, 1 = per-instance.
  pd.vertexInput.bindings   = {MeshVertex::binding(), getInstanceVertexBinding()};
  pd.vertexInput.attributes = MeshVertex::attributes();
  for (const auto& a : getInstanceVertexAttributes()) {
    pd.vertexInput.attributes.push_back(a);
  }
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = render_pass;
  pd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

}  // namespace hongtu::renderer
