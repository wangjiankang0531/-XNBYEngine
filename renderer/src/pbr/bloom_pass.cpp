#include <hongtu/renderer/pbr/bloom_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

namespace {

struct PushBlock {
  // .x threshold (threshold pass), upsample radius (upsample pass)
  // .y texel_size_x of *source* mip (so the shader can scale offsets)
  // .z texel_size_y of *source* mip
  // .w unused
  float values[4];
};
static_assert(sizeof(PushBlock) == 16, "BloomPass push block size mismatch");

}  // namespace

bool BloomPass::initialize(RHIDevice* device,
                            RHITextureViewPtr hdr_view,
                            uint32_t hdr_w,
                            uint32_t hdr_h) {
  device_   = device;
  hdr_view_ = hdr_view;
  hdr_w_    = hdr_w;
  hdr_h_    = hdr_h;
  return createMipChain(hdr_w, hdr_h) && createPipelines();
}

RHITextureViewPtr BloomPass::getBloomView() const {
  return mips_.empty() ? nullptr : mips_[0].view;
}

bool BloomPass::createMipChain(uint32_t hdr_w, uint32_t hdr_h) {
  mips_.clear();
  mips_.resize(kMipCount);
  uint32_t w = std::max(hdr_w / 2u, 1u);
  uint32_t h = std::max(hdr_h / 2u, 1u);
  for (uint32_t i = 0; i < kMipCount; ++i) {
    MipLevel& m = mips_[i];
    m.w = w;
    m.h = h;
    TextureDesc td;
    td.type        = TextureType::Texture2D;
    td.extent      = {w, h, 1u};
    td.mipLevels   = 1u;
    td.arrayLayers = 1u;
    td.format      = Format::R16G16B16A16_SFLOAT;
    td.usage       = TextureUsageFlagBits::ColorAttachment
                   | TextureUsageFlagBits::Sampled;
    td.sampleCount = 1u;
    m.tex = device_->createTexture(td);
    if (!m.tex) return false;
    m.view = m.tex->createView();
    if (!m.view) return false;
    w = std::max(w / 2u, 1u);
    h = std::max(h / 2u, 1u);
  }
  return true;
}

bool BloomPass::createPipelines() {
  // ---- shared single-color-attachment render pass --------------------------
  // Always loadOpLoad + storeOpStore — downsample / threshold can also use
  // it (they overwrite the entire framebuffer so loadOp doesn't matter
  // visually). Upsample relies on this for additive blending.
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format        = Format::R16G16B16A16_SFLOAT;
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;  // next pass samples
  ca.loadOpClear   = false;   // upsample needs Load; threshold/downsample
                              // overwrite fully so initial garbage is OK
  ca.storeOpStore  = true;
  rpd.attachments.push_back(ca);

  SubpassDescription sp;
  AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  rpd.subpasses.push_back(sp);

  SubpassDependency dep;
  dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass    = 0;
  dep.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  dep.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
  rpd.dependencies.push_back(dep);
  // 出口依赖:每级 mip 渲完立刻被下一级/compose 采样(同 HDR pass 注释)。
  SubpassDependency out;
  out.srcSubpass    = 0;
  out.dstSubpass    = VK_SUBPASS_EXTERNAL;
  out.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
  out.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
  out.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  out.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
  rpd.dependencies.push_back(out);
  render_pass_ = device_->createRenderPass(rpd);
  if (!render_pass_) return false;

  // ---- framebuffer per mip -------------------------------------------------
  for (auto& m : mips_) {
    m.fb = device_->createFramebuffer(render_pass_, {m.view}, {m.w, m.h}, 1);
    if (!m.fb) return false;
  }

  // ---- shared sampler + descriptor set layout (binding=0 sampler2D) --------
  SamplerDesc sd;
  sd.magFilter        = SamplerFilter::Linear;
  sd.minFilter        = SamplerFilter::Linear;
  sd.addressModeU     = SamplerAddressMode::ClampToEdge;
  sd.addressModeV     = SamplerAddressMode::ClampToEdge;
  sd.addressModeW     = SamplerAddressMode::ClampToEdge;
  sd.anisotropyEnable = false;
  sampler_ = device_->createSampler(sd);
  if (!sampler_) return false;

  DescriptorSetLayoutDesc dl;
  DescriptorSetLayoutBinding lb;
  lb.binding         = 0u;
  lb.descriptorCount = 1u;
  lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  lb.descriptorType  = DescriptorType::CombinedImageSampler;
  dl.bindings.push_back(lb);
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  if (!desc_layout_) return false;

  // descriptor for HDR view (threshold pass input)
  auto makeDescSet = [&](RHITextureViewPtr v) {
    DescriptorSetDesc dsd;
    DescriptorBinding db;
    db.binding     = 0u;
    db.type        = DescriptorType::CombinedImageSampler;
    db.textureView = v;
    db.sampler     = sampler_;
    dsd.bindings.push_back(db);
    return device_->createDescriptorSet(desc_layout_, dsd);
  };
  hdr_desc_ = makeDescSet(hdr_view_);
  if (!hdr_desc_) return false;
  for (auto& m : mips_) {
    m.sampler_desc = makeDescSet(m.view);
    if (!m.sampler_desc) return false;
  }

  // ---- pipeline layout (single push range) ---------------------------------
  PipelineLayoutDesc pll;
  pll.setLayouts.push_back(desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size   = sizeof(PushBlock);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  // ---- shaders -------------------------------------------------------------
  ShaderCompiler compiler;

  const char* vsrc = R"glsl(
    layout(location = 0) out vec2 vUv;
    void main() {
      vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
      gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
    }
  )glsl";

  // Threshold pass: 4-tap box filter at half-res, then soft threshold.
  // Output = avg_color × max(0, max_channel - threshold) / max_channel.
  const char* fsrc_threshold = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D srcTex;
    layout(push_constant) uniform PC { vec4 params; } pc;
    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;
    // Firefly clamp:HDR 里低粗糙度掠射高光的镜面项会溢出 R16F → Inf。
    // 不防的话 contrib=(Inf-thr)/Inf=NaN,模糊后扩散成满屏绿/品红彩噪
    // (城市玻璃幕墙缩进看实测)。把每个采样钳到有限上限,既保留强 bloom
    // 又杜绝 Inf/NaN(min(Inf,X)=X)。NaN 源用 ?: 兜底归零。
    // 防弹 firefly clamp:NaN/Inf 与任何数比较恒为 false(IEEE),故
    // `(x < 2000) ? x : 2000` 把 NaN、Inf、超大值统一压到 2000;mix(bvec)
    // 版在 MoltenVK/SPIRV-Cross 上不可靠(实测没钳住,bloom 出绿 NaN)。
    float ffc(float x) { return (x < 2000.0) ? x : 2000.0; }
    vec3 ffclamp(vec3 c) {
      return max(vec3(ffc(c.r), ffc(c.g), ffc(c.b)), vec3(0.0));
    }
    void main() {
      vec2 t = pc.params.yz;          // texel size of source
      vec3 a = ffclamp(texture(srcTex, vUv + vec2(-1.0, -1.0) * t).rgb);
      vec3 b = ffclamp(texture(srcTex, vUv + vec2( 1.0, -1.0) * t).rgb);
      vec3 c = ffclamp(texture(srcTex, vUv + vec2(-1.0,  1.0) * t).rgb);
      vec3 d = ffclamp(texture(srcTex, vUv + vec2( 1.0,  1.0) * t).rgb);
      vec3 col = 0.25 * (a + b + c + d);
      float brightness = max(col.r, max(col.g, col.b));
      float threshold  = pc.params.x;
      float contrib    = max(brightness - threshold, 0.0) / max(brightness, 1e-4);
      outColor = vec4(col * contrib, 1.0);
    }
  )glsl";

  // Downsample: same 4-tap box filter, no threshold.
  const char* fsrc_downsample = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D srcTex;
    layout(push_constant) uniform PC { vec4 params; } pc;
    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;
    void main() {
      vec2 t = pc.params.yz;
      vec3 a = texture(srcTex, vUv + vec2(-1.0, -1.0) * t).rgb;
      vec3 b = texture(srcTex, vUv + vec2( 1.0, -1.0) * t).rgb;
      vec3 c = texture(srcTex, vUv + vec2(-1.0,  1.0) * t).rgb;
      vec3 d = texture(srcTex, vUv + vec2( 1.0,  1.0) * t).rgb;
      outColor = vec4(0.25 * (a + b + c + d), 1.0);
    }
  )glsl";

  // Upsample: 3×3 tent (1-2-1) blur — additively blended into target
  // (blend state One/One). Radius scales the tap offset.
  const char* fsrc_upsample = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D srcTex;
    layout(push_constant) uniform PC { vec4 params; } pc;
    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;
    void main() {
      vec2 t = pc.params.yz * pc.params.x;   // texel × radius
      vec3 col = vec3(0.0);
      col += texture(srcTex, vUv + vec2(-1.0, -1.0) * t).rgb * 1.0;
      col += texture(srcTex, vUv + vec2( 0.0, -1.0) * t).rgb * 2.0;
      col += texture(srcTex, vUv + vec2( 1.0, -1.0) * t).rgb * 1.0;
      col += texture(srcTex, vUv + vec2(-1.0,  0.0) * t).rgb * 2.0;
      col += texture(srcTex, vUv + vec2( 0.0,  0.0) * t).rgb * 4.0;
      col += texture(srcTex, vUv + vec2( 1.0,  0.0) * t).rgb * 2.0;
      col += texture(srcTex, vUv + vec2(-1.0,  1.0) * t).rgb * 1.0;
      col += texture(srcTex, vUv + vec2( 0.0,  1.0) * t).rgb * 2.0;
      col += texture(srcTex, vUv + vec2( 1.0,  1.0) * t).rgb * 1.0;
      outColor = vec4(col * (1.0 / 16.0), 1.0);
    }
  )glsl";

  auto compile = [&](const char* src, const char* stage) {
    return compiler.compileGLSLToSPIRV(src, "main", stage);
  };
  auto vr = compile(vsrc,              "vertex");
  auto tr = compile(fsrc_threshold,    "fragment");
  auto dr = compile(fsrc_downsample,   "fragment");
  auto ur = compile(fsrc_upsample,     "fragment");
  if (!vr.success || !tr.success || !dr.success || !ur.success) return false;

  auto mk = [&](const auto& res, ShaderStageFlagBits stage) {
    ShaderDesc sd2; sd2.stage = stage; sd2.spirvCode = res.spirvCode;
    return device_->createShader(sd2);
  };
  vs_fullscreen_ = mk(vr, ShaderStageFlagBits::Vertex);
  fs_threshold_  = mk(tr, ShaderStageFlagBits::Fragment);
  fs_downsample_ = mk(dr, ShaderStageFlagBits::Fragment);
  fs_upsample_   = mk(ur, ShaderStageFlagBits::Fragment);
  if (!vs_fullscreen_ || !fs_threshold_ || !fs_downsample_ || !fs_upsample_)
    return false;

  // ---- three pipelines (threshold + downsample = overwrite; upsample = additive)
  auto makePipeline = [&](RHIShaderPtr fs, bool additive) {
    GraphicsPipelineDesc pd;
    pd.shaders                       = {vs_fullscreen_, fs};
    pd.primitiveTopology             = PrimitiveTopology::TriangleList;
    pd.rasterization.cullMode        = CullMode::None;
    pd.rasterization.fillMode        = FillMode::Solid;
    pd.depthStencil.depthTestEnable  = false;
    pd.depthStencil.depthWriteEnable = false;
    pd.renderPass                    = render_pass_;
    pd.pipelineLayout                = pipeline_layout_;
    ColorBlendAttachmentState ba;
    if (additive) {
      ba.blendEnable         = true;
      ba.srcColorBlendFactor = BlendFactor::One;
      ba.dstColorBlendFactor = BlendFactor::One;
      ba.colorBlendOp        = BlendOp::Add;
      ba.srcAlphaBlendFactor = BlendFactor::One;
      ba.dstAlphaBlendFactor = BlendFactor::One;
      ba.alphaBlendOp        = BlendOp::Add;
    }
    pd.colorBlendAttachments.push_back(ba);
    return device_->createGraphicsPipeline(pd);
  };
  pipeline_threshold_  = makePipeline(fs_threshold_,  /*additive=*/false);
  pipeline_downsample_ = makePipeline(fs_downsample_, /*additive=*/false);
  pipeline_upsample_   = makePipeline(fs_upsample_,   /*additive=*/true);
  return pipeline_threshold_ && pipeline_downsample_ && pipeline_upsample_;
}

void BloomPass::record(const RHICommandBufferPtr& cmd, const Params& params) {
  auto begin = [&](const MipLevel& dst, RHIPipelinePtr pip,
                    const RHIDescriptorSetPtr& sample_desc,
                    const PushBlock& pc) {
    Rect2D area;
    area.offset = {0, 0};
    area.extent = {dst.w, dst.h};
    std::vector<std::array<float, 4>> clears = {{0.0f, 0.0f, 0.0f, 1.0f}};
    cmd->beginRenderPass(render_pass_, dst.fb, area, clears);
    cmd->bindPipeline(pip);
    Viewport vp;
    vp.x = 0; vp.y = 0;
    vp.width  = static_cast<float>(dst.w);
    vp.height = static_cast<float>(dst.h);
    vp.minDepth = 0; vp.maxDepth = 1;
    cmd->setViewport(vp);
    cmd->setScissor(area);
    cmd->bindDescriptorSet(0u, sample_desc, pipeline_layout_);
    cmd->pushConstants(pipeline_layout_,
        static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
        0, sizeof(pc), &pc);
    cmd->draw(3u);
    cmd->endRenderPass();
  };

  // Threshold pass: HDR (full res) → mip 0 (half res). Source texel size is HDR's.
  {
    PushBlock pc;
    pc.values[0] = params.threshold;
    pc.values[1] = 1.0f / static_cast<float>(hdr_w_);
    pc.values[2] = 1.0f / static_cast<float>(hdr_h_);
    pc.values[3] = 0.0f;
    begin(mips_[0], pipeline_threshold_, hdr_desc_, pc);
  }

  // Downsample chain: mip i → mip i+1. Source texel size is mip i's.
  for (uint32_t i = 0; i + 1 < kMipCount; ++i) {
    PushBlock pc;
    pc.values[0] = 0.0f;  // unused
    pc.values[1] = 1.0f / static_cast<float>(mips_[i].w);
    pc.values[2] = 1.0f / static_cast<float>(mips_[i].h);
    pc.values[3] = 0.0f;
    begin(mips_[i + 1], pipeline_downsample_, mips_[i].sampler_desc, pc);
  }

  // Upsample chain: mip i → mip i-1 (additive blend). Tent filter; radius
  // is the user-tuned blur strength.
  for (uint32_t i = kMipCount - 1; i > 0; --i) {
    PushBlock pc;
    pc.values[0] = params.radius;
    pc.values[1] = 1.0f / static_cast<float>(mips_[i].w);
    pc.values[2] = 1.0f / static_cast<float>(mips_[i].h);
    pc.values[3] = 0.0f;
    begin(mips_[i - 1], pipeline_upsample_, mips_[i].sampler_desc, pc);
  }
}

}  // namespace hongtu::renderer
