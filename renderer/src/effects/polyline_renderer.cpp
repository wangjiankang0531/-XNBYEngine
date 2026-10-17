#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

// =============================================================================
// Pure ribbon math
// =============================================================================
//
// Approach: for each input point, compute a "side" vector perpendicular to
// both the polyline direction and the view direction (camera → point). The
// ribbon's two edge vertices at point `i` are `point ± half_width * side`.
//
// Edge cases:
//   - first / last point only have one neighbour direction (handled by
//     reusing it for the "average")
//   - degenerate segment (zero-length) is treated as a fallback: just use
//     a world-up bias to avoid producing NaN
//   - closed loop: wrap neighbour indices

namespace {

inline glm::vec3 safeNormalize(const glm::vec3& v) {
  float len_sq = glm::dot(v, v);
  if (len_sq < 1e-12f) return glm::vec3(0.0f);
  return v * (1.0f / std::sqrt(len_sq));
}

// Compute the side offset at point i. `dir_in` and `dir_out` are unit
// vectors of the incoming / outgoing segments (zero for non-existent
// neighbours). `view_dir` is the unit vector from camera to point i.
glm::vec3 ribbonSide(const glm::vec3& dir_in, const glm::vec3& dir_out,
                     const glm::vec3& view_dir) {
  glm::vec3 avg = dir_in + dir_out;
  glm::vec3 tan = safeNormalize(avg);
  if (glm::length(tan) < 1e-6f) {
    // Both neighbours zero — degenerate. Fall back to world-up perpendicular.
    tan = safeNormalize(glm::cross(view_dir, glm::vec3(0.0f, 0.0f, 1.0f)));
    if (glm::length(tan) < 1e-6f) tan = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  glm::vec3 side = glm::cross(tan, view_dir);
  glm::vec3 n = safeNormalize(side);
  if (glm::length(n) < 1e-6f) {
    // Tangent parallel to view dir (ribbon looking straight down its length).
    // Pick any perpendicular to the view.
    glm::vec3 alt = std::abs(view_dir.z) > 0.9f
                      ? glm::vec3(1.0f, 0.0f, 0.0f)
                      : glm::vec3(0.0f, 0.0f, 1.0f);
    n = safeNormalize(glm::cross(view_dir, alt));
  }
  return n;
}

}  // namespace

std::vector<glm::vec3>
buildArcPolyline(const std::vector<glm::vec3>& points, float height_ratio,
                 int seg_per_span) {
  if (points.size() < 2 || height_ratio <= 0.0f) return points;
  if (seg_per_span < 2) seg_per_span = 2;
  std::vector<glm::vec3> out;
  out.reserve((points.size() - 1) * seg_per_span + 1);
  out.push_back(points[0]);
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    const glm::vec3 a = points[i];
    const glm::vec3 b = points[i + 1];
    const float h = glm::length(b - a) * height_ratio;
    for (int k = 1; k <= seg_per_span; ++k) {
      const float t = static_cast<float>(k) / seg_per_span;
      glm::vec3 p = a + (b - a) * t;
      p.z += 4.0f * h * t * (1.0f - t);  // 抛物线,峰在 t=0.5
      out.push_back(p);
    }
  }
  return out;
}

std::vector<RibbonVertex>
expandPolylineRibbon(const std::vector<glm::vec3>& points,
                     float half_width,
                     const glm::vec3& camera_pos,
                     const glm::vec4& color,
                     bool closed,
                     bool  arrow_head,
                     bool  arrow_tail,
                     float arrow_size,
                     bool  flat) {
  std::vector<RibbonVertex> out;
  if (points.size() < 2) return out;
  // 横铺 ribbon 用世界上方向求侧向(水平躺地);朝相机用 view_dir。
  const glm::vec3 kWorldUp(0.0f, 0.0f, 1.0f);

  // 箭头几何:线身端点拉回到箭头底部,箭头三角独占尖端 —— 粗线也不会被
  // 线身盖住而重叠;箭头大小随箭头长(不随线宽缩放),细粗线都清晰。
  const float full_w = half_width * 2.0f;
  const float a_len  = (!closed && (arrow_head || arrow_tail))
      ? (arrow_size > 0.0f ? arrow_size : std::max(full_w * 2.4f, half_width * 5.0f))
      : 0.0f;
  const float half_a = a_len * 0.72f;   // 箭头半宽:全宽 ≈ 1.44·a_len,明显比线宽

  std::vector<glm::vec3> rp = points;   // 线身顶点(端点可能拉回)
  glm::vec3 head_tip, head_dir, tail_tip, tail_dir;
  bool do_head = false, do_tail = false;
  if (a_len > 0.0f && arrow_head) {
    head_tip = points.back();
    head_dir = safeNormalize(points[points.size() - 1] - points[points.size() - 2]);
    const float seg = glm::length(points[points.size() - 1] - points[points.size() - 2]);
    rp.back() = head_tip - head_dir * std::min(a_len, seg * 0.9f);
    do_head = true;
  }
  if (a_len > 0.0f && arrow_tail) {
    tail_tip = points.front();
    tail_dir = safeNormalize(points[1] - points[0]);  // 沿线身向前
    const float seg = glm::length(points[1] - points[0]);
    rp.front() = tail_tip + tail_dir * std::min(a_len, seg * 0.9f);
    do_tail = true;
  }
  const size_t n = rp.size();

  // Per-point side vectors(L/R offsets,无 quad 扭曲)。
  std::vector<glm::vec3> sides(n);
  for (size_t i = 0; i < n; ++i) {
    glm::vec3 dir_in(0.0f), dir_out(0.0f);
    if (i > 0)        dir_in  = safeNormalize(rp[i] - rp[i - 1]);
    else if (closed)  dir_in  = safeNormalize(rp[0] - rp[n - 1]);
    if (i + 1 < n)    dir_out = safeNormalize(rp[i + 1] - rp[i]);
    else if (closed)  dir_out = safeNormalize(rp[0] - rp[n - 1]);
    if (flat) {
      // 水平躺地:侧向 = 切线 × 世界上,始终在水平面内,掠射角不入地。
      glm::vec3 tan = safeNormalize(dir_in + dir_out);
      if (glm::length(tan) < 1e-6f) tan = glm::vec3(1.0f, 0.0f, 0.0f);
      sides[i] = safeNormalize(glm::cross(tan, kWorldUp)) * half_width;
    } else {
      glm::vec3 view_dir = safeNormalize(rp[i] - camera_pos);
      sides[i] = ribbonSide(dir_in, dir_out, view_dir) * half_width;
    }
  }
  std::vector<float> arc(n, 0.0f);
  for (size_t i = 1; i < n; ++i) arc[i] = arc[i - 1] + glm::length(rp[i] - rp[i - 1]);

  const size_t seg_count = closed ? n : (n - 1);
  out.reserve(seg_count * 6 + 6);
  for (size_t s = 0; s < seg_count; ++s) {
    const size_t i = s, j = (s + 1) % n;
    const float ai = arc[i];
    const float aj = (closed && s == seg_count - 1)
                        ? arc[n - 1] + glm::length(rp[0] - rp[n - 1]) : arc[j];
    const glm::vec3 L0 = rp[i] + sides[i], R0 = rp[i] - sides[i];
    const glm::vec3 L1 = rp[j] + sides[j], R1 = rp[j] - sides[j];
    out.push_back({L0, color, ai, +1.0f});
    out.push_back({R0, color, ai, -1.0f});
    out.push_back({L1, color, aj, +1.0f});
    out.push_back({R0, color, ai, -1.0f});
    out.push_back({R1, color, aj, -1.0f});
    out.push_back({L1, color, aj, +1.0f});
  }

  // 箭头三角(原始尖端;side=0 / arc=-1 → 永远实心,不受虚线/分带影响)。
  auto emitHead = [&](const glm::vec3& tip, const glm::vec3& dir) {
    const glm::vec3 perp = flat ? kWorldUp : safeNormalize(tip - camera_pos);
    const glm::vec3 side = safeNormalize(glm::cross(dir, perp));
    const glm::vec3 base = tip - dir * a_len;
    out.push_back({tip,                 color, -1.0f, 0.0f});
    out.push_back({base + side * half_a, color, -1.0f, 0.0f});
    out.push_back({base - side * half_a, color, -1.0f, 0.0f});
  };
  if (do_head) emitHead(head_tip, head_dir);
  if (do_tail) emitHead(tail_tip, -tail_dir);  // 首端箭头指向线外

  return out;
}

// =============================================================================
// PolylineRenderer
// =============================================================================

bool PolylineRenderer::initialize(RHIDevice* device,
                                  RHIRenderPassPtr scene_render_pass) {
  device_ = device;
  if (!createPatternAtlas()) return false;
  return createPipeline(scene_render_pass);
}

bool PolylineRenderer::createPatternAtlas() {
  // 4 行图案,每行 W×ROW。U 方向铺(一个周期 = PERIOD px)。
  constexpr int W = 256, ROW = 64, ROWS = 4, H = ROW * ROWS;
  std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
  auto set = [&](int x, int y, uint8_t a) {
    uint8_t* p = &px[(static_cast<size_t>(y) * W + x) * 4];
    p[0] = p[1] = p[2] = 255; p[3] = a;  // 白 RGB,alpha 携图案(着色器乘线色)
  };
  for (int x = 0; x < W; ++x) {
    const float u = static_cast<float>(x) / W;     // 0..1
    for (int ly = 0; ly < ROW; ++ly) {
      const float v = static_cast<float>(ly) / (ROW - 1);  // 0..1 跨线宽
      const float c = std::abs(v - 0.5f) * 2.0f;           // 0 中线..1 缘
      // 行0 箭头流:">" V 形,沿 +U 指向。
      {
        const float ph = std::fmod(u * 6.0f + c * 0.5f, 1.0f);
        uint8_t a = (ph < 0.45f) ? 255 : 0;
        if (c > 0.92f) a = 0;  // 收边
        set(x, 0 * ROW + ly, a);
      }
      // 行1 虚块流:占空比 0.6 的方块,满宽。
      set(x, 1 * ROW + ly, (std::fmod(u * 5.0f, 1.0f) < 0.6f && c < 0.95f) ? 255 : 0);
      // 行2 圆点流:每周期一个圆点(中线)。
      {
        const float pu = std::fmod(u * 5.0f, 1.0f) - 0.5f;
        const float d  = std::sqrt(pu * pu + (v - 0.5f) * (v - 0.5f) * 1.0f);
        set(x, 2 * ROW + ly, d < 0.28f ? 255 : 0);
      }
      // 行3 实心光带:中间亮、缘柔(给流动光带打底)。
      set(x, 3 * ROW + ly, static_cast<uint8_t>(std::clamp(255.0f * (1.0f - c * c), 0.0f, 255.0f)));
    }
  }

  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {static_cast<uint32_t>(W), static_cast<uint32_t>(H), 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R8G8B8A8_UNORM;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  pattern_tex_ = device_->createTexture(td);
  if (!pattern_tex_) return false;
  RHIDevice::MipData m0;
  m0.width = W; m0.height = H; m0.data = px.data(); m0.dataSize = px.size();
  device_->uploadTextureData(pattern_tex_, {m0}, 0u);
  pattern_view_ = pattern_tex_->createView();
  if (!pattern_view_) return false;

  SamplerDesc sd;
  sd.magFilter    = SamplerFilter::Linear;
  sd.minFilter    = SamplerFilter::Linear;
  sd.mipmapMode   = SamplerMipmapMode::Linear;
  sd.addressModeU = SamplerAddressMode::Repeat;       // 沿弧长平铺
  sd.addressModeV = SamplerAddressMode::ClampToEdge;  // 跨宽不重复
  sd.addressModeW = SamplerAddressMode::ClampToEdge;
  pattern_sampler_ = device_->createSampler(sd);
  if (!pattern_sampler_) return false;

  DescriptorSetLayoutDesc dl;
  DescriptorSetLayoutBinding lb;
  lb.binding = 0u; lb.descriptorCount = 1u;
  lb.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  lb.descriptorType = DescriptorType::CombinedImageSampler;
  dl.bindings.push_back(lb);
  pattern_set_layout_ = device_->createDescriptorSetLayout(dl);
  if (!pattern_set_layout_) return false;
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding = 0u; db.type = DescriptorType::CombinedImageSampler;
  db.textureView = pattern_view_; db.sampler = pattern_sampler_;
  dsd.bindings.push_back(db);
  pattern_set_ = device_->createDescriptorSet(pattern_set_layout_, dsd);
  return pattern_set_ != nullptr;
}

int PolylineRenderer::getCustomPatternSlot(const std::string& key) const {
  auto it = custom_index_.find(key);
  return it == custom_index_.end() ? -1 : it->second;
}

int PolylineRenderer::ensureCustomPattern(const std::string& key,
                                          const uint8_t* rgba, int w, int h) {
  auto it = custom_index_.find(key);
  if (it != custom_index_.end()) return it->second;
  if (!device_ || !rgba || w <= 0 || h <= 0 ||
      !pattern_set_layout_ || !pattern_sampler_)
    return -1;
  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
  td.mipLevels   = 1u; td.arrayLayers = 1u;
  td.format      = Format::R8G8B8A8_UNORM;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  auto tex = device_->createTexture(td);
  if (!tex) return -1;
  RHIDevice::MipData m0;
  m0.width = static_cast<uint32_t>(w); m0.height = static_cast<uint32_t>(h);
  m0.data = const_cast<uint8_t*>(rgba);
  m0.dataSize = static_cast<size_t>(w) * h * 4u;
  device_->uploadTextureData(tex, {m0}, 0u);
  auto view = tex->createView();
  if (!view) return -1;
  DescriptorSetDesc dsd;
  DescriptorBinding db;
  db.binding = 0u; db.type = DescriptorType::CombinedImageSampler;
  db.textureView = view; db.sampler = pattern_sampler_;
  dsd.bindings.push_back(db);
  auto set = device_->createDescriptorSet(pattern_set_layout_, dsd);
  if (!set) return -1;
  int slot = static_cast<int>(custom_set_.size());
  custom_tex_.push_back(std::move(tex));
  custom_view_.push_back(std::move(view));
  custom_set_.push_back(std::move(set));
  custom_index_[key] = slot;
  return slot;
}

bool PolylineRenderer::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Coloured-line shader with optional dash pattern. View-projection +
  // dash params are push constants; per-vertex colour + arc_length come
  // from the ribbon VBO. arc_length < 0 marks arrowhead geometry, which
  // the fragment shader always treats as solid.
  const char* vsrc = R"glsl(
    layout(location = 0) in vec3  inPos;
    layout(location = 1) in vec4  inColor;
    layout(location = 2) in float inArcLength;
    layout(location = 3) in float inSide;
    layout(push_constant) uniform PC {
      mat4 view_proj;
      vec4 dash_params;
      vec4 line_params;
      vec4 outline_color;
      vec4 tex_params;
    } pc;
    layout(location = 0) out vec4  vColor;
    layout(location = 1) out float vArcLength;
    layout(location = 2) out float vSide;
    void main() {
      gl_Position = pc.view_proj * vec4(inPos, 1.0);
      vColor      = inColor;
      vArcLength  = inArcLength;
      vSide       = inSide;
    }
  )glsl";

  // line_params.x = fx 位掩码(bit0 描边, bit1 发光, bit2 图案/流动)—— 可叠加。
  // line_params.y = glow/core 芯宽   .z = time   .w = flow_speed
  const char* fsrc = R"glsl(
    layout(location = 0) in  vec4  vColor;
    layout(location = 1) in  float vArcLength;
    layout(location = 2) in  float vSide;
    layout(location = 0) out vec4  outColor;
    layout(set = 0, binding = 0) uniform sampler2D patternTex;
    layout(push_constant) uniform PC {
      mat4 view_proj;
      vec4 dash_params;
      vec4 line_params;    // x=fx_bits y=glow/core z=time w=flow_speed
      vec4 outline_color;
      vec4 tex_params;     // x=tex_tiling(每米) y=texture_slot(行)
    } pc;
    void main() {
      // 箭头(arc_length<0)恒实心,即使虚线。
      if (pc.dash_params.y > 0.5 && vArcLength >= 0.0 && pc.dash_params.x > 0.0) {
        float t = fract(vArcLength / pc.dash_params.x);
        if (t < pc.dash_params.z) discard;
      }
      int   bits = int(pc.line_params.x + 0.5);
      float s    = abs(vSide);          // 0 中线 .. 1 边缘
      float core = clamp(pc.line_params.y, 0.05, 0.95);
      vec3  rgb  = vColor.rgb;
      float a    = vColor.a;
      // 图案 / 流动(bit2):沿弧长采图,flow_speed 滚动 U。
      //   tex_params.z>0.5 = 自定义贴图(V 跨线宽采全图,RGB×线色 + alpha);
      //   否则 = 内置 4 行 atlas(选 row,只用 alpha 携图案形状)。
      if ((bits & 4) != 0) {
        float tile = max(pc.tex_params.x, 0.001);
        float u    = vArcLength * tile - pc.line_params.z * pc.line_params.w * tile;
        if (pc.tex_params.z > 0.5) {
          vec4 t = texture(patternTex, vec2(u, vSide * 0.5 + 0.5));
          rgb *= t.rgb;
          a   *= t.a;
        } else {
          float row  = clamp(pc.tex_params.y, 0.0, 3.0);
          float vrow = (row + (vSide * 0.5 + 0.5)) / 4.0;
          a *= texture(patternTex, vec2(u, vrow)).a;
        }
      }
      // 发光(bit1):芯亮、边缘柔衰减(alpha 随 |side| 落到 0,芯部向白提亮)。
      if ((bits & 2) != 0) {
        a   *= 1.0 - smoothstep(core, 1.0, s);
        rgb  = mix(rgb * 1.4 + vec3(0.15), rgb, s);
      }
      // 描边(bit0):外侧 core..core+0.06 软边混到描边色。
      if ((bits & 1) != 0) {
        float k = smoothstep(core, core + 0.06, s);
        rgb = mix(rgb, pc.outline_color.rgb, k);
        a   = mix(a,   pc.outline_color.a,   k);
      }
      if (a < 0.02) discard;
      outColor = vec4(rgb, a);
    }
  )glsl";

  auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vs_r.success || !fs_r.success) {
    spdlog::error("PolylineRenderer: shader compile failed vs='{}' fs='{}'",
                  vs_r.errorMessage, fs_r.errorMessage);
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
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc ld;
  ld.setLayouts.push_back(pattern_set_layout_);  // set 0:图案 atlas
  PushConstantRange pcr;
  // Push constants now used by both vertex (view_proj) and fragment
  // (dash_params) — flag both stages so validation is happy.
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0u;
  pcr.size = sizeof(PushConstants);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) return false;

  VertexInputBindingDescription bind;
  bind.binding = 0u;
  bind.stride = sizeof(RibbonVertex);
  bind.perInstance = false;

  VertexInputAttributeDescription pos_attr;
  pos_attr.location = 0u;
  pos_attr.binding = 0u;
  pos_attr.format = Format::R32G32B32_SFLOAT;
  pos_attr.offset = offsetof(RibbonVertex, position);

  VertexInputAttributeDescription col_attr;
  col_attr.location = 1u;
  col_attr.binding = 0u;
  col_attr.format = Format::R32G32B32A32_SFLOAT;
  col_attr.offset = offsetof(RibbonVertex, color);

  VertexInputAttributeDescription arc_attr;
  arc_attr.location = 2u;
  arc_attr.binding = 0u;
  arc_attr.format = Format::R32_SFLOAT;
  arc_attr.offset = offsetof(RibbonVertex, arc_length);

  VertexInputAttributeDescription side_attr;
  side_attr.location = 3u;
  side_attr.binding = 0u;
  side_attr.format = Format::R32_SFLOAT;
  side_attr.offset = offsetof(RibbonVertex, side);

  // Alpha-blended so polylines can fade smoothly and overlap routes don't
  // produce hard z-fighting boundaries. Depth-test on (so ribbons hide
  // behind buildings), depth-write off (so overlapping ribbons don't
  // self-occlude).
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
  pd.vertexInput.attributes = {pos_attr, col_attr, arc_attr, side_attr};
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
  if (!pipeline_) return false;

  // Second pipeline: same state but depth-test disabled. Used by gizmo /
  // selection-highlight draws that must stay visible through terrain.
  pd.depthStencil.depthTestEnable = false;
  pipeline_on_top_ = device_->createGraphicsPipeline(pd);
  return pipeline_on_top_ != nullptr;
}

RHIBufferPtr PolylineRenderer::uploadRibbon(const std::vector<RibbonVertex>& ribbon) {
  const uint32_t needed = static_cast<uint32_t>(ribbon.size());
  if (needed == 0) return nullptr;
  // Round-robin:每次 record() 用下一个槽。同帧多次 record(场景折线、gizmo
  // 各一次)若共写同一块 host-visible 内存,命令缓冲提交执行时只剩最后一次
  // 的数据 —— 先前的 draw 用自己的顶点数去读别人的几何 + 陈旧尾巴,表现为
  // 选中实体时 gizmo 原点冒出巨型绿色垃圾飘带(随构建布局/时序偶发)。
  const int slot = ribbon_pool_next_;
  ribbon_pool_next_ = (ribbon_pool_next_ + 1) % kRibbonPool;
  if (!ribbon_vbo_pool_[slot] || ribbon_cap_pool_[slot] < needed) {
    // Grow with headroom so frame-to-frame jitter doesn't reallocate.
    const uint32_t grown = std::max(needed * 2u, 256u);
    BufferDesc bd;
    bd.size = static_cast<uint64_t>(grown) * sizeof(RibbonVertex);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    ribbon_vbo_pool_[slot] = device_->createBuffer(bd);
    ribbon_cap_pool_[slot] = grown;
  }
  if (!ribbon_vbo_pool_[slot]) return nullptr;
  void* dst = ribbon_vbo_pool_[slot]->map();
  std::memcpy(dst, ribbon.data(), ribbon.size() * sizeof(RibbonVertex));
  ribbon_vbo_pool_[slot]->unmap();
  return ribbon_vbo_pool_[slot];
}

void PolylineRenderer::record(const RHICommandBufferPtr& cmd,
                              const Params& p,
                              const std::vector<PolylineDraw>& lines) {
  if (lines.empty()) return;

  // Build one big ribbon vertex buffer covering every polyline. Per-draw
  // dash params live in push constants, so we re-push between draws —
  // cheap, and lets each polyline have its own dash pattern.
  std::vector<RibbonVertex> combined;
  struct Range {
    uint32_t  first;
    uint32_t  count;
    glm::vec4 dash_params;
    glm::vec4 line_params;    // x=fx_bits y=glow_power z=time w=flow_speed
    glm::vec4 outline_color;
    glm::vec4 tex_params;
    bool      on_top;
    int       custom_slot;    // 自定义贴图槽,-1=用内置 atlas
  };
  std::vector<Range> ranges;
  ranges.reserve(lines.size());
  for (const auto& line : lines) {
    auto ribbon = expandPolylineRibbon(line.points, line.width_m * 0.5f,
                                       p.camera_pos_rtc, line.color,
                                       line.closed,
                                       line.arrow_head, line.arrow_tail,
                                       line.arrow_size_m, line.flat);
    glm::vec4 dash{line.dash_size_m, line.dashed ? 1.0f : 0.0f,
                   line.dash_gap_ratio, 0.0f};
    // fx 位掩码:has_fx 直接用;否则从旧单选 line_type 映射(兼容旧调用)。
    uint32_t bits = line.fx_bits;
    if (!line.has_fx) {
      switch (line.line_type) {
        case 1: bits = 1u; break;          // 描边
        case 2: bits = 2u; break;          // 发光
        case 3: case 4: bits = 4u; break;  // 贴图 / 流动
        default: bits = 0u; break;         // 普通 / 飞线(几何)
      }
    }
    glm::vec4 lp{static_cast<float>(bits), line.glow_power,
                 p.time_s, line.flow_speed};
    // tex_params.z = 自定义贴图标志(1=用整图全宽采样,0=内置 4 行 atlas)。
    const bool custom = line.custom_pattern_slot >= 0 &&
                        line.custom_pattern_slot < static_cast<int>(custom_set_.size());
    glm::vec4 tex{line.tex_tiling, static_cast<float>(line.texture_slot),
                  custom ? 1.0f : 0.0f, 0.0f};
    const int cslot = custom ? line.custom_pattern_slot : -1;
    if (ribbon.empty()) {
      ranges.push_back({static_cast<uint32_t>(combined.size()), 0u,
                         dash, lp, line.outline_color, tex, line.on_top, cslot});
      continue;
    }
    ranges.push_back({static_cast<uint32_t>(combined.size()),
                      static_cast<uint32_t>(ribbon.size()),
                      dash, lp, line.outline_color, tex, line.on_top, cslot});
    combined.insert(combined.end(), ribbon.begin(), ribbon.end());
  }
  if (combined.empty()) return;

  RHIBufferPtr vbo = uploadRibbon(combined);
  if (!vbo) return;

  cmd->bindVertexBuffer(0, vbo);

  const ShaderStageFlags push_stages =
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  PushConstants pc;
  pc.view_proj = p.view_proj;

  // Two-pass: depth-tested lines first, then on-top lines so they win
  // the over-paint regardless of submit order. Avoids per-draw pipeline
  // switches when on_top is mixed in arbitrarily.
  auto draw_pass = [&](bool on_top) {
    auto pipe = on_top ? pipeline_on_top_ : pipeline_;
    if (!pipe) return;
    bool bound = false;
    const void* cur_set = nullptr;   // 当前绑定的 set 0(避免重复绑定)
    for (const auto& r : ranges) {
      if (r.count == 0 || r.on_top != on_top) continue;
      if (!bound) { cmd->bindPipeline(pipe); bound = true; }
      // set 0:自定义贴图槽优先,否则内置图案 atlas(普通/描边/发光不采样,
      // 但布局要求始终有一张图绑着)。仅在 set 变化时重绑。
      RHIDescriptorSetPtr want =
          (r.custom_slot >= 0 && r.custom_slot < static_cast<int>(custom_set_.size()))
              ? custom_set_[r.custom_slot] : pattern_set_;
      if (want && want.get() != cur_set) {
        cmd->bindDescriptorSet(0u, want, pipeline_layout_);
        cur_set = want.get();
      }
      pc.dash_params   = r.dash_params;
      pc.line_params   = r.line_params;
      pc.outline_color = r.outline_color;
      pc.tex_params    = r.tex_params;
      cmd->pushConstants(pipeline_layout_, push_stages,
                         0, sizeof(pc), &pc);
      cmd->draw(r.count, 1u, r.first, 0u);
    }
  };
  draw_pass(false);
  draw_pass(true);
}

}  // namespace hongtu::renderer
