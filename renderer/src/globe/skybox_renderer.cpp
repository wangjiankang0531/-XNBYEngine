#include <hongtu/renderer/globe/skybox_renderer.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using namespace HongTu::Asset;

// ============================================================================
// Destructor / cleanup
// ============================================================================

SkyboxRenderer::~SkyboxRenderer() {
  cleanup();
}

void SkyboxRenderer::cleanup() {
  pipeline_.reset();
  pipeline_layout_.reset();
  fragment_shader_.reset();
  vertex_shader_.reset();
  initialized_ = false;
}

// ============================================================================
// Public: init
// ============================================================================

bool SkyboxRenderer::init(RHIDevice* device, RHIRenderPassPtr render_pass) {
  if (!device || !render_pass) {
    spdlog::error("SkyboxRenderer::init: null device or render_pass");
    return false;
  }
  device_ = device;

  if (!createPipeline(render_pass)) {
    spdlog::error("SkyboxRenderer::init: failed to create pipeline");
    return false;
  }

  initialized_ = true;
  spdlog::info("SkyboxRenderer initialized (procedural star field)");
  return true;
}

// ============================================================================
// Pipeline
// ============================================================================

bool SkyboxRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Full-screen triangle — no vertex buffer, positions generated via gl_VertexIndex.
  // Unprojects NDC corners to world-space ray direction via inverse VP matrix.
  const char* vs_src = R"glsl(
    layout(push_constant) uniform PC {
        mat4 inv_proj_view;
        vec4 camPos;   // xyz = ECEF camera pos, w = earth radius
        vec4 sun;      // xyz = direction to sun (ECEF)
    } pc;
    layout(location = 0) out vec3 outDir;
    void main() {
        vec2 ndc = vec2(
            (gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
            (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0
        );
        vec4 worldPos = pc.inv_proj_view * vec4(ndc, 1.0, 1.0);
        outDir = worldPos.xyz / worldPos.w;
        gl_Position = vec4(ndc, 1.0, 1.0);
    }
  )glsl";

  // Procedural star field — no external texture required.
  // Stars are randomly distributed on the celestial sphere using a hash grid.
  // A faint Milky Way band runs along the equatorial plane.
  const char* fs_src = R"glsl(
    layout(location = 0) in  vec3 inDir;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4 inv_proj_view;
        vec4 camPos;   // xyz = ECEF camera pos, w = earth radius
        vec4 sun;      // xyz = direction to sun (ECEF)
    } pc;

    float hash(vec2 p) {
        return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
    }

    void main() {
        vec3 d = normalize(inDir);
        const float PI = 3.14159265;

        // Convert to spherical UV for a uniform star grid
        float u = atan(d.z, d.x) * (0.5 / PI) + 0.5;
        float v = asin(clamp(d.y, -1.0, 1.0)) * (1.0 / PI) + 0.5;

        // Star grid: each cell may contain one star
        const float SCALE = 250.0;
        vec2 uv_s = vec2(u, v) * SCALE;
        vec2 cell  = floor(uv_s);
        vec2 local = fract(uv_s);

        float presence  = hash(cell);
        float ox        = hash(cell + vec2(37.3, 0.0));
        float oy        = hash(cell + vec2(0.0, 73.1));
        float szVar     = hash(cell + vec2(117.9, 0.0));
        float colorVar  = hash(cell + vec2(157.3, 0.0));

        // Dark deep-space background
        vec3 color = vec3(0.0, 0.0, 0.004);

        // Draw star if this cell is selected (~7% probability)
        if (presence > 0.93) {
            vec2  center     = vec2(ox, oy);
            float dist       = length(local - center);
            float radius     = 0.025 + 0.055 * szVar;
            float brightness = max(0.0, 1.0 - dist / radius);
            brightness = brightness * brightness;
            // Vary brightness by how "selected" the cell is
            brightness *= 0.4 + 0.6 * (presence - 0.93) / 0.07;

            // Star color spectrum: warm (yellowish) to cool (bluish-white)
            vec3 warmStar = vec3(1.0, 0.95, 0.80);
            vec3 coolStar = vec3(0.85, 0.90, 1.00);
            color += mix(warmStar, coolStar, colorVar) * brightness;
        }

        // Faint Milky Way band — subtle purple-blue smear along v ≈ 0.5
        float band = exp(-pow((v - 0.5) * 13.0, 2.0)) * 0.022;
        color += vec3(0.45, 0.35, 0.70) * band;

        // --- Atmospheric blue sky (from inside the atmosphere) -------------
        // Blends in as the camera descends; fades back to the star field as it
        // climbs into space. Gives "blue sky + white clouds" at low altitude.
        float Re   = pc.camPos.w;
        float alt  = length(pc.camPos.xyz) - Re;
        float skyAmt = 1.0 - smoothstep(0.0, 100000.0, alt);   // blue below ~100 km
        if (skyAmt > 0.001) {
            vec3  up    = normalize(pc.camPos.xyz);
            float cosUp = dot(d, up);
            float hgt   = clamp(cosUp, 0.0, 1.0);
            vec3  zenith  = vec3(0.16, 0.40, 0.82);
            vec3  horizon = vec3(0.72, 0.83, 0.95);
            vec3  sky = mix(horizon, zenith, pow(hgt, 0.55));
            vec3  sd  = normalize(pc.sun.xyz);
            float sdot = max(dot(d, sd), 0.0);
            sky += vec3(1.0, 0.85, 0.6) * pow(sdot, 12.0) * 0.25;             // sun glow
            sky += vec3(1.0, 0.96, 0.88) * smoothstep(0.9994, 0.9999, sdot) * 4.0; // sun disc
            sky *= smoothstep(-0.06, 0.06, cosUp);   // fade just under the horizon
            color = mix(color, sky, skyAmt);
        }

        outColor = vec4(color, 1.0);
    }
  )glsl";

  auto vs_result = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
  auto fs_result = compiler.compileGLSLToSPIRV(fs_src, "main", "fragment");

  if (!vs_result.success || !fs_result.success) {
    spdlog::error("SkyboxRenderer: shader compilation failed: vert='{}' frag='{}'",
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
    spdlog::error("SkyboxRenderer: failed to create shader modules");
    return false;
  }

  // Pipeline layout: only push constant (inv_proj_view, 64 bytes, vertex stage)
  PipelineLayoutDesc layout_desc;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0u;
  pcr.size   = sizeof(glm::mat4) + sizeof(glm::vec4) * 2;  // ipv + camPos + sun
  layout_desc.pushConstantRanges.push_back(pcr);

  pipeline_layout_ = device_->createPipelineLayout(layout_desc);
  if (!pipeline_layout_) {
    spdlog::error("SkyboxRenderer: failed to create pipeline layout");
    return false;
  }

  // Graphics pipeline — no vertex input, full-screen triangle
  GraphicsPipelineDesc pd;
  pd.shaders           = {vertex_shader_, fragment_shader_};
  pd.primitiveTopology = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode = CullMode::None;
  pd.rasterization.fillMode = FillMode::Solid;

  // Depth write disabled; test LESS_OR_EQUAL so skybox stays behind geometry
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = false;
  pd.depthStencil.depthCompareOp   = CompareOp::LessOrEqual;

  pd.renderPass    = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) {
    spdlog::error("SkyboxRenderer: failed to create graphics pipeline");
    return false;
  }

  return true;
}

// ============================================================================
// Render
// ============================================================================

void SkyboxRenderer::render(RHICommandBufferPtr cmd, const glm::mat4& view_rot,
                             const glm::mat4& proj, const glm::vec3& cam_pos_ecef,
                             const glm::vec3& sun_dir) {
  if (!initialized_) return;

  cmd->bindPipeline(pipeline_);

  struct { glm::mat4 inv_proj_view; glm::vec4 camPos; glm::vec4 sun; } push;
  push.inv_proj_view = glm::inverse(proj * view_rot);
  push.camPos = glm::vec4(cam_pos_ecef, 6378137.0f);   // WGS84 semi-major
  push.sun    = glm::vec4(glm::normalize(sun_dir), 0.0f);
  cmd->pushConstants(pipeline_layout_,
                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                         | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
                     0u, sizeof(push), &push);

  // 3 vertices — no vertex buffer, positions generated via gl_VertexIndex
  cmd->draw(3u, 1u, 0u, 0u);
}

}  // namespace hongtu::renderer
