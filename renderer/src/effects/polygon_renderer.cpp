#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/renderer/effects/polygon_renderer.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace hongtu::renderer {

  using HongTu::Asset::ShaderCompiler;

  // =============================================================================
  // Pure fan triangulation
  // =============================================================================

  std::vector<PolygonVertex> triangulateFan(const std::vector<glm::vec3>& points, const glm::vec4& color) {
    if (points.size() < 3)
      return {};

    // 质心 + 最大半径 → 每顶点归一 uv(中心 0,边界 ±1),供填充效果着色器。
    glm::vec3 centroid(0.0f);
    for (const auto& p : points)
      centroid += p;
    centroid /= static_cast<float>(points.size());
    float maxR = 1e-4f;
    for (const auto& p : points)
      maxR = std::max(maxR, glm::length(p - centroid));
    auto uvOf = [&](const glm::vec3& p) {
      return glm::vec2((p.x - centroid.x) / maxR, (p.y - centroid.y) / maxR);
    };

    std::vector<PolygonVertex> out;
    out.reserve((points.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
      out.push_back({points[0], color, uvOf(points[0])});
      out.push_back({points[i], color, uvOf(points[i])});
      out.push_back({points[i + 1], color, uvOf(points[i + 1])});
    }
    return out;
  }

  // =============================================================================
  // PolygonRenderer
  // =============================================================================

  bool PolygonRenderer::initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass) {
    device_ = device;
    if (!device_)
      return false;
    return createPipeline(scene_render_pass);
  }

  bool PolygonRenderer::createPipeline(RHIRenderPassPtr render_pass) {
    ShaderCompiler compiler;

    const char* vsrc = R"glsl(
    layout(location = 0) in vec3 inPos;
    layout(location = 1) in vec4 inColor;
    layout(location = 2) in vec2 inUv;
    layout(push_constant) uniform PC { mat4 view_proj; vec4 fx; } pc;
    layout(location = 0) out vec4 vColor;
    layout(location = 1) out vec2 vUv;
    void main() {
      gl_Position = pc.view_proj * vec4(inPos, 1.0);
      vColor = inColor;
      vUv    = inUv;
    }
  )glsl";

    // fx.x=fill_type(0纯色 1扫描 2网格 3脉冲) y=time z=主参
    const char* fsrc = R"glsl(
    layout(location = 0) in  vec4 vColor;
    layout(location = 1) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC { mat4 view_proj; vec4 fx; } pc;
    const float PI = 3.14159265;
    void main() {
      int   type = int(pc.fx.x + 0.5);
      float t    = pc.fx.y;
      float prm  = pc.fx.z;
      float a    = vColor.a;
      vec3  rgb  = vColor.rgb;
      if (type == 1) {
        // 扫描·雷达:旋转亮边 + 暗底,边缘淡出。
        float ang = atan(vUv.y, vUv.x) / (2.0 * PI) + 0.5;  // 0..1
        float sweep = fract(ang - t * max(prm, 0.05) * 0.15);
        float lead = pow(1.0 - sweep, 6.0);                 // 拖尾亮边
        a = vColor.a * (0.18 + 0.9 * lead);
        rgb = mix(vColor.rgb, vColor.rgb * 1.6 + 0.2, lead);
      } else if (type == 2) {
        // 网格·科技格:程序化格线 + 暗底。
        float n = max(prm, 1.0);
        vec2 g = abs(fract(vUv * n) - 0.5);
        float line = smoothstep(0.46, 0.5, max(g.x, g.y));
        a = vColor.a * (0.22 + 0.85 * line);
        rgb = mix(vColor.rgb, vColor.rgb * 1.5 + 0.15, line);
      } else if (type == 3) {
        // 脉冲·呼吸:整体透明度随时间正弦起伏。
        float puls = 0.5 + 0.5 * sin(t * max(prm, 0.1) * 2.0);
        a = vColor.a * (0.3 + 0.7 * puls);
      }
      // 边缘淡出(效果填充统一柔化,避免硬边):纯色 type0 不淡。
      if (type != 0) a *= smoothstep(1.03, 0.8, length(vUv));
      outColor = vec4(rgb, a);
    }
  )glsl";

    auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
    auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
    if (!vs_r.success || !fs_r.success) {
      spdlog::error("PolygonRenderer: shader compile failed vs='{}' fs='{}'", vs_r.errorMessage, fs_r.errorMessage);
      return false;
    }

    ShaderDesc vsd;
    vsd.stage = ShaderStageFlagBits::Vertex;
    vsd.spirvCode = vs_r.spirvCode;
    vs_ = device_->createShader(vsd);

    ShaderDesc fsd;
    fsd.stage = ShaderStageFlagBits::Fragment;
    fsd.spirvCode = fs_r.spirvCode;
    fs_ = device_->createShader(fsd);
    if (!vs_ || !fs_)
      return false;

    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    pcr.offset = 0u;
    pcr.size = sizeof(PushConstants);
    ld.pushConstantRanges.push_back(pcr);
    pipeline_layout_ = device_->createPipelineLayout(ld);
    if (!pipeline_layout_)
      return false;

    VertexInputBindingDescription bind;
    bind.binding = 0u;
    bind.stride = sizeof(PolygonVertex);
    bind.perInstance = false;

    VertexInputAttributeDescription pos_attr;
    pos_attr.location = 0u;
    pos_attr.binding = 0u;
    pos_attr.format = Format::R32G32B32_SFLOAT;
    pos_attr.offset = offsetof(PolygonVertex, position);

    VertexInputAttributeDescription col_attr;
    col_attr.location = 1u;
    col_attr.binding = 0u;
    col_attr.format = Format::R32G32B32A32_SFLOAT;
    col_attr.offset = offsetof(PolygonVertex, color);

    VertexInputAttributeDescription uv_attr;
    uv_attr.location = 2u;
    uv_attr.binding = 0u;
    uv_attr.format = Format::R32G32_SFLOAT;
    uv_attr.offset = offsetof(PolygonVertex, uv);

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
    pd.vertexInput.attributes = {pos_attr, col_attr, uv_attr};
    pd.primitiveTopology = PrimitiveTopology::TriangleList;
    pd.rasterization.cullMode = CullMode::None;
    pd.rasterization.fillMode = FillMode::Solid;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthWriteEnable = false;
    pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
    pd.colorBlendAttachments.push_back(blend);
    pd.renderPass = render_pass;
    pd.pipelineLayout = pipeline_layout_;

    pipeline_ = device_->createGraphicsPipeline(pd);
    return pipeline_ != nullptr;
  }

  void PolygonRenderer::uploadFan(const std::vector<PolygonVertex>& fan) {
    const uint32_t needed = static_cast<uint32_t>(fan.size());
    if (needed == 0)
      return;
    if (!fan_vbo_ || fan_vbo_capacity_ < needed) {
      const uint32_t grown = std::max(needed * 2u, 256u);
      BufferDesc bd;
      bd.size = static_cast<uint64_t>(grown) * sizeof(PolygonVertex);
      bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
      bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      fan_vbo_ = device_->createBuffer(bd);
      fan_vbo_capacity_ = grown;
    }
    if (!fan_vbo_)
      return;
    void* dst = fan_vbo_->map();
    std::memcpy(dst, fan.data(), fan.size() * sizeof(PolygonVertex));
    fan_vbo_->unmap();
  }

  void PolygonRenderer::record(const RHICommandBufferPtr& cmd,
                               const Params& p,
                               const std::vector<PolygonDraw>& polygons) {
    if (polygons.empty())
      return;

    std::vector<PolygonVertex> combined;
    struct Range {
      uint32_t first;
      uint32_t count;
      glm::vec4 fx;
    };
    std::vector<Range> ranges;
    ranges.reserve(polygons.size());

    for (const auto& poly : polygons) {
      auto fan = triangulateFan(poly.points, poly.color);
      glm::vec4 fx{static_cast<float>(poly.fill_type), p.time_s, poly.fill_params.x, 0.0f};
      if (fan.empty()) {
        ranges.push_back({0u, 0u, fx});
        continue;
      }
      ranges.push_back({static_cast<uint32_t>(combined.size()), static_cast<uint32_t>(fan.size()), fx});
      combined.insert(combined.end(), fan.begin(), fan.end());
    }
    if (combined.empty())
      return;

    uploadFan(combined);
    if (!fan_vbo_)
      return;

    const ShaderStageFlags stages = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                                    static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    cmd->bindPipeline(pipeline_);
    cmd->bindVertexBuffer(0, fan_vbo_);
    PushConstants pc;
    pc.view_proj = p.view_proj;
    for (const auto& r : ranges) {
      if (r.count == 0)
        continue;
      pc.fx = r.fx;
      cmd->pushConstants(pipeline_layout_, stages, 0, sizeof(pc), &pc);
      cmd->draw(r.count, 1u, r.first, 0u);
    }
  }

  void PolygonRenderer::recordTriangles(const RHICommandBufferPtr& cmd,
                                        const Params& p,
                                        const std::vector<PolygonVertex>& triangles) {
    if (triangles.empty())
      return;
    if (triangles.size() % 3 != 0)
      return;
    uploadFan(triangles);
    if (!fan_vbo_)
      return;
    PushConstants pc{p.view_proj, glm::vec4(0.0f)};
    cmd->bindPipeline(pipeline_);
    cmd->pushConstants(pipeline_layout_,
                       static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                           static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
                       0,
                       sizeof(pc),
                       &pc);
    cmd->bindVertexBuffer(0, fan_vbo_);
    cmd->draw(static_cast<uint32_t>(triangles.size()));
  }

} // namespace hongtu::renderer
