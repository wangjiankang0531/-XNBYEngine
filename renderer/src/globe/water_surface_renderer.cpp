#include <hongtu/renderer/globe/water_surface_renderer.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/effects/polygon_renderer.hpp>  // triangulateFan
#include "water_shading_glsl.hpp"
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <cstring>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using namespace HongTu::Asset;

// ============================================================================
// Mesh build (pure — unit-tested)
// ============================================================================

std::vector<glm::vec3> buildWaterSurfaceRtcVertices(
    const std::vector<coordinate::Geodetic>& boundary,
    const coordinate::RTCOrigin& origin,
    const coordinate::Ellipsoid& ellipsoid) {
  if (boundary.size() < 3) return {};

  // Geodetic → ECEF (double) → RTC (float). The double-precision subtraction
  // happens inside ecefToRTC, so vertices near the origin keep full precision
  // (see feedback_geo_rtc_precision: RTC build + un-offset cancel exactly).
  std::vector<glm::vec3> ring;
  ring.reserve(boundary.size());
  for (const auto& geo : boundary) {
    ring.push_back(origin.ecefToRTC(coordinate::geodeticToECEF(geo, ellipsoid)));
  }

  // Same P0 fan contract as polygon fills; color is discarded (water color is
  // a per-body push constant, not per-vertex).
  auto fan = triangulateFan(ring, glm::vec4(1.0f));
  std::vector<glm::vec3> out;
  out.reserve(fan.size());
  for (const auto& v : fan) out.push_back(v.position);
  return out;
}

// ============================================================================
// Lifecycle
// ============================================================================

WaterSurfaceRenderer::~WaterSurfaceRenderer() {
  cleanup();
}

void WaterSurfaceRenderer::cleanup() {
  bodies_.clear();
  pipeline_.reset();
  pipeline_layout_.reset();
  fs_.reset();
  vs_.reset();
  initialized_ = false;
}

bool WaterSurfaceRenderer::init(RHIDevice* device, RHIRenderPassPtr render_pass) {
  if (!device || !render_pass) {
    spdlog::error("WaterSurfaceRenderer::init: null device or render_pass");
    return false;
  }
  device_ = device;

  ShaderCompiler compiler;

  const char* vs_src = R"glsl(
    layout(location = 0) in  vec3 inPos;
    layout(location = 0) out vec3 outWorldPos;  // RTC
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 sun_dir_time;
        vec4 cam_rtc;
        vec4 rtc_origin;
        vec4 color;
    } pc;
    void main() {
        outWorldPos = inPos;
        gl_Position = pc.mvp * vec4(inPos, 1.0);
    }
  )glsl";

  // Close-range water shading. Shares the water-GLSL primitives with the
  // global-ocean shaders but runs its own composition: the footprint IS the
  // water authority (mask = 1, no color classification), ripple cells are
  // metres-scale (the ocean's are 250 m), and the result alpha-blends over
  // the terrain underneath.
  const char* fs_src = R"glsl(
    layout(location = 0) in  vec3 inWorldPos;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 sun_dir_time;  // xyz = sun dir ECEF, w = time (s)
        vec4 cam_rtc;       // xyz = camera in RTC
        vec4 rtc_origin;    // xyz = RTC origin in ECEF
        vec4 color;         // rgb = water color, a = base opacity
    } pc;
    void main() {
        vec3 sunDir  = pc.sun_dir_time.xyz;
        float time   = pc.sun_dir_time.w;
        vec3 up      = normalize(inWorldPos + pc.rtc_origin.xyz);
        vec3 viewDir = normalize(pc.cam_rtc.xyz - inWorldPos);

        // Tangent frame for world-locked ripple.
        vec3 ref = abs(up.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 t = normalize(cross(up, ref));
        vec3 b = cross(up, t);
        vec2 q = vec2(dot(inWorldPos, t), dot(inWorldPos, b));

        // Ripple normal from three animated noise octaves: ~75 m swell (keeps
        // the reflection streaky at km distances where the fine cells go
        // sub-pixel), ~12 m and ~5 m chop for close-ups. Centered slopes;
        // shading only — the surface stays geometrically flat.
        float s0 = ht_vnoise(q * 0.013 + vec2(time * 0.18, -time * 0.11)) - 0.5;
        float s1 = ht_vnoise(q * 0.08  + vec2(time * 0.55,  time * 0.21)) - 0.5;
        float s2 = ht_vnoise(q * 0.21  - vec2(time * 0.34,  time * 0.47)) - 0.5;
        vec3 N = normalize(up + t * (s0 * 0.20 + s1 * 0.22 + s2 * 0.10)
                              + b * (s0 * 0.15 + s2 * 0.20 - s1 * 0.08));

        float daylit = smoothstep(-0.05, 0.18, dot(up, sunDir));

        // Base water color, dimmed at night.
        vec3 base = pc.color.rgb * (0.22 + 0.78 * daylit);

        // Fresnel sky reflection on the rippled normal. F is clamped well
        // below 1: at grazing angles an unclamped Schlick goes full sky and
        // the water reads as a featureless milky sheet (wave roughness keeps
        // real water's effective reflectivity lower).
        float ndv = max(dot(N, viewDir), 0.0);
        float F = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
        F = min(F, 0.55);
        vec3 sky = ht_skyReflect(reflect(-viewDir, N), up, sunDir);
        vec3 color = mix(base, sky * 0.92, F * daylit);

        // Sharp sun glint on the rippled normal (close range — the ripple
        // itself scatters the highlight; no extra sparkle noise needed).
        vec3 H = normalize(viewDir + sunDir);
        float glint = pow(max(dot(N, H), 0.0), 300.0) * 2.0 * daylit;
        color += vec3(1.0, 0.97, 0.9) * glint;

        // Grazing views read more reflective → somewhat more opaque.
        float alpha = clamp(pc.color.a + F * 0.30, 0.0, 0.95);
        outColor = vec4(color, alpha);
    }
  )glsl";

  const std::string fs_full = std::string(kWaterGlsl) + fs_src;
  auto vs_res = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
  auto fs_res = compiler.compileGLSLToSPIRV(fs_full, "main", "fragment");
  if (!vs_res.success || !fs_res.success) {
    spdlog::error("WaterSurfaceRenderer: shader compile failed: vert='{}' frag='{}'",
                  vs_res.errorMessage, fs_res.errorMessage);
    return false;
  }

  ShaderDesc vsd;
  vsd.stage     = ShaderStageFlagBits::Vertex;
  vsd.spirvCode = vs_res.spirvCode;
  vs_ = device_->createShader(vsd);

  ShaderDesc fsd;
  fsd.stage     = ShaderStageFlagBits::Fragment;
  fsd.spirvCode = fs_res.spirvCode;
  fs_ = device_->createShader(fsd);

  if (!vs_ || !fs_) {
    spdlog::error("WaterSurfaceRenderer: failed to create shader modules");
    return false;
  }

  {
    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;
    pcr.offset = 0u;
    pcr.size   = sizeof(WaterPushData);  // 128 bytes
    ld.pushConstantRanges.push_back(pcr);
    pipeline_layout_ = device_->createPipelineLayout(ld);
    if (!pipeline_layout_) {
      spdlog::error("WaterSurfaceRenderer: failed to create pipeline layout");
      return false;
    }
  }

  VertexInputBindingDescription bind;
  bind.binding     = 0u;
  bind.stride      = sizeof(glm::vec3);
  bind.perInstance = false;

  std::vector<VertexInputAttributeDescription> attrs(1);
  attrs[0] = {0u, 0u, Format::R32G32B32_SFLOAT, 0u};

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings   = {bind};
  pd.vertexInput.attributes = attrs;
  pd.primitiveTopology = PrimitiveTopology::TriangleList;

  // Fan winding is unspecified (caller's boundary order) and the surface may
  // be seen from below at grazing angles — no culling.
  pd.rasterization.cullMode = CullMode::None;
  pd.rasterization.fillMode = FillMode::Solid;

  // Transparent surface over terrain: depth-tested against the opaque globe,
  // but no depth write (water bodies rarely overlap; skip the sort problem).
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = false;
  pd.depthStencil.depthCompareOp   = CompareOp::LessOrEqual;

  ColorBlendAttachmentState blend;
  blend.blendEnable         = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp        = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.alphaBlendOp        = BlendOp::Add;
  pd.colorBlendAttachments.push_back(blend);

  pd.renderPass     = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("WaterSurfaceRenderer: failed to create graphics pipeline");
    return false;
  }

  initialized_ = true;
  spdlog::info("WaterSurfaceRenderer initialized");
  return true;
}

// ============================================================================
// Water bodies
// ============================================================================

int WaterSurfaceRenderer::addWaterBody(std::vector<coordinate::Geodetic> boundary,
                                       const WaterStyle& style) {
  if (boundary.size() < 3) {
    spdlog::warn("WaterSurfaceRenderer::addWaterBody: boundary needs >= 3 vertices");
    return -1;
  }
  const int id = next_id_++;
  Body body;
  body.boundary = std::move(boundary);
  body.style    = style;
  bodies_.emplace(id, std::move(body));
  return id;
}

void WaterSurfaceRenderer::removeWaterBody(int id) {
  bodies_.erase(id);
}

void WaterSurfaceRenderer::clearWaterBodies() {
  bodies_.clear();
}

void WaterSurfaceRenderer::rebuildBody(Body& body, const coordinate::RTCOrigin& origin) {
  auto verts = buildWaterSurfaceRtcVertices(body.boundary, origin);
  body.vertex_count = static_cast<u32>(verts.size());
  body.dirty        = false;
  if (verts.empty()) {
    body.vbo.reset();
    return;
  }
  BufferDesc bd;
  bd.size             = verts.size() * sizeof(glm::vec3);
  bd.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  body.vbo            = device_->createBuffer(bd);
  if (body.vbo) {
    void* ptr = body.vbo->map();
    std::memcpy(ptr, verts.data(), bd.size);
    body.vbo->unmap();
  }
}

// ============================================================================
// update / render
// ============================================================================

void WaterSurfaceRenderer::update(const GlobeCamera& camera) {
  if (!initialized_ || bodies_.empty()) return;

  const glm::dvec3& origin = camera.getRTCOrigin().getOriginECEF();
  const bool origin_moved  = (cached_rtc_origin_ != origin);
  if (origin_moved) cached_rtc_origin_ = origin;

  for (auto& [id, body] : bodies_) {
    if (origin_moved || body.dirty) rebuildBody(body, camera.getRTCOrigin());
  }
}

void WaterSurfaceRenderer::render(RHICommandBufferPtr cmd, const GlobeCamera& camera,
                                  f32 aspect, const glm::vec3& sun_dir_ecef,
                                  f32 time_s) {
  if (!initialized_ || !pipeline_ || bodies_.empty()) return;

  glm::mat4 proj = camera.getProjectionMatrix(aspect);
  glm::mat4 view = camera.getRTCViewMatrix();

  WaterPushData push;
  push.mvp          = proj * view;
  push.sun_dir_time = glm::vec4(sun_dir_ecef, time_s);
  push.cam_rtc      = glm::vec4(
      camera.getRTCOrigin().ecefToRTC(camera.getPositionECEF()), 0.0f);
  push.rtc_origin   = glm::vec4(glm::vec3(camera.getRTCOrigin().getOriginECEF()), 0.0f);

  const ShaderStageFlags both = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;

  bool bound = false;
  for (auto& [id, body] : bodies_) {
    if (!body.vbo || body.vertex_count == 0) continue;
    if (!bound) {
      cmd->bindPipeline(pipeline_);
      bound = true;
    }
    push.color = body.style.color;
    cmd->pushConstants(pipeline_layout_, both, 0u, sizeof(WaterPushData), &push);
    cmd->bindVertexBuffer(0u, body.vbo);
    cmd->draw(body.vertex_count, 1u, 0u, 0u);
  }
}

}  // namespace hongtu::renderer
