#include <hongtu/renderer/effects/infinite_grid_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool InfiniteGridPass::initialize(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;
  return createPipeline(render_pass);
}

bool InfiniteGridPass::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Fullscreen-triangle pass-through. Same trick as ShockwavePass /
  // VolumetricPass — one triangle covering NDC [-1,3]x[-1,3].
  const char* vsrc = R"glsl(
    layout(location = 0) out vec2 vNDC;
    void main() {
      vec2 ndc = vec2(
          (gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
          (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0);
      vNDC        = ndc;
      gl_Position = vec4(ndc, 1.0, 1.0);  // depth overwritten in FS
    }
  )glsl";

  // Fragment: ray vs z=0 plane → two-layer screen-space grid + axes.
  const char* fsrc = R"glsl(
    layout(push_constant) uniform PC {
      mat4 view_proj;
      mat4 inv_view_proj;
      vec4 camera_world;    // .xyz cam pos, .w fade_distance
      vec4 fine_color;
      vec4 major_color;
      vec4 x_axis_color;
      vec4 y_axis_color;
      vec4 scales;          // .x fine_s, .y major_s, .z fade_start_px, .w fade_end_px
    } pc;

    layout(location = 0) in  vec2 vNDC;
    layout(location = 0) out vec4 outColor;

    // Returns line alpha for grid with cell size `scale` at world point P.
    // Output is 1 at a grid line center, falls off to 0 within one pixel.
    float gridAlpha(vec2 P, float scale) {
      vec2 coord = P / scale;
      vec2 deriv = fwidth(coord);
      vec2 grid  = abs(fract(coord - 0.5) - 0.5) / max(deriv, vec2(1e-6));
      float line = min(grid.x, grid.y);
      return 1.0 - min(line, 1.0);
    }

    void main() {
      // 1. Unproject NDC → world ray
      vec4 nh = pc.inv_view_proj * vec4(vNDC, 0.0, 1.0);
      vec4 fh = pc.inv_view_proj * vec4(vNDC, 1.0, 1.0);
      vec3 ro = nh.xyz / nh.w;
      vec3 rf = fh.xyz / fh.w;
      vec3 rd = rf - ro;

      // 2. Intersect z = 0 plane
      if (abs(rd.z) < 1e-6) discard;
      float t = -ro.z / rd.z;
      if (t < 0.0 || t > 1.0) discard;   // behind near, or past far
      vec3 P = ro + t * rd;

      // 3. Pixel size (world meters) at this fragment — drives both line
      //    width and fine-layer fade.
      vec2 pix_xy = fwidth(P.xy);
      float pixel_size_m = max(pix_xy.x, pix_xy.y);

      // 4. Two grid layers
      float a_fine  = gridAlpha(P.xy, pc.scales.x);
      float a_major = gridAlpha(P.xy, pc.scales.y);

      // 5. Fade fine grid when cells get sub-pixel
      float fade_lo  = pc.scales.z;
      float fade_hi  = pc.scales.w;
      // pixel_size relative to fine cell size: cells_per_pixel = ps / fine_s
      // cell_px = fine_s / ps  → bigger = more pixels per cell = keep visible
      float cell_px  = pc.scales.x / max(pixel_size_m, 1e-6);
      float fine_visible = smoothstep(fade_hi, fade_lo, cell_px);
      // smoothstep(hi, lo) inverts so result is 1 when cell_px >= fade_lo,
      // 0 when cell_px <= fade_hi (typical fade_lo=4, fade_hi=2)
      a_fine *= fine_visible;

      // 6. Axis accents — distance-to-axis in pixels using fwidth
      float ax_px = abs(P.y) / max(fwidth(P.y), 1e-6);  // X-axis: P.y = 0
      float ay_px = abs(P.x) / max(fwidth(P.x), 1e-6);  // Y-axis: P.x = 0
      float a_x_axis = 1.0 - smoothstep(0.5, 1.5, ax_px);
      float a_y_axis = 1.0 - smoothstep(0.5, 1.5, ay_px);

      // 7. Compose layers — fine, then major, then axes on top
      vec4 col = vec4(0.0);
      col = mix(col, vec4(pc.fine_color.rgb,  pc.fine_color.a),  a_fine);
      col = mix(col, vec4(pc.major_color.rgb, pc.major_color.a), a_major);
      col = mix(col, pc.x_axis_color, a_x_axis);
      col = mix(col, pc.y_axis_color, a_y_axis);

      // 8. Distance fade — softly fade the whole grid out far from camera
      float dist = length(P - pc.camera_world.xyz);
      float fade = 1.0 - smoothstep(pc.camera_world.w * 0.5,
                                     pc.camera_world.w, dist);
      col.a *= fade;

      if (col.a < 0.001) discard;
      outColor = col;

      // 9. Write depth at the intersection so opaque geometry occludes us.
      vec4 clip = pc.view_proj * vec4(P, 1.0);
      gl_FragDepth = clip.z / clip.w;  // Vulkan NDC depth ∈ [0,1]
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) {
    spdlog::error("InfiniteGridPass: shader fail vs='{}' fs='{}'",
                  vr.errorMessage, fr.errorMessage);
    return false;
  }
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc pll;
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0; pcr.size = sizeof(PushConstants);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  // Standard alpha blend — grid lines tint the background.
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.alphaBlendOp = BlendOp::Add;

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  // depth-test ON so opaque geometry occludes the grid; depth-write ON so
  // the grid plane occludes things drawn later (annotations, particles)
  // that fall behind it.
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass     = render_pass;
  pd.pipelineLayout = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void InfiniteGridPass::record(const RHICommandBufferPtr& cmd, const Params& p) {
  if (!pipeline_) return;

  PushConstants pc{};
  pc.view_proj     = p.view_proj;
  pc.inv_view_proj = glm::inverse(p.view_proj);
  pc.camera_world  = glm::vec4(p.camera_world_pos, p.fade_distance);
  pc.fine_color    = p.fine_color;
  pc.major_color   = p.major_color;
  pc.x_axis_color  = p.x_axis_color;
  pc.y_axis_color  = p.y_axis_color;
  pc.scales        = glm::vec4(p.fine_scale_m, p.major_scale_m,
                                 p.fine_fade_start_px, p.fine_fade_end_px);

  cmd->bindPipeline(pipeline_);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->draw(3u, 1u, 0u, 0u);
}

}  // namespace hongtu::renderer
