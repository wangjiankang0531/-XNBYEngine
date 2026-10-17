#include <hongtu/renderer/effects/light_wall_renderer.hpp>

#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

std::vector<WallVertex>
buildLightWall(const std::vector<glm::vec3>& boundary, float height, bool closed) {
  std::vector<WallVertex> out;
  if (boundary.size() < 2 || height <= 0.0f) return out;
  const size_t n = boundary.size();
  const size_t seg = closed ? n : (n - 1);
  out.reserve(seg * 6);
  float arc = 0.0f;
  for (size_t s = 0; s < seg; ++s) {
    const glm::vec3 a = boundary[s];
    const glm::vec3 b = boundary[(s + 1) % n];
    const float la = arc;
    const float lb = arc + glm::length(b - a);
    arc = lb;
    const glm::vec3 at = a + glm::vec3(0, 0, height);
    const glm::vec3 bt = b + glm::vec3(0, 0, height);
    // 两三角:a(底) b(底) at(顶) / b(底) bt(顶) at(顶)。uv.y 0 底 1 顶。
    out.push_back({a,  {la, 0.0f}});
    out.push_back({b,  {lb, 0.0f}});
    out.push_back({at, {la, 1.0f}});
    out.push_back({b,  {lb, 0.0f}});
    out.push_back({bt, {lb, 1.0f}});
    out.push_back({at, {la, 1.0f}});
  }
  return out;
}

bool LightWallRenderer::initialize(RHIDevice* device, RHIRenderPassPtr rp) {
  device_ = device;
  if (!device_) return false;
  return createPipeline(rp);
}

bool LightWallRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  const char* vsrc = R"glsl(
    layout(location = 0) in vec3 inPos;
    layout(location = 1) in vec2 inUv;
    layout(push_constant) uniform PC { mat4 view_proj; vec4 color; vec4 fx; } pc;
    layout(location = 0) out vec2 vUv;
    void main() {
      gl_Position = pc.view_proj * vec4(inPos, 1.0);
      vUv = inUv;
    }
  )glsl";

  // 底实顶虚 + 竖向流动光带。color.a=基透明,fx.x=time,fx.y=flow_speed。
  const char* fsrc = R"glsl(
    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC { mat4 view_proj; vec4 color; vec4 fx; } pc;
    void main() {
      float h = clamp(vUv.y, 0.0, 1.0);
      float a = pc.color.a * pow(1.0 - h, 1.6);   // 自底向上衰减
      vec3  rgb = pc.color.rgb;
      // 流动:滚动亮带,叠一层动态光感。fx.z 选方向:
      //   横向(0)沿周长 vUv.x 滚;竖向(1)自底向上沿 vUv.y 滚。
      if (abs(pc.fx.y) > 0.001) {
        float coord = (pc.fx.z > 0.5) ? vUv.y : (vUv.x * 0.08);
        float band  = fract(coord - pc.fx.x * pc.fx.y * 0.1);
        float pulse = pow(1.0 - band, 4.0);
        a   += pc.color.a * 0.6 * pulse * (1.0 - h);
        rgb  = mix(rgb, rgb * 1.6 + 0.2, pulse);
      }
      // 顶缘细亮线(围栏感)。
      a += smoothstep(0.92, 1.0, h) * pc.color.a * 0.5;
      if (a < 0.01) discard;
      outColor = vec4(rgb, a);
    }
  )glsl";

  auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vs_r.success || !fs_r.success) {
    spdlog::error("LightWallRenderer: shader compile failed vs='{}' fs='{}'",
                  vs_r.errorMessage, fs_r.errorMessage);
    return false;
  }
  ShaderDesc vsd; vsd.stage = ShaderStageFlagBits::Vertex;   vsd.spirvCode = vs_r.spirvCode;
  ShaderDesc fsd; fsd.stage = ShaderStageFlagBits::Fragment; fsd.spirvCode = fs_r.spirvCode;
  vs_ = device_->createShader(vsd);
  fs_ = device_->createShader(fsd);
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc ld;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0u; pcr.size = sizeof(PushConstants);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) return false;

  VertexInputBindingDescription bind;
  bind.binding = 0u; bind.stride = sizeof(WallVertex); bind.perInstance = false;
  VertexInputAttributeDescription pos_attr;
  pos_attr.location = 0u; pos_attr.binding = 0u;
  pos_attr.format = Format::R32G32B32_SFLOAT; pos_attr.offset = offsetof(WallVertex, position);
  VertexInputAttributeDescription uv_attr;
  uv_attr.location = 1u; uv_attr.binding = 0u;
  uv_attr.format = Format::R32G32_SFLOAT; uv_attr.offset = offsetof(WallVertex, uv);

  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::Zero;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings = {bind};
  pd.vertexInput.attributes = {pos_attr, uv_attr};
  pd.primitiveTopology = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode = CullMode::None;   // 双面墙
  pd.rasterization.fillMode = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = false;
  pd.depthStencil.depthCompareOp   = CompareOp::LessOrEqual;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void LightWallRenderer::upload(const std::vector<WallVertex>& verts) {
  const uint32_t needed = static_cast<uint32_t>(verts.size());
  if (needed == 0) return;
  if (!vbo_ || vbo_capacity_ < needed) {
    const uint32_t grown = std::max(needed * 2u, 256u);
    BufferDesc bd;
    bd.size = static_cast<uint64_t>(grown) * sizeof(WallVertex);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    vbo_ = device_->createBuffer(bd);
    vbo_capacity_ = grown;
  }
  if (!vbo_) return;
  std::memcpy(vbo_->map(), verts.data(), verts.size() * sizeof(WallVertex));
  vbo_->unmap();
}

void LightWallRenderer::record(const RHICommandBufferPtr& cmd, const Params& p,
                               const std::vector<WallDraw>& walls) {
  if (walls.empty()) return;
  std::vector<WallVertex> combined;
  struct Range { uint32_t first, count; glm::vec4 color, fx; };
  std::vector<Range> ranges;
  for (const auto& w : walls) {
    auto v = buildLightWall(w.boundary, w.height, w.closed);
    glm::vec4 fx{p.time_s, w.flow_speed, w.flow_vertical ? 1.0f : 0.0f, 0};
    if (v.empty()) { ranges.push_back({0u, 0u, w.color, fx}); continue; }
    ranges.push_back({static_cast<uint32_t>(combined.size()),
                      static_cast<uint32_t>(v.size()), w.color, fx});
    combined.insert(combined.end(), v.begin(), v.end());
  }
  if (combined.empty()) return;
  upload(combined);
  if (!vbo_) return;

  const ShaderStageFlags stages =
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  cmd->bindPipeline(pipeline_);
  cmd->bindVertexBuffer(0, vbo_);
  PushConstants pc;
  pc.view_proj = p.view_proj;
  for (const auto& r : ranges) {
    if (r.count == 0) continue;
    pc.color = r.color;
    pc.fx    = r.fx;
    cmd->pushConstants(pipeline_layout_, stages, 0, sizeof(pc), &pc);
    cmd->draw(r.count, 1u, r.first, 0u);
  }
}

}  // namespace hongtu::renderer
