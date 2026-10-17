#include <hongtu/renderer/pbr/pbr_forward_pipeline.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/effects/shading_glsl.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <vulkan/vulkan.h>

#include <iostream>
#include <string>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;
using ShadingGLSL = effects::ShadingGLSL;

bool PbrForwardPipeline::initialize(RHIDevice* device,
                                      RHIDescriptorSetLayoutPtr material_set_layout,
                                      RHIDescriptorSetLayoutPtr shadow_set_layout,
                                      Format hdr_color_format) {
  device_ = device;
  has_lights_ = false;  // 基础路径无 set=3 点光源 UBO
  if (!createRenderPass(hdr_color_format)) return false;
  if (!createShaders())                    return false;
  if (!createPipeline(material_set_layout, shadow_set_layout)) return false;
  return true;
}

bool PbrForwardPipeline::initializeWithRenderPass(
    RHIDevice* device,
    RHIDescriptorSetLayoutPtr material_set_layout,
    RHIDescriptorSetLayoutPtr shadow_set_layout,
    RHIRenderPassPtr existing_render_pass) {
  device_      = device;
  render_pass_ = existing_render_pass;
  has_ibl_     = false;
  has_lights_  = false;  // 嵌入路径(demo/CRS)无 set=3 点光源 UBO
  if (!createShaders()) return false;
  return createPipeline(material_set_layout, shadow_set_layout);
}

bool PbrForwardPipeline::initializeWithIbl(
    RHIDevice* device,
    RHIDescriptorSetLayoutPtr material_set_layout,
    RHIDescriptorSetLayoutPtr shadow_set_layout,
    RHIDescriptorSetLayoutPtr ibl_set_layout,
    RHIRenderPassPtr existing_render_pass,
    RHIDescriptorSetLayoutPtr lights_set_layout,
    bool instanced) {
  device_      = device;
  render_pass_ = existing_render_pass;
  has_ibl_     = (ibl_set_layout != nullptr);
  // 与下方 layout 追加 set=3 的条件严格一致(IBL 与 lights 都在才有 set=3)。
  has_lights_  = (ibl_set_layout != nullptr && lights_set_layout != nullptr);
  instanced_   = instanced;
  if (!createShaders()) return false;
  // 管线 layout:set=0 material / set=1 shadow / set=2 IBL / set=3 点光源 UBO。
  // 只在 IBL 也存在时才追加 lights —— shader 里 set=3 紧跟 set=2;
  // 若 IBL 空但 lights 非空,set 编号会错位。编辑器始终两者都传。
  PipelineLayoutDesc pld;
  pld.setLayouts.push_back(material_set_layout);
  pld.setLayouts.push_back(shadow_set_layout);
  if (ibl_set_layout) pld.setLayouts.push_back(ibl_set_layout);
  if (ibl_set_layout && lights_set_layout) pld.setLayouts.push_back(lights_set_layout);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                    static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size   = sizeof(PushConstants);
  pld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pld);
  if (!pipeline_layout_) return false;
  // Reuse the standard pipeline build (vertex layout / blend / depth /
  // shader stages are identical — the only difference is the pipeline
  // layout which already accounts for the optional IBL set 2).
  GraphicsPipelineDesc gpd;
  gpd.shaders                       = {vert_, frag_};
  gpd.vertexInput.bindings          = {MeshVertex::binding()};
  gpd.vertexInput.attributes        = MeshVertex::attributes();
  if (instanced_) {
    // binding=1:per-instance 模型矩阵(mat4 = 4×vec4),perInstance。
    VertexInputBindingDescription ib;
    ib.binding     = 1;
    ib.stride      = sizeof(float) * 16;
    ib.perInstance = true;
    gpd.vertexInput.bindings.push_back(ib);
    for (uint32_t c = 0; c < 4u; ++c) {
      VertexInputAttributeDescription a;
      a.location = 3u + c;
      a.binding  = 1u;
      a.format   = Format::R32G32B32A32_SFLOAT;
      a.offset   = c * sizeof(float) * 4;
      gpd.vertexInput.attributes.push_back(a);
    }
  }
  gpd.primitiveTopology             = PrimitiveTopology::TriangleList;
  gpd.rasterization.cullMode        = CullMode::None;
  // glTF is CCW-front; with the engine's GL-style proj + [1][1]*=-1 Y-flip
  // the framebuffer winding stays CCW. The RHI default (frontCCW=false →
  // VK_FRONT_FACE_CLOCKWISE) classified EVERY spec-compliant triangle as
  // back-facing, so the shader's `if (!gl_FrontFacing) N = -N` flipped all
  // normals — terrain lit as if facing down (black in PBR mode).
  gpd.rasterization.frontCounterClockwise = true;
  gpd.rasterization.fillMode        = FillMode::Solid;
  gpd.depthStencil.depthTestEnable  = true;
  gpd.depthStencil.depthWriteEnable = true;
  gpd.renderPass                    = render_pass_;
  gpd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(gpd);
  return pipeline_ != nullptr;
}

bool PbrForwardPipeline::initializeWithIblOwnedPass(
    RHIDevice* device,
    RHIDescriptorSetLayoutPtr material_set_layout,
    RHIDescriptorSetLayoutPtr shadow_set_layout,
    RHIDescriptorSetLayoutPtr ibl_set_layout,
    Format hdr_color_format) {
  device_ = device;
  // Build the offscreen HDR render pass (identical to `initialize`), then run
  // the IBL-capable build against it. No set=3 point-lights for this path
  // (cloud server doesn't drive forward point lights through this pipeline).
  if (!createRenderPass(hdr_color_format)) return false;
  return initializeWithIbl(device, material_set_layout, shadow_set_layout,
                            ibl_set_layout, render_pass_,
                            /*lights_set_layout=*/nullptr, /*instanced=*/false);
}

bool PbrForwardPipeline::createRenderPass(Format hdr_color_format) {
  // Forward pass writes into an offscreen HDR target (caller-owned). The
  // compose pass tonemaps it into the swapchain.
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format        = hdr_color_format;
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;
  ca.loadOpClear   = true;
  ca.storeOpStore  = true;
  rpd.attachments.push_back(ca);

  AttachmentDescription da;
  da.format        = Format::D32_SFLOAT;
  da.sampleCount   = 1;
  da.initialLayout = ImageLayout::Undefined;
  // SSAO / SSR sample this depth in a subsequent fullscreen pass — keep it
  // around and transition to a read-only layout so the next pipeline can
  // bind it as a CombinedImageSampler.
  da.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;
  da.loadOpClear   = true;
  da.storeOpStore  = true;
  rpd.attachments.push_back(da);

  SubpassDescription sp;
  AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  sp.depthStencilAttachment.attachment = 1;
  sp.depthStencilAttachment.layout = ImageLayout::DepthStencilAttachmentOptimal;
  sp.hasDepthStencil = true;
  rpd.subpasses.push_back(sp);

  SubpassDependency dep;
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput)
                   | static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
  dep.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput)
                   | static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite)
                    | static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
  rpd.dependencies.push_back(dep);

  // 出口依赖(0→EXTERNAL):HDR 颜色/深度随后被 compose / SSAO 的片元着色器
  // 采样。没有它,隐式依赖 dstAccess=0 —— finalLayout 转换对后续读取无内存
  // 可见性,Apple TBDR 上 pass 末尾写的 tile(植被)间歇性读到垃圾,
  // 表现为推流画面树冠闪黑块(取证逐层排除后定位于此)。
  SubpassDependency out;
  out.srcSubpass    = 0;
  out.dstSubpass    = VK_SUBPASS_EXTERNAL;
  out.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput)
                    | static_cast<PipelineStageFlags>(PipelineStageFlagBits::LateFragmentTests);
  out.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite)
                    | static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
  out.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  out.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
  rpd.dependencies.push_back(out);

  render_pass_ = device_->createRenderPass(rpd);
  return render_pass_ != nullptr;
}

bool PbrForwardPipeline::createShaders() {
  ShaderCompiler compiler;

  // ---------------------------------------------------------------------------
  // PBR forward shaders
  //   set=0 binding=0  baseColor (sRGB)
  //   set=0 binding=1  normal map (linear, [0,1] → [-1,1] in shader)
  //   set=0 binding=2  metallic-roughness (G=roughness, B=metallic)
  //   set=0 binding=3  emissive (sRGB, 常亮不受昼夜调制)
  //   set=1 binding=0  shadow map (single dir-light, PCF)
  //
  // Push constant block matches `PbrForwardPipeline::PushConstants` exactly.
  // ---------------------------------------------------------------------------
  static const char* kPushBlock =
      "layout(push_constant) uniform PC {\n"
      "  mat4 mvp;\n"
      "  mat4 model;\n"
      "  vec4 base_color;\n"
      "  vec4 mr_params;       // .x=metallic, .y=roughness\n"
      "  vec4 camera_pos_ws;   // .xyz=camera world pos\n"
      "  vec4 light_dir_ws;    // .xyz=light direction toward camera, .w=intensity\n"
      "  mat4 light_vp;        // world->light clip space\n"
      "  vec4 env_params;      // .x=sky scale, .y=IBL diffuse mul, .z=exposure, .w=shadow strength\n"
      "  vec4 env_params2;     // .x=skylight bleed, .y=debug mode, .z=mip-bias, .w=inline_tonemap\n"
      "  vec4 env_params3;     // .rgb = HDR sky tint (1,1,1 = neutral, no HDR loaded)\n"
      "  vec4 emissive_params; // .rgb = emissiveFactor*strength, .w 保留\n"
      "  vec4 geo_params;      // .xyz=up, .w=height bias(窗灯 ENU)\n"
      "} pc;\n";

  // Instanced 变体:per-instance 模型矩阵从实例顶点属性(location 3-6,binding=1,
  // perInstance)读;pc.mvp 复用为共享 view_proj;pc.model 不用。编辑器批量画大量
  // 同 mesh 实例时一次 instanced draw 画完。非 instanced 路径(demo/CRS)layout 不变。
  const std::string vsrc = instanced_
      ? (std::string(
            "layout(location = 0) in vec3 inPosition;\n"
            "layout(location = 1) in vec3 inNormal;\n"
            "layout(location = 2) in vec2 inUv;\n"
            "layout(location = 3) in vec4 inM0;\n"
            "layout(location = 4) in vec4 inM1;\n"
            "layout(location = 5) in vec4 inM2;\n"
            "layout(location = 6) in vec4 inM3;\n"
            "layout(location = 0) out vec3 vWorldPos;\n"
            "layout(location = 1) out vec3 vWorldNormal;\n"
            "layout(location = 2) out vec2 vUv;\n"
            "layout(location = 3) out vec4 vLightSpacePos;\n")
         + kPushBlock +
            "void main() {\n"
            "  mat4 M = mat4(inM0, inM1, inM2, inM3);\n"
            "  vec4 wp = M * vec4(inPosition, 1.0);\n"
            "  vWorldPos    = wp.xyz;\n"
            "  vWorldNormal = mat3(M) * inNormal;\n"
            "  vUv          = inUv;\n"
            "  vLightSpacePos = pc.light_vp * wp;\n"
            "  gl_Position  = pc.mvp * wp;\n"  // pc.mvp = view_proj
            "}\n")
      : (std::string(
            "layout(location = 0) in vec3 inPosition;\n"
            "layout(location = 1) in vec3 inNormal;\n"
            "layout(location = 2) in vec2 inUv;\n"
            "layout(location = 0) out vec3 vWorldPos;\n"
            "layout(location = 1) out vec3 vWorldNormal;\n"
            "layout(location = 2) out vec2 vUv;\n"
            "layout(location = 3) out vec4 vLightSpacePos;\n")
         + kPushBlock +
            "void main() {\n"
            "  vec4 wp = pc.model * vec4(inPosition, 1.0);\n"
            "  vWorldPos    = wp.xyz;\n"
            "  vWorldNormal = mat3(pc.model) * inNormal;\n"
            "  vUv          = inUv;\n"
            "  vLightSpacePos = pc.light_vp * wp;\n"
            "  gl_Position  = pc.mvp * vec4(inPosition, 1.0);\n"
            "}\n");

  // When IBL cube bindings are active (initializeWithIbl with non-null
  // ibl_set_layout), append cube sampler declarations + a textureCube-based
  // evalCubeIBL helper. Default analytic path stays for back-compat demos.
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
      + ibl_decl +
      "layout(location = 0) in vec3 vWorldPos;\n"
      "layout(location = 1) in vec3 vWorldNormal;\n"
      "layout(location = 2) in vec2 vUv;\n"
      "layout(location = 3) in vec4 vLightSpacePos;\n"
      "layout(location = 0) out vec4 outColor;\n"
      + kPushBlock
      + ShadingGLSL::fresnelSchlick()
      + ShadingGLSL::distributionGGX()
      + ShadingGLSL::geometrySmith()
      + ShadingGLSL::tbnFromDerivatives()
      + ShadingGLSL::analyticEnv()
      + ShadingGLSL::envBRDFApprox()
      + ShadingGLSL::evalAnalyticIBL()
      + ShadingGLSL::shadowPCF()
      // set=3 点光源 UBO 声明只在 layout 真有 set=3 时拼入(见 has_lights_)。
      + (has_lights_ ? ShadingGLSL::pointLights() : std::string())
      + ShadingGLSL::acesTonemap() +
      // ACES tonemap lives in the compose pass; main fragment writes raw HDR.
      "void main() {\n"
      "  int mode = int(pc.env_params2.y + 0.5);\n"
      "  // ----- mode 0 RAW: texture × baseColorFactor, no shading -----\n"
      "  if (mode == 0) {\n"
      "    // texture(...) lets GPU pick mip from screen-space derivatives;\n"
      "    // 3rd arg is a BIAS (not absolute LOD), so user [/] keys nudge\n"
      "    // sharper / softer without forcing a single mip.\n"
      "    // baseColorTex is sampled with a sRGB sampler view (hardware\n"
      "    // sRGB→linear) so values here are LINEAR. The offscreen target\n"
      "    // is UNORM and consumers treat it as already-sRGB-encoded for\n"
      "    // display, so we encode linear→sRGB before write — otherwise\n"
      "    // raw albedo looks ~one stop darker than the source PNG.\n"
      "    vec3 raw = texture(baseColorTex, vUv, pc.env_params2.z).rgb\n"
      "             * pc.base_color.rgb;\n"
      "    raw = clamp(raw, vec3(0.0), vec3(1.0));\n"
      "    vec3 srgb = mix(1.055 * pow(raw, vec3(1.0/2.4)) - 0.055,\n"
      "                     12.92 * raw, step(raw, vec3(0.0031308)));\n"
      "    outColor = vec4(srgb, 1.0);\n"
      "    return;\n"
      "  }\n"
      "  // Debug modes write display-ready colors: encode linear->sRGB here\n"
      "  // (same convention as mode 0) so the compose pass MUST NOT tonemap\n"
      "  // them — ACES would wash the diagnostic colors out.\n"
      "  // ----- mode 2 NORMAL viz: world N * 0.5 + 0.5 (backface flipped) -----\n"
      "  if (mode == 2) {\n"
      "    vec3 Nshow = normalize(vWorldNormal);\n"
      "    if (!gl_FrontFacing) Nshow = -Nshow;\n"
      "    vec3 c = Nshow * 0.5 + 0.5;\n"
      "    outColor = vec4(mix(1.055 * pow(c, vec3(1.0/2.4)) - 0.055,\n"
      "                         12.92 * c, step(c, vec3(0.0031308))), 1.0);\n"
      "    return;\n"
      "  }\n"
      "  // ----- mode 3 MIP level viz: black=mip0, red=mip4, yellow=mip8, -----\n"
      "  // ----- white=mip12. Black = magnified (texture under-resolved); -----\n"
      "  // ----- toward white = heavily minified.                         -----\n"
      "  if (mode == 3) {\n"
      "    vec2 dx = dFdx(vUv) * vec2(textureSize(baseColorTex, 0));\n"
      "    vec2 dy = dFdy(vUv) * vec2(textureSize(baseColorTex, 0));\n"
      "    float d2 = max(dot(dx, dx), dot(dy, dy));\n"
      "    float lod = 0.5 * log2(max(d2, 1e-12));\n"
      "    vec3 c = vec3(0.0);\n"
      "    c.r = clamp(lod / 4.0, 0.0, 1.0);\n"
      "    c.g = clamp((lod - 4.0) / 4.0, 0.0, 1.0);\n"
      "    c.b = clamp((lod - 8.0) / 4.0, 0.0, 1.0);\n"
      "    outColor = vec4(mix(1.055 * pow(c, vec3(1.0/2.4)) - 0.055,\n"
      "                         12.92 * c, step(c, vec3(0.0031308))), 1.0);\n"
      "    return;\n"
      "  }\n"
      "  // ----- mode 1 PBR full (or any other value) -----\n"
      "  vec4 baseSample = texture(baseColorTex, vUv, pc.env_params2.z);\n"
      "  vec3 albedo    = baseSample.rgb * pc.base_color.rgb;\n"
      "  vec3 nm        = texture(normalTex,   vUv).xyz * 2.0 - 1.0;\n"
      "  vec3 mr        = texture(mrTex,       vUv).rgb;\n"
      "  float roughness = clamp(mr.g * pc.mr_params.y, 0.04, 1.0);\n"
      "  float metallic  = clamp(mr.b * pc.mr_params.x, 0.0,  1.0);\n"
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
      "  spec = min(spec, vec3(2000.0));  // 低粗糙度掠射:D~1e5、分母~1e-4 →\n"
      "                                   // spec~1e9 溢出 R16F→Inf,后续 bloom 出\n"
      "                                   // 满屏绿 NaN。源头钳住,HDR 不存 Inf。\n"
      "  vec3  kS = F;\n"
      "  vec3  kD = (1.0 - kS) * (1.0 - metallic);\n"
      "  vec3  Lo_unshad = (kD * albedo / 3.14159265 + spec) * pc.light_dir_ws.w * NdotL;\n"
      "  vec3 lsCoords = vLightSpacePos.xyz / vLightSpacePos.w;\n"
      "  lsCoords.xy = lsCoords.xy * 0.5 + 0.5;\n"
      "  float bias = max(0.006 * (1.0 - NdotL), 0.0015);\n"
      "  float shadowRaw = sampleShadowPCF(shadowMap, lsCoords, bias);\n"
      "  // Lerp against fully-lit by shadow_strength so the user can dial it down.\n"
      "  float shadow = mix(1.0, shadowRaw, pc.env_params.w);\n"
      "  vec3 Lo = Lo_unshad * shadow;\n"
      + std::string(has_ibl_ ?
      // ===== Real cubemap IBL =====
      // Diffuse: pre-convolved Lambert irradiance map (cube layer 2).
      // Specular: env spec cube at reflection R, LOD = roughness × maxMip.
      //   roughness 0 → mip 0 (sharp);roughness 1 → top mip (blurry).
      // Karis split-sum BRDF for energy conservation.
      "  vec3 R = reflect(-V, N);\n"
      "  vec3 irradiance = texture(envDiffuseCube, N).rgb;\n"
      "  float specMipMax = log2(float(textureSize(envSpecCube, 0).x));\n"
      "  vec3 prefilt = textureLod(envSpecCube, R, roughness * specMipMax).rgb;\n"
      "  vec2 envBRDF = envBRDFApprox(NdotV, roughness);\n"
      "  vec3 kS_ibl = F0 + (vec3(1.0) - F0)\n"
      "               * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);\n"
      "  vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);\n"
      "  vec3 diffuseIBL  = kD_ibl * irradiance * albedo;\n"
      "  vec3 specularIBL = prefilt * (F0 * envBRDF.x + envBRDF.y);\n"
      "  vec3 ibl = diffuseIBL + specularIBL;\n"
      "  ibl *= pc.env_params.y;\n"
      // Skylight bleed kept in IBL path too — it's an additive ambient
      // floor that prevents dark-albedo surfaces (asphalt, roads) from
      // going pitch black. Cube IBL alone can't lift them because
      // diffuse = albedo × E, and albedo≈0.05 × E ≈ tiny.
      "  vec3 bleed = skylightBleed(N, pc.env_params.x, pc.env_params2.x, metallic);\n"
      :
      // ===== Analytic env (back-compat) =====
      "  vec3 ibl = evalAnalyticIBL(N, V, F0, albedo, roughness, metallic, pc.env_params.x);\n"
      "  ibl *= pc.env_params.y;\n"
      "  ibl *= pc.env_params3.rgb;\n"
      "  vec3 bleed = skylightBleed(N, pc.env_params.x, pc.env_params2.x, metallic);\n"
      "  bleed *= pc.env_params3.rgb;\n") +
      // 点光源累加(UBO set=3):仅在 layout 有 set=3 时调用,否则 shader 不
      // 引用 set=3(非 IBL 路径无该 set,引用即 MoltenVK argument buffer 崩)。
      (has_lights_
           ? std::string("  Lo += evalPointLights(vWorldPos, N, V, albedo, "
                         "metallic, roughness, F0);\n")
           : std::string()) +
      "  vec3 hdr = (Lo + ibl + bleed) * pc.env_params.z;\n"
      "  // emissive_params.w = 夜窗门控量:材质自带 emissive 贴图(美术画的窗灯)\n"
      "  // 且昼夜循环开 → w=1:emissive × night_factor(夜亮昼灭),程序化窗灯\n"
      "  // 让位(贴图权威);否则 w=0:emissive 常亮、程序化窗灯兜底照旧。\n"
      "  float egate = mix(1.0, pc.env_params3.w, pc.emissive_params.w);\n"
      "  hdr += windowEmissive(vWorldPos, N_geom, pc.env_params3.w, albedo,\n"
      "                        pc.geo_params.xyz, pc.geo_params.w)\n"
      "         * (1.0 - pc.emissive_params.w);\n"
      "  hdr += texture(emissiveTex, vUv).rgb * pc.emissive_params.rgb * egate;\n"
      "  // Inline ACES when the caller writes straight into an SRGB framebuffer\n"
      "  // (no HDR compose pass downstream). Embedded mode in earth_demo sets this.\n"
      "  if (pc.env_params2.w > 0.5) hdr = acesFilmic(hdr);\n"
      "  outColor = vec4(hdr, 1.0);\n"
      "}\n";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  if (!vr.success) { std::cerr << "pbr vs fail: " << vr.errorMessage << "\n"; return false; }
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!fr.success) { std::cerr << "pbr fs fail: " << fr.errorMessage << "\n"; return false; }

  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vert_ = device_->createShader(vd);
  frag_ = device_->createShader(fd);
  return vert_ && frag_;
}

bool PbrForwardPipeline::createPipeline(RHIDescriptorSetLayoutPtr material_set_layout,
                                          RHIDescriptorSetLayoutPtr shadow_set_layout) {
  PipelineLayoutDesc layoutDesc;
  layoutDesc.setLayouts.push_back(material_set_layout);  // set 0: material
  layoutDesc.setLayouts.push_back(shadow_set_layout);    // set 1: shadow map
  PushConstantRange pc;
  pc.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pc.offset = 0;
  pc.size   = sizeof(PushConstants);
  layoutDesc.pushConstantRanges.push_back(pc);
  pipeline_layout_ = device_->createPipelineLayout(layoutDesc);
  if (!pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders                       = {vert_, frag_};
  pd.vertexInput.bindings          = {MeshVertex::binding()};
  pd.vertexInput.attributes        = MeshVertex::attributes();
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  // Disable backface culling: city / architectural glTF assets are notorious
  // for inconsistent triangle winding (mix of CW and CCW per face depending
  // on the exporter). With CullMode::Back, "wrong-winding" faces get culled
  // and the model appears full of holes. Drawing both sides costs a few % GPU
  // but makes the model look correct.
  pd.rasterization.cullMode        = CullMode::None;
  // See initializeWithIbl: CCW front so gl_FrontFacing matches glTF winding.
  pd.rasterization.frontCounterClockwise = true;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = render_pass_;
  pd.pipelineLayout                = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

}  // namespace hongtu::renderer
