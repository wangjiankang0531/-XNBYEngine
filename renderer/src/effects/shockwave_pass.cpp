#include <hongtu/renderer/effects/shockwave_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool ShockwavePass::initialize(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;
  return createPipeline(render_pass);
}

bool ShockwavePass::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Fullscreen-tri vertex shader,跟 VolumetricPass 同模式。
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

  // Fragment shader:
  //   1. inv-VP unproject NDC near+far → world ray
  //   2. 求 ray 到 shockwave center 的最近点距离 d
  //   3. |d - radius| < thickness → 输出 brigh ring with soft falloff
  //   4. chroma_offset > 0 时 R/G/B 三个通道用略微不同的 radius 测距,
  //      产生 chromatic aberration(经典爆炸前沿色散)
  const char* fsrc = R"glsl(
    layout(push_constant) uniform PC {
      mat4 inv_view_proj;
      vec4 center_radius;     // .xyz center, .w radius
      vec4 color_intensity;   // .rgb color, .a intensity
      vec4 thickness_chroma;  // .x thickness, .y chroma_offset
    } pc;

    layout(location = 0) in  vec2 vNDC;
    layout(location = 0) out vec4 outColor;

    // Returns brightness (0..1) for a given radius value.
    float ringMask(float d, float radius, float thickness) {
      float diff = abs(d - radius);
      if (diff >= thickness) return 0.0;
      float t = 1.0 - diff / thickness;
      return t * t;
    }

    void main() {
      vec4 near_h = pc.inv_view_proj * vec4(vNDC, 0.0, 1.0);
      vec4 far_h  = pc.inv_view_proj * vec4(vNDC, 1.0, 1.0);
      vec3 ro = near_h.xyz / near_h.w;
      vec3 rd = normalize(far_h.xyz / far_h.w - ro);

      vec3  c   = pc.center_radius.xyz;
      float r   = pc.center_radius.w;
      float th  = pc.thickness_chroma.x;
      float chr = pc.thickness_chroma.y;

      vec3 oc = c - ro;
      float t_close = dot(oc, rd);
      vec3 closest  = ro + rd * t_close;
      float d = length(closest - c);

      // Only show shockwave for points in front of camera.
      if (t_close < 0.0) discard;

      vec3 col   = pc.color_intensity.rgb * pc.color_intensity.a;
      float aMono = ringMask(d, r, th);
      if (chr <= 0.001) {
        if (aMono < 0.001) discard;
        outColor = vec4(col * aMono, aMono);
        return;
      }

      // Chromatic aberration: 3 ring masks at r-chr / r / r+chr → RGB channels.
      float aR = ringMask(d, r - chr, th);
      float aG = aMono;
      float aB = ringMask(d, r + chr, th);
      float aMax = max(max(aR, aG), aB);
      if (aMax < 0.001) discard;
      vec3 rgb = vec3(col.r * aR, col.g * aG, col.b * aB);
      outColor = vec4(rgb, aMax);
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("ShockwavePass: shader fail vs='{}' fs='{}'",
                  vr.errorMessage, fr.errorMessage);
    return false;
  }
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc pll;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0; pcr.size = sizeof(PushConstants);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  // Additive blend — shockwave overlays the scene without darkening anything.
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::One;
  blend.dstColorBlendFactor = BlendFactor::One;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::One;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.depthStencil.depthTestEnable  = false;
  pd.depthStencil.depthWriteEnable = false;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass     = render_pass;
  pd.pipelineLayout = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void ShockwavePass::record(const RHICommandBufferPtr& cmd, const Params& p) {
  if (!pipeline_) return;

  PushConstants pc{};
  pc.inv_view_proj    = glm::inverse(p.view_proj);
  pc.center_radius    = glm::vec4(p.center_rtc, p.radius_m);
  pc.color_intensity  = glm::vec4(p.color, p.intensity);
  pc.thickness_chroma = glm::vec4(p.thickness_m, p.chroma_offset, 0.0f, 0.0f);

  cmd->bindPipeline(pipeline_);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->draw(3u, 1u, 0u, 0u);
}

}  // namespace hongtu::renderer
