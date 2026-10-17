#include <hongtu/renderer/pbr/shadow_pass.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool ShadowPass::initialize(RHIDevice* device, uint32_t resolution) {
  device_     = device;
  resolution_ = resolution;

  // ---- depth texture (sampled later as a shadow map) ----
  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {resolution_, resolution_, 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::D32_SFLOAT;
  td.usage       = TextureUsageFlagBits::DepthStencilAttachment
                 | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  depth_tex_ = device_->createTexture(td);
  if (!depth_tex_) return false;
  depth_view_ = depth_tex_->createView();
  if (!depth_view_) return false;

  // ---- render pass: depth-only ----
  RenderPassDesc rpd;
  AttachmentDescription da;
  da.format        = Format::D32_SFLOAT;
  da.sampleCount   = 1;
  da.initialLayout = ImageLayout::Undefined;
  da.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;  // sampleable next
  da.loadOpClear   = true;
  da.storeOpStore  = true;
  rpd.attachments.push_back(da);

  SubpassDescription sp;
  sp.depthStencilAttachment.attachment = 0;
  sp.depthStencilAttachment.layout = ImageLayout::DepthStencilAttachmentOptimal;
  sp.hasDepthStencil = true;
  rpd.subpasses.push_back(sp);

  SubpassDependency dep;
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  dep.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
  rpd.dependencies.push_back(dep);
  // 出口依赖:shadow map 被主 pass 片元采样(同 HDR pass 注释)。
  SubpassDependency out;
  out.srcSubpass    = 0;
  out.dstSubpass    = VK_SUBPASS_EXTERNAL;
  out.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::LateFragmentTests);
  out.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
  out.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  out.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
  rpd.dependencies.push_back(out);
  render_pass_ = device_->createRenderPass(rpd);
  if (!render_pass_) return false;

  // ---- framebuffer ----
  framebuffer_ = device_->createFramebuffer(render_pass_, {depth_view_},
                                              {resolution_, resolution_}, 1);
  if (!framebuffer_) return false;

  // ---- shadow shaders: vertex transforms by lightMVP, fragment is empty ----
  ShaderCompiler compiler;
  const char* vsrc = R"glsl(
    layout(location = 0) in vec3 inPosition;
    // Same vertex layout as the main pipeline (normal/uv ignored).
    layout(push_constant) uniform PC { mat4 light_mvp; } pc;
    void main() { gl_Position = pc.light_mvp * vec4(inPosition, 1.0); }
  )glsl";
  const char* fsrc = R"glsl(
    void main() {}
  )glsl";
  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) return false;
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  // ---- pipeline (depth write, no colour) ----
  PipelineLayoutDesc pl;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
  pcr.offset = 0; pcr.size = sizeof(PushData);
  pl.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pl);
  if (!pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders                       = {vs_, fs_};
  pd.vertexInput.bindings          = {MeshVertex::binding()};
  pd.vertexInput.attributes        = MeshVertex::attributes();
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  // Render both sides into the shadow map — matches the main pass's
  // double-sided handling for assets without consistent winding.
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.renderPass                    = render_pass_;
  pd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) return false;

  // ---- sampler (linear filter, clamp outside frustum) ----
  SamplerDesc ssd;
  ssd.magFilter        = SamplerFilter::Linear;
  ssd.minFilter        = SamplerFilter::Linear;
  ssd.mipmapMode       = SamplerMipmapMode::Linear;
  ssd.addressModeU     = SamplerAddressMode::ClampToEdge;
  ssd.addressModeV     = SamplerAddressMode::ClampToEdge;
  ssd.addressModeW     = SamplerAddressMode::ClampToEdge;
  ssd.anisotropyEnable = false;
  sampler_ = device_->createSampler(ssd);
  if (!sampler_) return false;

  // ---- descriptor set 1 binding 0 = shadow sampler ----
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
  db.sampler     = sampler_;
  dsd.bindings.push_back(db);
  desc_set_ = device_->createDescriptorSet(desc_layout_, dsd);
  if (!desc_set_) return false;

  cmd_ = device_->createCommandBuffer();
  return cmd_ != nullptr;
}

void ShadowPass::render(const glm::vec3& light_dir,
                         const glm::vec3& center,
                         float extent,
                         const RHIBufferPtr& vbo,
                         const RHIBufferPtr& ibo,
                         const std::vector<glm::mat4>& entity_models,
                         const std::vector<PrimitiveSlice>& primitives) {
  // Light camera tight around the scene bbox.
  glm::vec3 ld  = glm::normalize(light_dir);
  glm::vec3 eye = center + ld * extent * 2.0f;
  glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0, 0, 1));
  glm::mat4 proj = glm::ortho(-extent, extent, -extent, extent,
                                0.1f, extent * 4.0f);
  // Vulkan: flip Y so light-space NDC matches our main projection convention.
  proj[1][1] *= -1.0f;
  // glm::ortho (no GLM_FORCE_DEPTH_ZERO_TO_ONE) emits GL-style z in [-1,1];
  // Vulkan clips z to [0,1], which silently dropped the light-near half of
  // the scene from the shadow map and skewed the sampled comparison depth —
  // shadows read as "fully lit" almost everywhere. Remap z' = 0.5z + 0.5w.
  const glm::mat4 z01(1, 0, 0, 0,
                      0, 1, 0, 0,
                      0, 0, 0.5f, 0,
                      0, 0, 0.5f, 1);
  light_view_proj_ = z01 * proj * view;

  cmd_->reset();
  cmd_->begin();

  Rect2D area;
  area.offset = {0, 0};
  area.extent = {resolution_, resolution_};
  std::vector<std::array<float, 4>> clears = {{1.0f, 0.0f, 0.0f, 0.0f}};  // depth=1
  cmd_->beginRenderPass(render_pass_, framebuffer_, area, clears);

  cmd_->bindPipeline(pipeline_);

  Viewport vp;
  vp.x = 0.0f; vp.y = 0.0f;
  vp.width    = static_cast<float>(resolution_);
  vp.height   = static_cast<float>(resolution_);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  cmd_->bindVertexBuffer(0, vbo);
  cmd_->bindIndexBuffer(ibo);

  for (const auto& model : entity_models) {
    PushData sp;
    sp.light_mvp = light_view_proj_ * model;
    cmd_->pushConstants(pipeline_layout_,
        static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
        0, sizeof(PushData), &sp);
    for (const auto& prim : primitives) {
      cmd_->drawIndexed(prim.second, 1u, prim.first);
    }
  }

  if (external_draw_) external_draw_(cmd_, light_view_proj_);
  cmd_->endRenderPass();
  cmd_->end();

  // Synchronous submit — shadow pass must complete before main pass samples
  // the map. Cheap on Apple M-series; can be made async-overlapped later.
  device_->submit({cmd_}, {}, {}, nullptr);
  device_->waitIdle();
}

void ShadowPass::renderBatched(const glm::vec3& light_dir,
                                  const glm::vec3& center,
                                  float extent,
                                  const std::vector<Batch>& batches) {
  glm::vec3 ld  = glm::normalize(light_dir);
  glm::vec3 eye = center + ld * extent * 2.0f;
  glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0, 0, 1));
  glm::mat4 proj = glm::ortho(-extent, extent, -extent, extent,
                                0.1f, extent * 4.0f);
  proj[1][1] *= -1.0f;
  // GL→Vulkan depth remap, same reasoning as render() above.
  const glm::mat4 z01(1, 0, 0, 0,
                      0, 1, 0, 0,
                      0, 0, 0.5f, 0,
                      0, 0, 0.5f, 1);
  light_view_proj_ = z01 * proj * view;

  cmd_->reset();
  cmd_->begin();
  Rect2D area;
  area.offset = {0, 0};
  area.extent = {resolution_, resolution_};
  std::vector<std::array<float, 4>> clears = {{1.0f, 0.0f, 0.0f, 0.0f}};
  cmd_->beginRenderPass(render_pass_, framebuffer_, area, clears);
  cmd_->bindPipeline(pipeline_);
  Viewport vp;
  vp.x = 0.0f; vp.y = 0.0f;
  vp.width    = static_cast<float>(resolution_);
  vp.height   = static_cast<float>(resolution_);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  for (const auto& b : batches) {
    if (!b.vbo || !b.ibo) continue;
    cmd_->bindVertexBuffer(0, b.vbo);
    cmd_->bindIndexBuffer(b.ibo);
    for (const auto& model : b.entity_models) {
      PushData sp;
      sp.light_mvp = light_view_proj_ * model;
      cmd_->pushConstants(pipeline_layout_,
          static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
          0, sizeof(PushData), &sp);
      for (const auto& prim : b.primitives) {
        cmd_->drawIndexed(prim.second, 1u, prim.first);
      }
    }
  }

  if (external_draw_) external_draw_(cmd_, light_view_proj_);
  cmd_->endRenderPass();
  cmd_->end();
  device_->submit({cmd_}, {}, {}, nullptr);
  device_->waitIdle();
}

}  // namespace hongtu::renderer
