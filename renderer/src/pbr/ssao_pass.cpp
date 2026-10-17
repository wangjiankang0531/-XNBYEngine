#include <hongtu/renderer/pbr/ssao_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool SsaoPass::initialize(RHIDevice* device, RHITextureViewPtr depth_view) {
  device_     = device;
  depth_view_ = depth_view;

  SamplerDesc sd;
  sd.magFilter        = SamplerFilter::Nearest;   // depth is not bilinear-safe
  sd.minFilter        = SamplerFilter::Nearest;
  sd.addressModeU     = SamplerAddressMode::ClampToEdge;
  sd.addressModeV     = SamplerAddressMode::ClampToEdge;
  sd.addressModeW     = SamplerAddressMode::ClampToEdge;
  sd.anisotropyEnable = false;
  depth_sampler_ = device_->createSampler(sd);
  if (!depth_sampler_) return false;

  if (!createTargetAndFramebuffer()) return false;
  return createPipeline();
}

bool SsaoPass::initialize(RHIDevice* device, RHITextureViewPtr depth_view,
                            uint32_t width, uint32_t height) {
  explicit_w_ = width;
  explicit_h_ = height;
  return initialize(device, depth_view);
}

bool SsaoPass::resize(RHITextureViewPtr depth_view, uint32_t width, uint32_t height) {
  explicit_w_ = width;
  explicit_h_ = height;
  return resize(depth_view);
}

bool SsaoPass::resize(RHITextureViewPtr depth_view) {
  depth_view_ = depth_view;
  framebuffer_.reset();
  ao_view_.reset();
  ao_tex_.reset();
  if (!createTargetAndFramebuffer()) return false;

  // Refresh the descriptor set so it points at the new depth view.
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding     = 0u;
  db.type        = DescriptorType::CombinedImageSampler;
  db.textureView = depth_view_;
  db.sampler     = depth_sampler_;
  dsd.bindings.push_back(db);
  desc_set_ = device_->createDescriptorSet(desc_layout_, dsd);
  return desc_set_ != nullptr;
}

bool SsaoPass::createTargetAndFramebuffer() {
  Extent2D ext;
  if (explicit_w_ > 0 && explicit_h_ > 0) {
    ext.width  = explicit_w_;
    ext.height = explicit_h_;
  } else {
    ext = device_->getSwapchainExtent();
  }

  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {ext.width, ext.height, 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R8_UNORM;
  td.usage       = TextureUsageFlagBits::ColorAttachment
                 | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  ao_tex_ = device_->createTexture(td);
  if (!ao_tex_) return false;
  ao_view_ = ao_tex_->createView();
  if (!ao_view_) return false;

  // Build (or rebuild) the render pass + framebuffer.
  if (!render_pass_) {
    RenderPassDesc rpd;
    AttachmentDescription ca;
    ca.format        = Format::R8_UNORM;
    ca.sampleCount   = 1;
    ca.initialLayout = ImageLayout::Undefined;
    ca.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;
    ca.loadOpClear   = true;
    ca.storeOpStore  = true;
    rpd.attachments.push_back(ca);

    SubpassDescription sp;
    AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
    sp.colorAttachments.push_back(cr);
    rpd.subpasses.push_back(sp);

    SubpassDependency dep;
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
    dep.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
    rpd.dependencies.push_back(dep);
    // 出口依赖:AO 随后被 compose 片元采样,缺省隐式依赖无内存可见性
    // (与 HDR pass 同一类 tile 竞争,见 pbr_forward_pipeline.cpp)。
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
  }

  framebuffer_ = device_->createFramebuffer(render_pass_, {ao_view_},
                                              {ext.width, ext.height}, 1);
  return framebuffer_ != nullptr;
}

bool SsaoPass::createPipeline() {
  ShaderCompiler compiler;

  // Fullscreen triangle — same trick the compose pass uses.
  const char* vsrc = R"glsl(
    layout(location = 0) out vec2 vUv;
    void main() {
      vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
      gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
    }
  )glsl";

  // SSAO fragment.
  //   - Reconstructs view-space position from sampled depth + inv_proj.
  //   - Reconstructs view-space normal from screen-space dFdx/dFdy of the
  //     view-space position (cross product), which avoids needing a normal
  //     G-buffer.
  //   - 12-tap spiral in a sphere of `radius` view-space meters around the
  //     current pixel, rotated per-pixel by a hash noise to break banding.
  //   - Output = (1 - average occlusion) * intensity, clamped to [0,1].
  const std::string fsrc = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D depthTex;
    layout(push_constant) uniform PC {
      mat4 proj;
      mat4 inv_proj;
      vec4 params;   // .x radius, .y intensity, .z bias
    } pc;
    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outAo;

    vec3 reconstructViewPos(vec2 uv) {
      float d = texture(depthTex, uv).r;
      // NDC: x,y in [-1,1], z in [0,1] (Vulkan-style).
      vec4 clip = vec4(uv * 2.0 - 1.0, d, 1.0);
      vec4 view = pc.inv_proj * clip;
      return view.xyz / view.w;
    }

    float hash12(vec2 p) {
      vec3 p3 = fract(vec3(p.xyx) * 0.1031);
      p3 += dot(p3, p3.yzx + 33.33);
      return fract((p3.x + p3.y) * p3.z);
    }

    void main() {
      // 背景/天空(depth≈far)直接判无遮挡。旧版用重建出的 view.z < -1e7
      // 判远点,但编辑器相机 far 仅 200km(view.z≈-2e5),远不到 1e7 阈值,
      // 于是城市级远视角下整片背景被当成几何算 AO,噪点压暗整个视口。
      // 改用原始深度判远(与 far 距离无关,鲁棒)。
      float d0 = texture(depthTex, vUv).r;
      if (d0 >= 0.99999) { outAo = vec4(1.0); return; }

      vec3 P = reconstructViewPos(vUv);
      // dFdx / dFdy of view position — cross gives the surface normal in view space.
      vec3 N = normalize(cross(dFdx(P), dFdy(P)));

      // 近平面退化点也跳过。
      if (P.z > -0.05) { outAo = vec4(1.0); return; }

      float radius    = pc.params.x;
      float intensity = pc.params.y;
      float bias      = pc.params.z;

      // 12 fixed offsets on the unit sphere (golden-spiral sample points).
      const vec3 kKernel[12] = vec3[12](
        vec3( 0.5381,  0.1856, -0.4319),
        vec3( 0.1379,  0.2486,  0.4430),
        vec3( 0.3371,  0.5679, -0.0057),
        vec3(-0.6999, -0.0451, -0.0019),
        vec3( 0.0689, -0.1598, -0.8547),
        vec3( 0.0560,  0.0069, -0.1843),
        vec3(-0.0146,  0.1402,  0.0762),
        vec3( 0.0100, -0.1924, -0.0344),
        vec3(-0.3577, -0.5301, -0.4358),
        vec3(-0.3169,  0.1063,  0.0158),
        vec3( 0.0103, -0.5869,  0.0046),
        vec3(-0.0897, -0.4940,  0.3287));

      // Per-pixel random rotation around N — kills the banding from a fixed kernel.
      float noise = hash12(gl_FragCoord.xy) * 6.2831853;
      float cs = cos(noise), sn = sin(noise);
      mat2  rot = mat2(cs, -sn, sn, cs);

      float occluded = 0.0;
      for (int i = 0; i < 12; ++i) {
        vec3 s = kKernel[i];
        // Flip into the hemisphere oriented by N.
        if (dot(s, N) < 0.0) s = -s;
        // Apply per-pixel rotation in the (T,B) plane perpendicular to N.
        vec2 r = rot * s.xy;
        vec3 sampleVec = vec3(r, s.z) * radius;
        vec3 samplePos = P + sampleVec;

        // Project to screen UV.
        vec4 sp = pc.proj * vec4(samplePos, 1.0);
        sp.xyz /= sp.w;
        vec2 sampleUv = sp.xy * 0.5 + 0.5;
        // Out-of-screen samples skip.
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 || sampleUv.y > 1.0) continue;

        // Read the surface depth at the sample's UV and reconstruct its view-Z.
        vec3 surfP = reconstructViewPos(sampleUv);
        // Range check: only count if the occluder is within `radius` — avoids
        // halo around silhouettes.
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - surfP.z), 1e-4));
        // The sample is occluded if the surface in front of it (view-space -Z is
        // forward) is closer than the sample point.
        if (surfP.z >= samplePos.z + bias) occluded += rangeCheck;
      }

      float ao = 1.0 - (occluded / 12.0) * intensity;
      outAo = vec4(clamp(ao, 0.0, 1.0));
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) return false;
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  // Descriptor: set=0 binding=0 = depth sampler.
  DescriptorSetLayoutDesc dl;
  DescriptorSetLayoutBinding lb;
  lb.binding         = 0u;
  lb.descriptorCount = 1u;
  lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  lb.descriptorType  = DescriptorType::CombinedImageSampler;
  dl.bindings.push_back(lb);
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  if (!desc_layout_) return false;

  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding     = 0u;
  db.type        = DescriptorType::CombinedImageSampler;
  db.textureView = depth_view_;
  db.sampler     = depth_sampler_;
  dsd.bindings.push_back(db);
  desc_set_ = device_->createDescriptorSet(desc_layout_, dsd);
  if (!desc_set_) return false;

  PipelineLayoutDesc pll;
  pll.setLayouts.push_back(desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size   = sizeof(PushData);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders                       = {vs_, fs_};
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = false;
  pd.depthStencil.depthWriteEnable = false;
  pd.renderPass                    = render_pass_;
  pd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void SsaoPass::record(const RHICommandBufferPtr& cmd, const PushData& pc) {
  Extent2D ext;
  if (explicit_w_ > 0 && explicit_h_ > 0) {
    ext.width  = explicit_w_;
    ext.height = explicit_h_;
  } else {
    ext = device_->getSwapchainExtent();
  }
  Rect2D area;
  area.offset = {0, 0};
  area.extent = ext;
  std::vector<std::array<float, 4>> clears = {{1.0f, 1.0f, 1.0f, 1.0f}};  // AO=1 default
  cmd->beginRenderPass(render_pass_, framebuffer_, area, clears);
  cmd->bindPipeline(pipeline_);

  Viewport vp;
  vp.x = 0.0f; vp.y = 0.0f;
  vp.width    = static_cast<float>(ext.width);
  vp.height   = static_cast<float>(ext.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd->setViewport(vp);
  cmd->setScissor(area);

  cmd->bindDescriptorSet(0u, desc_set_, pipeline_layout_);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->draw(3u);

  cmd->endRenderPass();
}

}  // namespace hongtu::renderer
