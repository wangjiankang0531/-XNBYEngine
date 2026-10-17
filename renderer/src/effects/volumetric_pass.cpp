#include <hongtu/renderer/effects/volumetric_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool VolumetricPass::initialize(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;

  // 3D 密度采样器 — linear interp(让 mid-cell 平滑)+ clamp-to-edge
  // (避免 AABB 外侧出现 wrap 的浓度尾巴)。
  SamplerDesc sd;
  sd.magFilter    = SamplerFilter::Linear;
  sd.minFilter    = SamplerFilter::Linear;
  sd.mipmapMode   = SamplerMipmapMode::Nearest;
  sd.addressModeU = SamplerAddressMode::ClampToEdge;
  sd.addressModeV = SamplerAddressMode::ClampToEdge;
  sd.addressModeW = SamplerAddressMode::ClampToEdge;
  density_sampler_ = device_->createSampler(sd);
  if (!density_sampler_) return false;

  return createPipeline(render_pass);
}

bool VolumetricPass::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Fullscreen-triangle vertex shader — 标准 3-vertex 全屏覆盖。
  // 用 gl_VertexIndex 生成 NDC 角点(-1..1),片元 stage 自己从 NDC
  // 算 world ray(直接走 inv_view_proj * vec4(ndc, z, 1))。
  const char* vsrc = R"glsl(
    layout(location = 0) out vec2 vNDC;
    void main() {
      vec2 ndc = vec2(
          (gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
          (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0);
      vNDC        = ndc;
      gl_Position = vec4(ndc, 0.0, 1.0);
    }
  )glsl";

  // 片元 stage:
  //   1. 用 inv_view_proj 把 NDC(near=0)+ NDC(far=1)反投影回 world,
  //      求 world-space ray(origin = camera_pos,direction = unit far-near)
  //   2. Slab 法求 ray ∩ AABB,得到 [t0, t1] 区间(t1 < t0 或 t1 < 0 直接 discard)
  //   3. 沿 ray march N 步,每步采样 3D 纹理,Beer-Lambert 累计透光率
  //   4. 输出 emissive color × accumulated alpha
  const char* fsrc = R"glsl(
    layout(set = 0, binding = 0) uniform sampler3D densityTex;

    layout(push_constant) uniform PC {
      mat4 inv_view_proj;
      vec4 camera_pos_rtc;       // .xyz
      vec4 aabb_min_density;     // .xyz min, .w density_scale
      vec4 aabb_max_steps;       // .xyz max, .w num_steps
      vec4 color_sigma;          // .rgb emissive color, .a sigma
      vec4 light_dir_intensity;  // .xyz dir → light, .w intensity (0=off)
      vec4 light_color_hg;       // .rgb light color, .a HG anisotropy g
    } pc;

    layout(location = 0) in  vec2 vNDC;
    layout(location = 0) out vec4 outColor;

    vec2 rayAABB(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
      vec3 inv_rd = 1.0 / rd;
      vec3 t0 = (mn - ro) * inv_rd;
      vec3 t1 = (mx - ro) * inv_rd;
      vec3 tmin = min(t0, t1);
      vec3 tmax = max(t0, t1);
      float t_enter = max(max(tmin.x, tmin.y), tmin.z);
      float t_exit  = min(min(tmax.x, tmax.y), tmax.z);
      return vec2(t_enter, t_exit);
    }

    // Henyey-Greenstein 单参数散射相位:
    //   P(θ) = (1 - g²) / [4π·(1 + g² - 2g·cosθ)^(3/2)]
    // g ∈ (-1, 1):g > 0 前向(雾透过太阳),g < 0 后向(夜间雾灯),0 = 各向同性。
    float phaseHG(float cos_theta, float g) {
      float g2 = g * g;
      float denom = 1.0 + g2 - 2.0 * g * cos_theta;
      return (1.0 - g2) / (12.566370614 * pow(max(denom, 1e-4), 1.5));
    }

    void main() {
      // Unproject NDC near(0)和 far(1)to world space.
      vec4 near_h = pc.inv_view_proj * vec4(vNDC, 0.0, 1.0);
      vec4 far_h  = pc.inv_view_proj * vec4(vNDC, 1.0, 1.0);
      vec3 near_p = near_h.xyz / near_h.w;
      vec3 far_p  = far_h.xyz  / far_h.w;
      vec3 ro     = near_p;
      vec3 rd     = normalize(far_p - near_p);

      vec3 aabb_min     = pc.aabb_min_density.xyz;
      vec3 aabb_max     = pc.aabb_max_steps.xyz;
      float density_scl = pc.aabb_min_density.w;
      int   num_steps   = max(8, int(pc.aabb_max_steps.w));
      vec3  base_color  = pc.color_sigma.rgb;
      float sigma       = pc.color_sigma.a;

      // 散射参数(intensity == 0 时跳过)
      vec3  light_dir   = pc.light_dir_intensity.xyz;
      float light_int   = pc.light_dir_intensity.w;
      vec3  light_col   = pc.light_color_hg.rgb;
      float hg_g        = pc.light_color_hg.a;

      // 一次性算 cos θ + HG phase(整个体积同一个光源方向,
      // 不在 march loop 里重复算)
      float cos_theta = dot(rd, -light_dir);   // ray 与"光来向"夹角
      float phase     = (light_int > 0.001) ? phaseHG(cos_theta, hg_g) : 0.0;

      vec2 t_range = rayAABB(ro, rd, aabb_min, aabb_max);
      if (t_range.x > t_range.y || t_range.y <= 0.0) discard;
      float t_start = max(t_range.x, 0.0);
      float t_end   = t_range.y;

      float dt = (t_end - t_start) / float(num_steps);
      if (dt <= 0.0) discard;

      float t = t_start;
      vec3  accum_color = vec3(0.0);
      float trans = 1.0;
      vec3  inv_ext = 1.0 / (aabb_max - aabb_min);
      for (int i = 0; i < num_steps; ++i) {
        vec3 p   = ro + rd * t;
        vec3 uvw = (p - aabb_min) * inv_ext;       // 0..1 in volume
        float d  = texture(densityTex, uvw).r * density_scl;
        float alpha = 1.0 - exp(-d * sigma * dt);

        // emissive(各向同性发光)+ in-scattering(光来向 × HG phase)
        vec3 sample_radiance = base_color
                             + light_col * light_int * phase * d;
        accum_color += trans * alpha * sample_radiance;
        trans       *= (1.0 - alpha);
        if (trans < 0.01) break;
        t += dt;
      }
      outColor = vec4(accum_color, 1.0 - trans);
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("VolumetricPass: shader fail vs='{}' fs='{}'",
                  vr.errorMessage, fr.errorMessage);
    return false;
  }
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  // Descriptor set layout: set=0 binding=0 → sampler3D
  DescriptorSetLayoutDesc dl;
  DescriptorSetLayoutBinding lb;
  lb.binding         = 0u;
  lb.descriptorCount = 1u;
  lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  lb.descriptorType  = DescriptorType::CombinedImageSampler;
  dl.bindings.push_back(lb);
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  if (!desc_layout_) return false;

  PipelineLayoutDesc pll;
  pll.setLayouts.push_back(desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0; pcr.size = sizeof(PushConstants);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  // Alpha-blend on,depth-test off(volumetric 不写深度)
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.depthStencil.depthTestEnable  = false;
  pd.depthStencil.depthWriteEnable = false;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

VolumetricPass::VolumeTexture
VolumetricPass::uploadDensity(const DensityGrid& g) {
  VolumeTexture vt;
  vt.dims = {g.width, g.height, g.depth};
  if (g.values.empty()) return vt;

  TextureDesc td;
  td.type        = TextureType::Texture3D;
  td.extent      = {g.width, g.height, g.depth};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R8_UNORM;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  vt.texture = device_->createTexture(td);
  if (!vt.texture) return vt;

  // values are float 0..1 — quantize to u8.
  std::vector<uint8_t> bytes(g.values.size());
  for (size_t i = 0; i < g.values.size(); ++i) {
    float v = std::clamp(g.values[i], 0.0f, 1.0f);
    bytes[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
  }
  RHIDevice::MipData mip;
  mip.width    = g.width;
  mip.height   = g.height;
  mip.depth    = g.depth;
  mip.data     = bytes.data();
  mip.dataSize = bytes.size();
  device_->uploadTextureData(vt.texture, {mip}, 0u);

  vt.view = vt.texture->createView();
  return vt;
}

void VolumetricPass::record(const RHICommandBufferPtr& cmd, const Params& p,
                            const RHITextureViewPtr& density_view) {
  if (!pipeline_ || !density_view) return;

  // 每次绘制都分配一个 descriptor set 绑定当前 density texture。
  // (与 BillboardRenderer::setAtlas 同模式 — 简单,fragmented allocate
  //  但 disaster_demo 一帧至多 1-2 个 volume,无性能问题。)
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding     = 0u;
  db.type        = DescriptorType::CombinedImageSampler;
  db.textureView = density_view;
  db.sampler     = density_sampler_;
  dsd.bindings.push_back(db);
  auto desc_set = device_->createDescriptorSet(desc_layout_, dsd);
  if (!desc_set) return;

  PushConstants pc{};
  pc.inv_view_proj         = glm::inverse(p.view_proj);
  pc.camera_pos_rtc        = glm::vec4(p.camera_pos_rtc, 0.0f);
  pc.aabb_min_density      = glm::vec4(p.aabb_min, p.density_scale);
  pc.aabb_max_steps        = glm::vec4(p.aabb_max, static_cast<float>(p.num_steps));
  pc.color_sigma           = glm::vec4(p.color, p.sigma);
  // 散射:确保 light_dir 单位向量,intensity=0 时 shader 跳过
  glm::vec3 ld = p.light_dir;
  float dlen = glm::length(ld);
  if (dlen > 1e-6f) ld = ld * (1.0f / dlen);
  pc.light_dir_intensity   = glm::vec4(ld, p.light_intensity);
  pc.light_color_hg        = glm::vec4(p.light_color, p.hg_g);

  cmd->bindPipeline(pipeline_);
  cmd->bindDescriptorSet(0u, desc_set, pipeline_layout_);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->draw(3u, 1u, 0u, 0u);
}

}  // namespace hongtu::renderer
