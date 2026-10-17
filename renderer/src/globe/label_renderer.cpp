#include <hongtu/renderer/globe/label_renderer.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using namespace HongTu::Asset;

void LabelRenderer::cleanup() {
  index_buffer_.reset();
  vertex_buffer_.reset();
  pipeline_.reset();
  pipeline_layout_.reset();
  fs_.reset();
  vs_.reset();
  index_count_ = 0;
  initialized_ = false;
}

bool LabelRenderer::init(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;
  if (!createPipeline(render_pass)) return false;
  initialized_ = true;
  dirty_ = true;
  spdlog::info("LabelRenderer initialized");
  return true;
}

bool LabelRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  const char* vs_src = R"glsl(
    layout(location = 0) in  vec3 inPos;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 color;
    } pc;
    void main() {
        gl_Position = pc.mvp * vec4(inPos, 1.0);
    }
  )glsl";

  const char* fs_src = R"glsl(
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 color;
    } pc;
    void main() {
        outColor = pc.color;
    }
  )glsl";

  auto vs_res = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
  auto fs_res = compiler.compileGLSLToSPIRV(fs_src, "main", "fragment");
  if (!vs_res.success || !fs_res.success) {
    spdlog::error("LabelRenderer: shader compile failed: vert='{}' frag='{}'",
                  vs_res.errorMessage, fs_res.errorMessage);
    return false;
  }

  ShaderDesc vsd;
  vsd.stage = ShaderStageFlagBits::Vertex;
  vsd.spirvCode = vs_res.spirvCode;
  vs_ = device_->createShader(vsd);

  ShaderDesc fsd;
  fsd.stage = ShaderStageFlagBits::Fragment;
  fsd.spirvCode = fs_res.spirvCode;
  fs_ = device_->createShader(fsd);

  if (!vs_ || !fs_) {
    spdlog::error("LabelRenderer: failed to create shader modules");
    return false;
  }

  // Pipeline layout: push constants only (no descriptor sets)
  PipelineLayoutDesc ld;
  PushConstantRange pcr;
  pcr.stageFlags = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;
  pcr.offset = 0u;
  pcr.size = sizeof(PushData);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) {
    spdlog::error("LabelRenderer: failed to create pipeline layout");
    return false;
  }

  VertexInputBindingDescription bind;
  bind.binding = 0u;
  bind.stride = sizeof(BillboardVertex);
  bind.perInstance = false;

  VertexInputAttributeDescription attr;
  attr.location = 0u;
  attr.binding = 0u;
  attr.format = Format::R32G32B32_SFLOAT;
  attr.offset = offsetof(BillboardVertex, position);

  // Alpha blending for translucent labels
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
  pd.vertexInput.attributes = {attr};
  pd.primitiveTopology = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode = CullMode::None;  // billboards always visible from either side
  pd.rasterization.fillMode = FillMode::Solid;
  // Labels are always-on-top markers; disable depth test so the camera-facing
  // quad doesn't get partially clipped when straddling the ellipsoid surface.
  pd.depthStencil.depthTestEnable = false;
  pd.depthStencil.depthWriteEnable = false;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("LabelRenderer: failed to create graphics pipeline");
    return false;
  }

  return true;
}

void LabelRenderer::addLabel(const Label& label) {
  labels_.push_back(label);
  dirty_ = true;
}

void LabelRenderer::clearLabels() {
  labels_.clear();
  vertex_buffer_.reset();
  index_buffer_.reset();
  index_count_ = 0;
  dirty_ = true;
}

void LabelRenderer::update(const GlobeCamera& camera) {
  if (!initialized_) return;

  const glm::dvec3& origin = camera.getRTCOrigin().getOriginECEF();
  if (!dirty_ && origin == cached_rtc_origin_) return;

  cached_rtc_origin_ = origin;
  rebuildBuffers(camera);
  dirty_ = false;
}

void LabelRenderer::rebuildBuffers(const GlobeCamera& camera) {
  if (labels_.empty()) {
    vertex_buffer_.reset();
    index_buffer_.reset();
    index_count_ = 0;
    return;
  }

  // Extract camera right and up from the RTC view matrix.
  // The view matrix rows are: [right, up, -forward] in column-major (glm) layout.
  // Column j, row i of a glm::mat4 is accessed as m[j][i].
  // Row 0 (right)  = (m[0][0], m[1][0], m[2][0])
  // Row 1 (up)     = (m[0][1], m[1][1], m[2][1])
  glm::mat4 view = camera.getRTCViewMatrix();
  glm::vec3 cam_right = glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
  glm::vec3 cam_up    = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));

  std::vector<BillboardVertex> verts;
  std::vector<u32> idxs;
  verts.reserve(labels_.size() * 4);
  idxs.reserve(labels_.size() * 6);

  for (const auto& label : labels_) {
    glm::dvec3 ecef  = coordinate::geodeticToECEF(label.position);
    glm::vec3 center = camera.getRTCOrigin().ecefToRTC(ecef);

    f32 hs = label.size_meters * 0.5f;
    glm::vec3 r = cam_right * hs;
    glm::vec3 u = cam_up * hs;

    u32 base = static_cast<u32>(verts.size());
    verts.push_back({center - r - u});  // BL
    verts.push_back({center + r - u});  // BR
    verts.push_back({center + r + u});  // TR
    verts.push_back({center - r + u});  // TL

    idxs.push_back(base + 0); idxs.push_back(base + 1); idxs.push_back(base + 2);
    idxs.push_back(base + 0); idxs.push_back(base + 2); idxs.push_back(base + 3);
  }

  // Upload vertex buffer
  {
    BufferDesc bd;
    bd.size = verts.size() * sizeof(BillboardVertex);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    vertex_buffer_ = device_->createBuffer(bd);
    if (vertex_buffer_) {
      void* ptr = vertex_buffer_->map();
      std::memcpy(ptr, verts.data(), bd.size);
      vertex_buffer_->unmap();
    }
  }

  // Upload index buffer
  {
    BufferDesc bd;
    bd.size = idxs.size() * sizeof(u32);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    index_buffer_ = device_->createBuffer(bd);
    if (index_buffer_) {
      void* ptr = index_buffer_->map();
      std::memcpy(ptr, idxs.data(), bd.size);
      index_buffer_->unmap();
    }
  }

  index_count_ = static_cast<u32>(idxs.size());
}

void LabelRenderer::render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect) {
  if (!initialized_ || index_count_ == 0) return;
  if (!vertex_buffer_ || !index_buffer_ || !pipeline_) return;

  glm::mat4 mvp = camera.getProjectionMatrix(aspect) * camera.getRTCViewMatrix();
  const ShaderStageFlags both = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;

  cmd->bindPipeline(pipeline_);
  cmd->bindVertexBuffer(0u, vertex_buffer_);
  cmd->bindIndexBuffer(index_buffer_);

  // One draw call per label to support per-label colors via push constants.
  for (size_t i = 0; i < labels_.size(); ++i) {
    PushData push;
    push.mvp = mvp;
    push.color = labels_[i].color;
    cmd->pushConstants(pipeline_layout_, both, 0u, sizeof(PushData), &push);
    cmd->drawIndexed(6u, 1u, static_cast<u32>(i * 6), 0, 0u);
  }
}

}  // namespace hongtu::renderer
