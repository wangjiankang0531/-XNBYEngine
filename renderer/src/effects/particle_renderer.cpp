#include <hongtu/renderer/effects/particle_renderer.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstring>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool ParticleRenderer::initialize(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;
  return createPipeline(render_pass);
}

bool ParticleRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Vertex shader: generate quad corner from gl_VertexIndex, offset along
  // camera_right / camera_up by `instance.size`, project. Pass corner-
  // relative UV (-1..1) to fragment for radial fade.
  const char* vsrc = R"glsl(
    layout(location = 1) in vec3  inWorldPos;
    layout(location = 2) in float inSize;
    layout(location = 3) in vec4  inColor;

    layout(push_constant) uniform PC {
      mat4 view_proj;
      vec4 camera_right;
      vec4 camera_up;
    } pc;

    layout(location = 0) out vec2 vCornerUV;   // (-1..1) for radial fade
    layout(location = 1) out vec4 vColor;

    void main() {
      // 0=BL, 1=BR, 2=TL, 3=TR (triangle strip)
      vec2 corner = vec2(
          (gl_VertexIndex & 1) != 0 ?  1.0 : -1.0,
          (gl_VertexIndex & 2) != 0 ?  1.0 : -1.0);
      vec3 offs = pc.camera_right.xyz * (corner.x * inSize * 0.5)
                + pc.camera_up.xyz    * (corner.y * inSize * 0.5);
      vec3 world = inWorldPos + offs;
      gl_Position = pc.view_proj * vec4(world, 1.0);
      vCornerUV   = corner;
      vColor      = inColor;
    }
  )glsl";

  // Fragment shader: radial alpha + cheap procedural noise so close-up
  // particles read as "flame tips" rather than smooth gaussian discs.
  //
  // Noise breaks the smooth (1-d²)² falloff into wispy filaments —
  // without it, at <20m altitude every particle looks like a perfect
  // soft blob and the fire reads as 浆糊.  Uses an interleaved gradient
  // noise (Jorge Jimenez) — single-instruction, no textures, no
  // descriptors, scales linearly with particle screen size.
  const char* fsrc = R"glsl(
    layout(location = 0) in vec2 vCornerUV;
    layout(location = 1) in vec4 vColor;
    layout(location = 0) out vec4 outColor;
    float ign(vec2 p) {
      return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
    }
    void main() {
      float d2 = dot(vCornerUV, vCornerUV);    // 0 at center, 2 at corner
      if (d2 > 1.0) discard;                    // outside the disc
      // Sharper core — exponent 4 keeps a bright centre and quick falloff
      // so individual particles read as distinct flame licks rather than
      // overlapping fog.
      float core = 1.0 - d2;
      float alpha = core * core * core * core;
      // Modulate by per-fragment noise tied to screen-space gl_FragCoord
      // so the pattern doesn't slide with the particle (which would look
      // like the texture is sliding past the flame).
      float n = ign(gl_FragCoord.xy + vColor.rgb.r * 17.0);
      alpha *= mix(0.55, 1.15, n);
      outColor = vec4(vColor.rgb * vColor.a * alpha,
                      vColor.a * alpha);
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("ParticleRenderer: shader fail vs='{}' fs='{}'",
                  vr.errorMessage, fr.errorMessage);
    return false;
  }

  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  // Pipeline layout: push constants only (no descriptor set — radial fade
  // is purely procedural in the fragment shader).
  PipelineLayoutDesc ld;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
  pcr.offset = 0u;
  pcr.size   = sizeof(PushConstants);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) return false;

  // Per-instance vertex binding (location 1..3 — leave 0 for compatibility
  // with future per-vertex variant if we add textured particles).
  VertexInputBindingDescription inst_bind;
  inst_bind.binding     = 0u;
  inst_bind.stride      = sizeof(ParticleInstance);
  inst_bind.perInstance = true;

  std::vector<VertexInputAttributeDescription> attrs(3);
  attrs[0].location = 1u;
  attrs[0].binding  = 0u;
  attrs[0].format   = Format::R32G32B32_SFLOAT;
  attrs[0].offset   = offsetof(ParticleInstance, world_pos);
  attrs[1].location = 2u;
  attrs[1].binding  = 0u;
  attrs[1].format   = Format::R32_SFLOAT;
  attrs[1].offset   = offsetof(ParticleInstance, size);
  attrs[2].location = 3u;
  attrs[2].binding  = 0u;
  attrs[2].format   = Format::R32G32B32A32_SFLOAT;
  attrs[2].offset   = offsetof(ParticleInstance, color);

  // Additive blend — overlapping particles brighten (fire / sparks look).
  // Smoke uses alpha-over via the same shader since its color.a already
  // encodes "near-zero on edges" — the additive math doesn't actually hurt
  // it because the rgb is gray * alpha; for the demo "good enough".
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::One;          // pre-multiplied alpha in shader
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings = {inst_bind};
  pd.vertexInput.attributes = attrs;
  pd.primitiveTopology = PrimitiveTopology::TriangleStrip;
  pd.rasterization.cullMode = CullMode::None;
  pd.rasterization.fillMode = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = false;     // 透明 particle 不写 depth
  pd.depthStencil.depthCompareOp   = CompareOp::LessOrEqual;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void ParticleRenderer::uploadInstances(const std::vector<ParticleInstance>& inst) {
  const uint32_t needed = static_cast<uint32_t>(inst.size());
  if (needed == 0) return;
  if (!inst_vbo_ || inst_vbo_capacity_ < needed) {
    const uint32_t grown = std::max(needed * 2u, 1024u);
    BufferDesc bd;
    bd.size = static_cast<uint64_t>(grown) * sizeof(ParticleInstance);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    inst_vbo_ = device_->createBuffer(bd);
    inst_vbo_capacity_ = grown;
  }
  if (!inst_vbo_) return;
  void* dst = inst_vbo_->map();
  std::memcpy(dst, inst.data(), inst.size() * sizeof(ParticleInstance));
  inst_vbo_->unmap();
}

void ParticleRenderer::record(const RHICommandBufferPtr& cmd, const Params& p,
                              const std::vector<ParticleInstance>& instances) {
  if (instances.empty() || !pipeline_) return;
  uploadInstances(instances);
  if (!inst_vbo_) return;

  PushConstants pc{};
  pc.view_proj    = p.view_proj;
  pc.camera_right = glm::vec4(p.camera_right, 0.0f);
  pc.camera_up    = glm::vec4(p.camera_up,    0.0f);

  cmd->bindPipeline(pipeline_);
  cmd->bindVertexBuffer(0, inst_vbo_);
  cmd->pushConstants(pipeline_layout_,
                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
                     0, sizeof(pc), &pc);
  cmd->draw(4u, static_cast<uint32_t>(instances.size()), 0u, 0u);
}

void ParticleRenderer::recordFromBuffer(const RHICommandBufferPtr& cmd, const Params& p,
                                        const RHIBufferPtr& instance_buffer,
                                        uint32_t instance_count,
                                        uint32_t stride_bytes) {
  if (!pipeline_ || !instance_buffer || instance_count == 0) return;
  // 当前 pipeline 用 sizeof(ParticleInstance) = 32B stride。GPU buffer 与
  // CPU ParticleInstance 同布局 → 直接 bind 即可,stride 不变。stride_bytes
  // 参数留作未来 dynamic-stride 扩展接口(Vulkan 1.3 vkCmdBindVertexBuffers2)
  (void)stride_bytes;

  PushConstants pc{};
  pc.view_proj    = p.view_proj;
  pc.camera_right = glm::vec4(p.camera_right, 0.0f);
  pc.camera_up    = glm::vec4(p.camera_up,    0.0f);

  cmd->bindPipeline(pipeline_);
  cmd->bindVertexBuffer(0, instance_buffer);
  cmd->pushConstants(pipeline_layout_,
                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
                     0, sizeof(pc), &pc);
  cmd->draw(4u, instance_count, 0u, 0u);
}

}  // namespace hongtu::renderer
