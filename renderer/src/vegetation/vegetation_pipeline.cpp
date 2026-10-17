#include <hongtu/renderer/vegetation/vegetation_pipeline.hpp>
#include <hongtu/renderer/pbr/instanced_pbr_pipeline.hpp>  // instance binding desc
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/effects/shading_glsl.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <string>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;
using ShadingGLSL = effects::ShadingGLSL;

bool VegetationPipeline::initialize(RHIDevice* device,
                                    RHIDescriptorSetLayoutPtr material_set_layout,
                                    RHIDescriptorSetLayoutPtr shadow_set_layout,
                                    RHIRenderPassPtr           render_pass,
                                    RHIDescriptorSetLayoutPtr ibl_set_layout) {
  device_  = device;
  has_ibl_ = (ibl_set_layout != nullptr);
  if (!createShaders()) return false;
  return createPipeline(material_set_layout, shadow_set_layout, render_pass,
                        ibl_set_layout);
}

bool VegetationPipeline::createShaders() {
  ShaderCompiler compiler;

  // Matches `PushConstants` byte-for-byte (InstancedPbrPipeline's block + the
  // two vegetation vec4s).
  static const char* kPushBlock =
      "layout(push_constant) uniform PC {\n"
      "  mat4 view_proj;\n"
      "  mat4 light_vp;\n"
      "  vec4 camera_pos_ws;\n"
      "  vec4 light_dir_ws;\n"
      "  vec4 env_params;\n"
      "  vec4 env_params2;\n"
      "  vec4 wind;\n"       // .xy dir·strength (world XY), .z time, .w freq
      "  vec4 archetype;\n"  // .x asset height (m), .y sway amplitude (m)
      "} pc;\n";

  // Vertex: InstancedPbrPipeline's VS + wind sway. Height weight uses the
  // ASSET-local Y (glTF up) so it survives the Y-up → Z-up instance rotation;
  // the displacement itself happens in world XY (Z-up ground plane).
  const std::string vsrc = std::string(
      "layout(location = 0) in vec3 inPosition;\n"
      "layout(location = 1) in vec3 inNormal;\n"
      "layout(location = 2) in vec2 inUv;\n"
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
      "  // Wind sway — quadratic height weight keeps the trunk planted.\n"
      "  float h01 = clamp(inPosition.y / max(pc.archetype.x, 0.001), 0.0, 1.0);\n"
      "  float weight = h01 * h01;\n"
      "  if (weight > 0.0 && dot(pc.wind.xy, pc.wind.xy) > 1e-6) {\n"
      "    float phase = dot(inModelCol3.xyz, vec3(0.73, 0.41, 1.31));\n"
      "    float t = pc.wind.z * pc.wind.w;\n"
      "    // Primary gust + a faster secondary flutter, per-instance phased.\n"
      "    float g = sin(t + phase) * 0.7 + sin(t * 2.33 + phase * 1.71) * 0.3;\n"
      "    vec2 d = pc.wind.xy * (g * weight * pc.archetype.y);\n"
      "    wp.xy += d;\n"
      "  }\n"
      "  vWorldPos      = wp.xyz;\n"
      "  vWorldNormal   = mat3(model) * inNormal;\n"
      "  vUv            = inUv;\n"
      "  vLightSpacePos = pc.light_vp * wp;\n"
      "  vBaseColor     = inBaseColor;\n"
      "  vMRParams      = inMRParams;\n"
      "  gl_Position    = pc.view_proj * wp;\n"
      "}\n";

  // Fragment: InstancedPbrPipeline's Cook-Torrance + IBL + PCF shadow body,
  // plus the alpha cutoff for MASK/BLEND foliage.
  const std::string ibl_decl = has_ibl_
      ? "layout(set = 2, binding = 0) uniform samplerCube envDiffuseCube;\n"
        "layout(set = 2, binding = 1) uniform samplerCube envSpecCube;\n"
      : "";
  const std::string fsrc = std::string(
      "layout(set = 0, binding = 0) uniform sampler2D baseColorTex;\n"
      "layout(set = 0, binding = 1) uniform sampler2D normalTex;\n"
      "layout(set = 0, binding = 2) uniform sampler2D mrTex;\n"
      "layout(set = 0, binding = 3) uniform sampler2D emissiveTex;\n"
      "layout(set = 1, binding = 0) uniform sampler2D shadowMap;\n")
      + ibl_decl
      + std::string(
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
      "  // Foliage alpha cutoff (no-op for opaque materials, alpha = 1).\n"
      "  // Box-filtered mips dilute alpha, so a fixed 0.5 test thins distant\n"
      "  // canopies to skeletons — compensate by scaling alpha with the\n"
      "  // sampled mip level (Castano-style conservative alpha).\n"
      "  float mip = textureQueryLod(baseColorTex, vUv).x;\n"
      "  // 补偿必须封顶:顶层 mip 的 alpha 被稀释后无限放大会让整张叶卡\n"
      "  // 过 cutoff,而透明区 RGB 是黑底 → 树冠闪纯黑方块(云渲染实测)。\n"
      "  float a = baseSample.a * vBaseColor.a * min(1.0 + mip * 0.45, 2.0);\n"
      "  if (a < 0.5) discard;\n"
      "  // un-premultiply:mip 把叶色和黑底平均,除回 alpha 还原叶色,\n"
      "  // 过 cutoff 的边缘/远景 texel 不再发黑(不透明材质 a=1 无影响)。\n"
      "  vec3 albedo     = baseSample.rgb / max(baseSample.a, 0.25) * vBaseColor.rgb;\n"
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
      "  // Foliage transmission — thin leaves glow when backlit. Approximate\n"
      "  // with the NEGATIVE NdotL lobe; gated by alpha-tested pixels having\n"
      "  // alpha < 1 in the source would be ideal, but a small global term\n"
      "  // reads right on bark too (sub-surface-ish warmth).\n"
      "  float backlit = max(-dot(N, L), 0.0);\n"
      "  Lo_unshad += albedo * vec3(1.0, 0.95, 0.7) * (backlit * backlit * 0.28)\n"
      "             * pc.light_dir_ws.w / 3.14159265;\n"
      "  vec3 lsCoords = vLightSpacePos.xyz / vLightSpacePos.w;\n"
      "  lsCoords.xy = lsCoords.xy * 0.5 + 0.5;\n"
      "  float bias = max(0.006 * (1.0 - NdotL), 0.0015);\n"
      "  float shadowRaw = sampleShadowPCF(shadowMap, lsCoords, bias);\n"
      "  float shadow = mix(1.0, shadowRaw, pc.env_params.w);\n"
      "  vec3 Lo = Lo_unshad * shadow;\n"
      + std::string(has_ibl_ ?
      // 真 cube IBL(与 PbrForwardPipeline 同款)
      "  vec3 R = reflect(-V, N);\n"
      "  vec3 irradiance = texture(envDiffuseCube, N).rgb;\n"
      "  float specMipMax = log2(float(textureSize(envSpecCube, 0).x));\n"
      "  vec3 prefilt = textureLod(envSpecCube, R, roughness * specMipMax).rgb;\n"
      "  vec2 envBRDF = envBRDFApprox(NdotV, roughness);\n"
      "  vec3 kS_ibl = F0 + (vec3(1.0) - F0)\n"
      "               * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);\n"
      "  vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);\n"
      "  vec3 ibl = (kD_ibl * irradiance * albedo\n"
      "             + prefilt * (F0 * envBRDF.x + envBRDF.y)) * pc.env_params.y;\n"
      :
      "  vec3 ibl = evalAnalyticIBL(N, V, F0, albedo, roughness, metallic, pc.env_params.x);\n"
      "  ibl *= pc.env_params.y;\n") +
      "  vec3 bleed = skylightBleed(N, pc.env_params.x, pc.env_params2.x, metallic);\n"
      "  vec3 hdr = (Lo + ibl + bleed) * pc.env_params.z;\n"
      "  // 自发光(路灯灯泡 / 演练要素):×night_factor(env_params2.w)夜间发亮、白天灭。\n"
      "  // 注:之前误乘 env_params2.z(=lod_bias=0)导致自发光恒为 0(全死);改用 .w。\n"
      "  hdr += texture(emissiveTex, vUv).rgb * pc.env_params2.w * 6.0;\n"
      "  // NaN 取证探针(env_params2.y>1.5 开):NaN/INF 染亮品红。\n"
      "  if (pc.env_params2.y > 1.5 && (any(isnan(hdr)) || any(isinf(hdr)))) {\n"
      "    outColor = vec4(100.0, 0.0, 100.0, 1.0); return;\n"
      "  }\n"
      "  if (pc.env_params2.w > 0.5) hdr = acesFilmic(hdr);\n"
      "  outColor = vec4(hdr, 1.0);\n"
      "}\n";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  if (!vr.success) {
    spdlog::error("VegetationPipeline vs fail: {}", vr.errorMessage);
    return false;
  }
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!fr.success) {
    spdlog::error("VegetationPipeline fs fail: {}", fr.errorMessage);
    return false;
  }

  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vert_ = device_->createShader(vd);
  frag_ = device_->createShader(fd);
  return vert_ && frag_;
}

bool VegetationPipeline::createPipeline(RHIDescriptorSetLayoutPtr material_set_layout,
                                        RHIDescriptorSetLayoutPtr shadow_set_layout,
                                        RHIRenderPassPtr           render_pass,
                                        RHIDescriptorSetLayoutPtr ibl_set_layout) {
  PipelineLayoutDesc layoutDesc;
  layoutDesc.setLayouts.push_back(material_set_layout);  // set 0
  layoutDesc.setLayouts.push_back(shadow_set_layout);    // set 1
  if (ibl_set_layout) layoutDesc.setLayouts.push_back(ibl_set_layout);  // set 2
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
  // Same two-binding contract as InstancedPbrPipeline — reuse its descs so
  // the 96-byte instance stream stays interchangeable between both.
  pd.vertexInput.bindings   = {MeshVertex::binding(),
                               InstancedPbrPipeline::getInstanceVertexBinding()};
  pd.vertexInput.attributes = MeshVertex::attributes();
  for (const auto& a : InstancedPbrPipeline::getInstanceVertexAttributes()) {
    pd.vertexInput.attributes.push_back(a);
  }
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;  // double-sided foliage
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = render_pass;
  pd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("VegetationPipeline: pipeline creation failed");
    return false;
  }
  return true;
}

}  // namespace hongtu::renderer
