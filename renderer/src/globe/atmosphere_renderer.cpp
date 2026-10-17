#include <hongtu/renderer/globe/atmosphere_renderer.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using namespace HongTu::Asset;

// WGS84 semi-major axis + ~160 km atmosphere shell
static constexpr double kAtmoRadius = 6378137.0 * 1.025;

// UV sphere resolution (lat × lon segments)
static constexpr int kLatSegs = 48;
static constexpr int kLonSegs = 96;

// ============================================================================
// Lifecycle
// ============================================================================

AtmosphereRenderer::~AtmosphereRenderer() {
  cleanup();
}

void AtmosphereRenderer::cleanup() {
  index_buffer_.reset();
  vertex_buffer_.reset();
  pipeline_.reset();
  pipeline_layout_.reset();
  fragment_shader_.reset();
  vertex_shader_.reset();
  index_count_ = 0;
  initialized_ = false;
}

// ============================================================================
// init
// ============================================================================

bool AtmosphereRenderer::init(RHIDevice* device, RHIRenderPassPtr render_pass) {
  if (!device || !render_pass) {
    spdlog::error("AtmosphereRenderer::init: null device or render_pass");
    return false;
  }
  device_ = device;

  ShaderCompiler compiler;

  // Vertex shader: transforms sphere vertices and passes position/normal to fragment.
  const char* vs_src = R"glsl(
    layout(location = 0) in  vec3 inPos;
    layout(location = 1) in  vec3 inNormal;
    layout(location = 0) out vec3 outNormal;
    layout(location = 1) out vec3 outFragPos;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 camPos;
    } pc;
    void main() {
        outNormal  = inNormal;
        outFragPos = inPos;
        gl_Position = pc.mvp * vec4(inPos, 1.0);
    }
  )glsl";

  // Fragment shader: Fresnel rim glow — strongest at limb (N ⊥ V), zero at center.
  // pc.camPos.w carries an altitude-based fade factor [0,1] set by render().
  const char* fs_src = R"glsl(
    layout(location = 0) in  vec3 inNormal;
    layout(location = 1) in  vec3 inFragPos;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 camPos;   // xyz = camera position in RTC, w = altitude fade [0,1]
    } pc;
    void main() {
        float fade = pc.camPos.w;

        vec3 N = normalize(inNormal);
        vec3 V = normalize(pc.camPos.xyz - inFragPos);

        float rim = 1.0 - abs(dot(N, V));
        rim = pow(clamp(rim, 0.0, 1.0), 3.5);

        vec3 innerColor = vec3(0.20, 0.50, 1.00);
        vec3 outerColor = vec3(0.55, 0.80, 1.00);
        vec3 atmoColor  = mix(innerColor, outerColor, rim);

        float intensity = rim * 0.75 * fade;
        outColor = vec4(atmoColor * intensity, intensity);
    }
  )glsl";

  auto vs_result = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
  auto fs_result = compiler.compileGLSLToSPIRV(fs_src, "main", "fragment");

  if (!vs_result.success || !fs_result.success) {
    spdlog::error("AtmosphereRenderer: shader compilation failed: vert='{}' frag='{}'",
                  vs_result.errorMessage, fs_result.errorMessage);
    return false;
  }

  ShaderDesc vs_desc;
  vs_desc.stage = ShaderStageFlagBits::Vertex;
  vs_desc.spirvCode = vs_result.spirvCode;
  vertex_shader_ = device_->createShader(vs_desc);

  ShaderDesc fs_desc;
  fs_desc.stage = ShaderStageFlagBits::Fragment;
  fs_desc.spirvCode = fs_result.spirvCode;
  fragment_shader_ = device_->createShader(fs_desc);

  if (!vertex_shader_ || !fragment_shader_) {
    spdlog::error("AtmosphereRenderer: failed to create shader modules");
    return false;
  }

  // Pipeline layout: no descriptor sets, push constant covers both stages
  {
    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;
    pcr.offset = 0u;
    pcr.size   = sizeof(AtmoPushData);  // 80 bytes
    ld.pushConstantRanges.push_back(pcr);

    pipeline_layout_ = device_->createPipelineLayout(ld);
    if (!pipeline_layout_) {
      spdlog::error("AtmosphereRenderer: failed to create pipeline layout");
      return false;
    }
  }

  // Vertex input: position (location 0) + normal (location 1)
  VertexInputBindingDescription bind;
  bind.binding   = 0u;
  bind.stride    = sizeof(AtmoVertex);
  bind.perInstance = false;

  std::vector<VertexInputAttributeDescription> attrs(2);
  attrs[0] = {0u, 0u, Format::R32G32B32_SFLOAT, offsetof(AtmoVertex, position)};
  attrs[1] = {1u, 0u, Format::R32G32B32_SFLOAT, offsetof(AtmoVertex, normal)};

  // Graphics pipeline: standard culling, additive blending, no depth write
  GraphicsPipelineDesc pd;
  pd.shaders = {vertex_shader_, fragment_shader_};
  pd.vertexInput.bindings   = {bind};
  pd.vertexInput.attributes = attrs;
  pd.primitiveTopology = PrimitiveTopology::TriangleList;

  // Standard back-face culling: front faces (outward normals toward camera) are rendered.
  // The Fresnel factor naturally concentrates the glow at grazing angles.
  pd.rasterization.cullMode = CullMode::Back;
  pd.rasterization.frontCounterClockwise = true;  // matches GlobeCamera Y-flip
  pd.rasterization.fillMode = FillMode::Solid;

  // No depth test/write — let Fresnel handle visibility; additive blend does the rest.
  pd.depthStencil.depthTestEnable  = false;
  pd.depthStencil.depthWriteEnable = false;

  // Additive blending: result = src_color + dst_color
  ColorBlendAttachmentState blend;
  blend.blendEnable          = true;
  blend.srcColorBlendFactor  = BlendFactor::One;
  blend.dstColorBlendFactor  = BlendFactor::One;
  blend.colorBlendOp         = BlendOp::Add;
  blend.srcAlphaBlendFactor  = BlendFactor::One;
  blend.dstAlphaBlendFactor  = BlendFactor::One;
  blend.alphaBlendOp         = BlendOp::Add;
  pd.colorBlendAttachments.push_back(blend);

  pd.renderPass    = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("AtmosphereRenderer: failed to create graphics pipeline");
    return false;
  }

  initialized_ = true;
  mesh_dirty_  = true;  // trigger mesh build on first update()
  spdlog::info("AtmosphereRenderer initialized");
  return true;
}

// ============================================================================
// Mesh generation
// ============================================================================

void AtmosphereRenderer::rebuildMesh(const glm::dvec3& rtc_origin) {
  std::vector<AtmoVertex> vertices;
  std::vector<uint32_t>   indices;

  vertices.reserve(static_cast<size_t>((kLatSegs + 1) * (kLonSegs + 1)));
  indices.reserve(static_cast<size_t>(kLatSegs * kLonSegs * 6));

  const double pi = glm::pi<double>();

  for (int lat = 0; lat <= kLatSegs; ++lat) {
    double theta = pi * static_cast<double>(lat) / static_cast<double>(kLatSegs);
    double sinT  = std::sin(theta);
    double cosT  = std::cos(theta);

    for (int lon = 0; lon <= kLonSegs; ++lon) {
      double phi  = 2.0 * pi * static_cast<double>(lon) / static_cast<double>(kLonSegs);
      double sinP = std::sin(phi);
      double cosP = std::cos(phi);

      // Position on the atmosphere sphere in ECEF
      glm::dvec3 ecef = {
          kAtmoRadius * sinT * cosP,
          kAtmoRadius * sinT * sinP,
          kAtmoRadius * cosT
      };

      AtmoVertex v;
      v.position = glm::vec3(ecef - rtc_origin);             // float32 RTC offset
      v.normal   = glm::vec3(glm::normalize(ecef));          // outward unit normal
      vertices.push_back(v);
    }
  }

  // Index generation — CCW winding (front face = outward normal)
  for (int lat = 0; lat < kLatSegs; ++lat) {
    for (int lon = 0; lon < kLonSegs; ++lon) {
      uint32_t tl = static_cast<uint32_t>( lat      * (kLonSegs + 1) + lon     );
      uint32_t tr = static_cast<uint32_t>( lat      * (kLonSegs + 1) + lon + 1 );
      uint32_t bl = static_cast<uint32_t>((lat + 1) * (kLonSegs + 1) + lon     );
      uint32_t br = static_cast<uint32_t>((lat + 1) * (kLonSegs + 1) + lon + 1 );

      indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
      indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
    }
  }

  // Upload to GPU
  {
    BufferDesc bd;
    bd.size             = vertices.size() * sizeof(AtmoVertex);
    bd.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    vertex_buffer_      = device_->createBuffer(bd);
    if (vertex_buffer_) {
      void* ptr = vertex_buffer_->map();
      std::memcpy(ptr, vertices.data(), bd.size);
      vertex_buffer_->unmap();
    }
  }
  {
    BufferDesc bd;
    bd.size             = indices.size() * sizeof(uint32_t);
    bd.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    index_buffer_       = device_->createBuffer(bd);
    if (index_buffer_) {
      void* ptr = index_buffer_->map();
      std::memcpy(ptr, indices.data(), bd.size);
      index_buffer_->unmap();
    }
  }

  index_count_       = static_cast<uint32_t>(indices.size());
  cached_rtc_origin_ = rtc_origin;
  mesh_dirty_        = false;
}

// ============================================================================
// update / render
// ============================================================================

void AtmosphereRenderer::update(const GlobeCamera& camera) {
  if (!initialized_) return;

  const glm::dvec3& origin = camera.getRTCOrigin().getOriginECEF();
  if (mesh_dirty_ || cached_rtc_origin_ != origin) {
    rebuildMesh(origin);
  }
}

void AtmosphereRenderer::render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect) {
  if (!initialized_ || !pipeline_ || index_count_ == 0) return;
  if (!vertex_buffer_ || !index_buffer_) return;

  // Altitude-based fade.
  // kAtmoRadius ≈ Earth * 1.025 → shell altitude ≈ 159 km.
  // kFadeAltNone is set ABOVE the shell so the effect is already at zero
  // before back-face culling kicks in — no abrupt pop, no haze at low altitude.
  constexpr double kFadeAltFull = 2'000'000.0;  // 2000 km — fully visible from space
  constexpr double kFadeAltNone =   200'000.0;  //  200 km — fully invisible when close
  double alt  = camera.getAltitude();
  double t    = std::clamp((alt - kFadeAltNone) / (kFadeAltFull - kFadeAltNone), 0.0, 1.0);
  float  fade = static_cast<float>(t * t * (3.0 - 2.0 * t));  // smoothstep
  if (fade < 0.002f) return;  // skip draw call when essentially transparent

  glm::mat4 proj = camera.getProjectionMatrix(aspect);
  glm::mat4 view = camera.getRTCViewMatrix();

  AtmoPushData push;
  push.mvp         = proj * view;
  push.cam_pos_rtc = glm::vec4(
      camera.getRTCOrigin().ecefToRTC(camera.getPositionECEF()), fade);

  const ShaderStageFlags both = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;

  cmd->bindPipeline(pipeline_);
  cmd->pushConstants(pipeline_layout_, both, 0u, sizeof(AtmoPushData), &push);
  cmd->bindVertexBuffer(0u, vertex_buffer_);
  cmd->bindIndexBuffer(index_buffer_);
  cmd->drawIndexed(index_count_);
}

}  // namespace hongtu::renderer
