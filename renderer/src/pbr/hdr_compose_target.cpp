#include <hongtu/renderer/pbr/hdr_compose_target.hpp>
#include <hongtu/renderer/effects/shading_glsl.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool HdrComposeTarget::initialize(RHIDevice* device) {
  device_ = device;
  offscreen_mode_ = false;
  if (!createHdrTarget()) return false;
  if (!createFallbackTextures()) return false;
  return createComposePipeline();
}

bool HdrComposeTarget::initializeOffscreen(RHIDevice* device, uint32_t width,
                                           uint32_t height, Format output_format) {
  device_ = device;
  offscreen_mode_ = true;
  output_format_  = output_format;
  offscreen_extent_ = {width, height};
  if (!createHdrTarget(width, height)) return false;
  if (!createFallbackTextures()) return false;
  if (!createOffscreenComposePass(output_format)) return false;
  return createComposePipelineState();
}

bool HdrComposeTarget::resizeOffscreen(uint32_t width, uint32_t height) {
  if (!offscreen_mode_ || !device_) return false;
  if (offscreen_extent_.width == width && offscreen_extent_.height == height &&
      hdr_color_ && output_color_) {
    return true;  // no change
  }
  device_->waitIdle();
  offscreen_extent_ = {width, height};

  // Recreate HDR RT at the new size.
  if (!createHdrTarget(width, height)) return false;

  // Recreate the owned output texture + framebuffer (same render pass + format
  // — only the attachment dimensions change).
  TextureDesc od;
  od.type        = TextureType::Texture2D;
  od.extent      = {width, height, 1u};
  od.mipLevels   = 1u;
  od.arrayLayers = 1u;
  od.format      = output_format_;
  od.usage       = static_cast<TextureUsageFlags>(TextureUsageFlagBits::ColorAttachment)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::Sampled)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::TransferSrc);
  od.sampleCount = 1u;
  output_color_ = device_->createTexture(od);
  if (!output_color_) return false;
  output_view_ = output_color_->createView();
  if (!output_view_) return false;
  offscreen_fb_ = device_->createFramebuffer(compose_render_pass_, {output_view_},
                                             offscreen_extent_, 1);
  if (!offscreen_fb_) return false;

  // Re-point the descriptor set at the fresh HDR view (binding 0).
  rebuildDescriptorSet();
  return true;
}

bool HdrComposeTarget::createFallbackTextures() {
  // 1×1 white AO fallback so the compose pipeline always has something to
  // sample for binding=1, regardless of whether SSAO is actually running.
  TextureDesc fd;
  fd.type        = TextureType::Texture2D;
  fd.extent      = {1u, 1u, 1u};
  fd.mipLevels   = 1u;
  fd.arrayLayers = 1u;
  fd.format      = Format::R8_UNORM;
  fd.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  fd.sampleCount = 1u;
  ao_default_tex_ = device_->createTexture(fd);
  if (!ao_default_tex_) return false;
  uint8_t white = 255u;
  std::vector<RHIDevice::MipData> mips(1);
  mips[0].width = 1u; mips[0].height = 1u;
  mips[0].data = &white; mips[0].dataSize = 1u;
  device_->uploadTextureData(ao_default_tex_, mips, 0u);
  ao_default_view_ = ao_default_tex_->createView();
  if (!ao_default_view_) return false;

  // 1×1 black bloom fallback — additive 0 has no effect, so compose pass is a
  // no-op for bloom when BloomPass isn't wired up.
  TextureDesc bd;
  bd.type        = TextureType::Texture2D;
  bd.extent      = {1u, 1u, 1u};
  bd.mipLevels   = 1u;
  bd.arrayLayers = 1u;
  bd.format      = Format::R16G16B16A16_SFLOAT;
  bd.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  bd.sampleCount = 1u;
  bloom_default_tex_ = device_->createTexture(bd);
  if (!bloom_default_tex_) return false;
  // R16G16B16A16_SFLOAT — 8 bytes per texel; zero = black.
  uint16_t black_rgba[4] = {0u, 0u, 0u, 0u};
  std::vector<RHIDevice::MipData> bmips(1);
  bmips[0].width = 1u; bmips[0].height = 1u;
  bmips[0].data = reinterpret_cast<uint8_t*>(black_rgba); bmips[0].dataSize = sizeof(black_rgba);
  device_->uploadTextureData(bloom_default_tex_, bmips, 0u);
  bloom_default_view_ = bloom_default_tex_->createView();
  if (!bloom_default_view_) return false;

  // 1×1 depth=1.0 fallback so the bilateral-AO / DoF / haze taps have a
  // sampleable depth texture even before setDepthView() is wired up. Uniform
  // depth makes the bilateral blur weights equal (→ plain AO average) and the
  // depth-dependent haze/DoF gradients degenerate to constants — all harmless.
  TextureDesc dd;
  dd.type        = TextureType::Texture2D;
  dd.extent      = {1u, 1u, 1u};
  dd.mipLevels   = 1u;
  dd.arrayLayers = 1u;
  dd.format      = Format::R32_SFLOAT;
  dd.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  dd.sampleCount = 1u;
  depth_default_tex_ = device_->createTexture(dd);
  if (!depth_default_tex_) return false;
  float one_depth = 1.0f;
  std::vector<RHIDevice::MipData> dmips(1);
  dmips[0].width = 1u; dmips[0].height = 1u;
  dmips[0].data = reinterpret_cast<uint8_t*>(&one_depth);
  dmips[0].dataSize = sizeof(one_depth);
  device_->uploadTextureData(depth_default_tex_, dmips, 0u);
  depth_default_view_ = depth_default_tex_->createView();
  if (!depth_default_view_) return false;

  return true;
}

bool HdrComposeTarget::createHdrTarget() {
  auto ext = device_->getSwapchainExtent();
  return createHdrTarget(ext.width, ext.height);
}

bool HdrComposeTarget::createHdrTarget(uint32_t width, uint32_t height) {
  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {width, height, 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R16G16B16A16_SFLOAT;
  // Storage 位刻意加上:Metal 对 ShaderWrite 纹理禁用无损压缩。HDR 同帧
  // 内先做 RT 再被 compose 采样,Apple GPU 的压缩元数据在这条路径上同步
  // 不可靠(MoltenVK),表现为相机运动时树冠 tile 状黑块闪烁(云渲染流
  // 逐项取证定位)。带宽代价:一张全屏 f16,UMA 上可忽略。
  td.usage       = static_cast<TextureUsageFlags>(TextureUsageFlagBits::ColorAttachment)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::Sampled)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::Storage);
  td.sampleCount = 1u;
  hdr_color_ = device_->createTexture(td);
  if (!hdr_color_) return false;
  hdr_view_ = hdr_color_->createView();
  return hdr_view_ != nullptr;
}

bool HdrComposeTarget::createSwapchainComposePass() {
  // ---- compose render pass: color attachment is the swapchain image ----
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format        = device_->getSwapchainFormat();
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::PresentSrc;
  ca.loadOpClear   = true;
  ca.storeOpStore  = true;
  rpd.attachments.push_back(ca);

  SubpassDescription sp;
  AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  rpd.subpasses.push_back(sp);

  SubpassDependency dep;
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
  dep.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
  rpd.dependencies.push_back(dep);
  compose_render_pass_ = device_->createRenderPass(rpd);
  if (!compose_render_pass_) return false;

  // ---- per-swapchain-image framebuffer ----
  for (uint32_t i = 0; i < device_->getSwapchainImageCount(); ++i) {
    auto img = device_->getSwapchainImage(i);
    auto cv  = img->createView();
    auto fb  = device_->createFramebuffer(compose_render_pass_, {cv},
                                            device_->getSwapchainExtent(), 1);
    if (!fb) return false;
    compose_fbs_.push_back(fb);
  }
  return true;
}

bool HdrComposeTarget::createOffscreenComposePass(Format output_format) {
  // ---- compose render pass: color attachment is the class-OWNED output ----
  // Final layout ShaderReadOnlyOptimal so the result can be sampled / read back
  // (NOT PresentSrc — there is no swapchain in this mode).
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format        = output_format;
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;
  ca.loadOpClear   = true;
  ca.storeOpStore  = true;
  rpd.attachments.push_back(ca);

  SubpassDescription sp;
  AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  rpd.subpasses.push_back(sp);

  // The compose fragment shader SAMPLES the HDR / AO / bloom / depth inputs
  // (all written as color/depth attachments by earlier passes). Make those
  // prior writes visible to this pass's fragment-shader reads — without the
  // FragmentShader dst stage the sampled bloom/AO can be read before the
  // producing pass's store completes (manifests as a uniformly-bright bloom
  // texture → blown-out compose). The previous ColorAttachmentOutput-only
  // dependency only covered the swapchain-style self-write ordering.
  SubpassDependency dep;
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
  dep.dstStageMask  = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput) |
                      static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
  dep.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite) |
                      static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
  rpd.dependencies.push_back(dep);
  compose_render_pass_ = device_->createRenderPass(rpd);
  if (!compose_render_pass_) return false;

  // ---- owned output color texture + view + single framebuffer ----
  // Usage: ColorAttachment (compose target) | Sampled (GL/ImGui) | TransferSrc
  // (readbackToBuffer-style copy to a host buffer).
  TextureDesc od;
  od.type        = TextureType::Texture2D;
  od.extent      = {offscreen_extent_.width, offscreen_extent_.height, 1u};
  od.mipLevels   = 1u;
  od.arrayLayers = 1u;
  od.format      = output_format;
  od.usage       = static_cast<TextureUsageFlags>(TextureUsageFlagBits::ColorAttachment)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::Sampled)
                 | static_cast<TextureUsageFlags>(TextureUsageFlagBits::TransferSrc);
  od.sampleCount = 1u;
  output_color_ = device_->createTexture(od);
  if (!output_color_) return false;
  output_view_ = output_color_->createView();
  if (!output_view_) return false;

  offscreen_fb_ = device_->createFramebuffer(compose_render_pass_, {output_view_},
                                             offscreen_extent_, 1);
  return offscreen_fb_ != nullptr;
}

bool HdrComposeTarget::createComposePipeline() {
  if (!createSwapchainComposePass()) return false;
  return createComposePipelineState();
}

bool HdrComposeTarget::createComposePipelineState() {
  // ---- compose shaders ----
  ShaderCompiler compiler;
  const char* vsrc = R"glsl(
    // Fullscreen triangle without vertex buffers — 3 verts via gl_VertexIndex.
    layout(location = 0) out vec2 vUv;
    void main() {
      vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
      gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
    }
  )glsl";
  // FULL cinematic compose — ported VERBATIM from the editor
  // (hongtu-editor/src/viewport/viewport_renderer.cpp buildComposePipeline).
  // The editor's 8×vec4 PC block (flags/grade/grade2/post/godray/flare/dof/cam)
  // is preserved exactly; the engine's pre-existing shockwave-ring + heat-haze
  // distortion vec4s (sw_ring/sw_strength/heat_params/heat_sources[4]) are
  // APPENDED after it so BOTH feature sets coexist. The distortion is applied
  // once to the incoming UV (distortUV) so it carries through every downstream
  // tap (resolveLDR / bilateralAO / FXAA / DoF / sharpen).
  //
  // Binding layout matches the engine's existing compose set, with depth added
  // at binding 3 (editor uses 0=HDR,1=AO,2=Bloom,3=Depth — same numbers).
  const char* fsrc = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D hdrTex;
    layout(set = 0, binding = 1) uniform sampler2D aoTex;
    layout(set = 0, binding = 2) uniform sampler2D bloomTex;
    layout(set = 0, binding = 3) uniform sampler2D depthTex;

    layout(push_constant) uniform PC {
      // ---- editor's 8×vec4 cinematic block (verbatim) ----
      // .x = tonemap mode (0 none,1 ACES,2 AgX,3 Filmic,4 Reinhard)
      // .y = ao_strength (0 disable, 1 full)
      // .z = bloom_intensity
      // .w = exposure
      vec4 flags;
      // .x = fxaa (1 = on), .y = saturation, .z = contrast, .w = vignette
      vec4 grade;
      // .x = temperature (-1 cool … +1 warm), .y = haze intensity,
      // .z = rain amount, .w = snow amount
      vec4 grade2;
      // .x = chromatic aberration, .y = sharpen, .z = film grain, .w = time
      vec4 post;
      // .xy = sun screen UV, .z = godray intensity (0 = off), .w = decay
      vec4 godray;
      // .x = lens-flare intensity (0 = off), .y = ghost dispersal,
      // .z = halo width, .w = haze depth start
      vec4 flare;
      // .x = DoF intensity (0 = off), .y = focus range (占焦点深度比例),
      // .z = max blur (texels), .w = haze curve power
      vec4 dof;
      // .x = znear, .y = zfar(把原始非线性深度还原成线性视距,DoF 用)。
      // .z = srgb_encode (1 = encode sRGB in-shader for UNORM targets,
      //      0 = leave linear for sRGB swapchain — engine default).
      vec4 cam;
      // ---- engine's distortion block (APPENDED — coexists with above) ----
      vec4 sw_ring;        // .xy center_uv, .z radius_uv, .w thickness_uv
      vec4 sw_strength;    // .x shockwave distortion strength (0 = off)
      vec4 heat_params;    // .x time, .y global_strength, .z source_count
      vec4 heat_sources[4];// .xy uv, .z radius_uv, .w intensity
    } pc;

    // 原始深度 [0,1] → 线性视距(米)。near/far 比例极大时原始深度全挤在 ~1,
    // 直接做 DoF 不灵;还原成米才有可调差异。
    float linearZ(float d) {
      return (pc.cam.x * pc.cam.y) / max(pc.cam.y - d * (pc.cam.y - pc.cam.x), 1e-4);
    }

    layout(location = 0) in  vec2 vUv;
    layout(location = 0) out vec4 outColor;

    // ---- engine shockwave-ring + heat-haze distortion (preserved) ----
    vec2 distortRing(vec2 uv) {
      if (pc.sw_strength.x < 1e-4) return uv;
      vec2 d = uv - pc.sw_ring.xy;
      float dist = length(d);
      if (dist < 1e-4) return uv;
      float diff = abs(dist - pc.sw_ring.z);
      if (diff >= pc.sw_ring.w) return uv;
      float shell = 1.0 - diff / pc.sw_ring.w;
      shell = shell * shell;          // 平方 soft falloff,中心最强
      return uv + (d / dist) * shell * pc.sw_strength.x;
    }
    float hash11(float x) { return fract(sin(x * 12.9898) * 43758.5453); }
    vec2 distortHeat(vec2 uv) {
      if (pc.heat_params.y < 1e-4) return vec2(0.0);
      int n = min(int(pc.heat_params.z), 4);
      float t = pc.heat_params.x;
      vec2 sum = vec2(0.0);
      for (int i = 0; i < 4; ++i) {
        if (i >= n) break;
        vec4 src = pc.heat_sources[i];
        vec2 d   = uv - src.xy;
        float dist = length(d);
        if (dist >= src.z) continue;
        float fall = 1.0 - dist / src.z;
        fall = fall * fall;
        float phase = uv.y * 80.0 + t * 6.0 + hash11(float(i)) * 6.28;
        sum.y += sin(phase) * fall * src.w * pc.heat_params.y * 0.01;
      }
      return sum;
    }
    // Combined distortion applied ONCE to the base UV so every downstream tap
    // inherits it (relative offsets are preserved).
    vec2 distortUV(vec2 uv) { return distortRing(uv) + distortHeat(uv); }

    // Karis-Narkowicz ACES filmic curve.
    vec3 acesFilmic(vec3 x) {
      const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
      return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    }

    // Uncharted 2 (Hable) filmic.
    vec3 uncharted2Partial(vec3 x) {
      const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
      return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    }
    vec3 hableFilmic(vec3 c) {
      vec3 curr  = uncharted2Partial(c * 2.0);
      vec3 white = uncharted2Partial(vec3(11.2));
      return clamp(curr / white, 0.0, 1.0);
    }

    // AgX (minimal, Benjamin Wrensch) — returns LINEAR (sRGB-encoded after).
    vec3 agxTonemap(vec3 val) {
      const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
      const mat3 agx_inv = mat3(
         1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205,-0.0989611768448433,  1.15107367264116);
      const float mn = -12.47393, mx = 4.026069;
      vec3 v = agx_mat * max(val, vec3(0.0));
      v = clamp(log2(v + 1e-10), mn, mx);
      v = (v - mn) / (mx - mn);
      vec3 v2 = v * v, v4 = v2 * v2;          // 6th-order sigmoid contrast
      v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4
          - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
      v = agx_inv * v;
      return pow(max(v, vec3(0.0)), vec3(2.2));
    }

    // Dispatch by flags.x: 0 none, 1 ACES, 2 AgX, 3 Filmic(Hable), 4 Reinhard.
    vec3 tonemapCurve(vec3 hdr) {
      int m = int(pc.flags.x + 0.5);
      if (m == 1) return acesFilmic(hdr);
      if (m == 2) return agxTonemap(hdr);
      if (m == 3) return hableFilmic(hdr);
      if (m == 4) return hdr / (hdr + vec3(1.0));
      return hdr;
    }

    // Accurate sRGB transfer (gated by cam.z so an sRGB swapchain isn't
    // double-encoded — engine leaves this OFF, editor turns it ON).
    vec3 maybeSrgb(vec3 c) {
      if (pc.cam.z < 0.5) return c;
      c = clamp(c, vec3(0.0), vec3(1.0));
      return mix(1.055 * pow(c, vec3(1.0/2.4)) - 0.055,
                  12.92 * c, step(c, vec3(0.0031308)));
    }

    // Post-tonemap color grading (operates on tonemapped LINEAR color, before
    // the sRGB encode). Saturation → contrast → temperature, all neutral at
    // the default values so an un-graded image is bit-identical.
    vec3 applyGrade(vec3 c) {
      float t = pc.grade2.x;
      c *= vec3(1.0 + 0.10 * t, 1.0 + 0.02 * t, 1.0 - 0.10 * t);
      float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
      c = mix(vec3(l), c, pc.grade.y);
      c = (c - 0.5) * pc.grade.z + 0.5;
      return max(c, vec3(0.0));
    }

    // HDR + AO + bloom → tonemap → grade → (optional sRGB). `aoVal` lets the
    // caller pass the (optionally bilateral-blurred) AO so the center pixel
    // keeps the nicer AO while FXAA neighbor taps use a cheap single-tap AO.
    vec3 resolveLDR(vec2 uv, float aoVal) {
      vec3 hdr = texture(hdrTex, uv).rgb * pc.flags.w;   // exposure
      // Guard AO + bloom by their strength flags: when a pass is skipped its
      // RT may be UNINITIALIZED GPU memory (Vulkan does not zero textures). For
      // a float16 bloom RT that can mean INF/NaN, and NaN*0 = NaN — it poisons
      // whole channels (R/B collapse to 0) without these guards. The editor
      // renders into always-cleared RTs so it can skip the guards; the engine
      // (cloud_render_server / standalone) cannot, so we keep them.
      if (pc.flags.y > 1e-5) hdr *= mix(1.0, aoVal, pc.flags.y);
      if (pc.flags.z > 1e-5) {
        // A very bright source (sun disc) can push the R16F bloom RT to
        // Inf/NaN in SOME channels; adding that poisons the frame (a per-channel
        // NaN→0 fix would leave a colour cast, e.g. green). Zero the WHOLE pixel
        // when any channel is non-finite, and clamp finite glow to a ceiling.
        vec3 bv = texture(bloomTex, uv).rgb;
        bool bad = any(isnan(bv)) || any(greaterThanEqual(bv, vec3(60000.0)));
        bv = bad ? vec3(0.0) : min(bv, vec3(16.0));
        hdr += bv * pc.flags.z;
      }
      vec3 mapped = tonemapCurve(hdr);
      return maybeSrgb(applyGrade(mapped));
    }

    // Depth-aware (bilateral) 5×5 AO blur — keeps AO from leaking across
    // depth discontinuities (building edge vs sky behind it).
    float bilateralAO(vec2 uv) {
      vec2  aoStep  = 3.0 / vec2(textureSize(aoTex, 0));
      float dCenter = texture(depthTex, uv).r;
      const float kDepthSigma = 0.005;
      float ao = 0.0, w_sum = 0.0;
      const float weights[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
      for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
          vec2 off = vec2(x, y) * aoStep;
          float spatial = weights[x + 2] * weights[y + 2];
          float dHere   = texture(depthTex, uv + off).r;
          float dDiff   = (dHere - dCenter) / kDepthSigma;
          float depthW  = exp(-0.5 * dDiff * dDiff);
          float w       = spatial * depthW;
          ao    += texture(aoTex, uv + off).r * w;
          w_sum += w;
        }
      }
      return (w_sum > 1e-6) ? (ao / w_sum) : texture(aoTex, uv).r;
    }

    void main() {
      const vec3 LUMA = vec3(0.299, 0.587, 0.114);
      // Apply shockwave/heat distortion to the base UV up front.
      vec2  vUvD = distortUV(vUv);
      float aoC  = bilateralAO(vUvD);
      vec3  rgbM = resolveLDR(vUvD, aoC);

      if (pc.grade.x > 0.5) {
        vec2 texel = 1.0 / vec2(textureSize(hdrTex, 0));
        vec3 rgbN = resolveLDR(vUvD + vec2(0.0, -texel.y), texture(aoTex, vUvD + vec2(0.0,-texel.y)).r);
        vec3 rgbS = resolveLDR(vUvD + vec2(0.0,  texel.y), texture(aoTex, vUvD + vec2(0.0, texel.y)).r);
        vec3 rgbW = resolveLDR(vUvD + vec2(-texel.x, 0.0), texture(aoTex, vUvD + vec2(-texel.x,0.0)).r);
        vec3 rgbE = resolveLDR(vUvD + vec2( texel.x, 0.0), texture(aoTex, vUvD + vec2( texel.x,0.0)).r);
        float lumaN = dot(rgbN, LUMA), lumaS = dot(rgbS, LUMA);
        float lumaW = dot(rgbW, LUMA), lumaE = dot(rgbE, LUMA);
        float lumaM = dot(rgbM, LUMA);
        float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
        float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
        float range = lumaMax - lumaMin;
        if (range >= max(0.0833, lumaMax * 0.166)) {
          vec2 dir = vec2(-((lumaN + lumaE) - (lumaS + lumaW)),
                           ((lumaN + lumaW) - (lumaS + lumaE)));
          float dirReduce = max((lumaN + lumaS + lumaE + lumaW) * 0.25 * 0.125, 1.0/128.0);
          float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
          dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;
          vec3 rgbA = 0.5 * (resolveLDR(vUvD + dir * (1.0/3.0 - 0.5), aoC) +
                             resolveLDR(vUvD + dir * (2.0/3.0 - 0.5), aoC));
          vec3 rgbB = rgbA * 0.5 + 0.25 * (resolveLDR(vUvD + dir * -0.5, aoC) +
                                            resolveLDR(vUvD + dir *  0.5, aoC));
          float lumaB = dot(rgbB, LUMA);
          rgbM = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
        }
      }

      // Depth of Field — autofocus on the center pixel; circle-of-confusion
      // grows with |depth − centerDepth|; out-of-focus pixels get a 12-tap
      // disc blur of the resolved image. Default off.
      if (pc.dof.x > 0.0) {
        float zFocus = linearZ(texture(depthTex, vec2(0.5)).r);
        float zHere  = linearZ(texture(depthTex, vUvD).r);
        float coc = clamp(abs(zHere - zFocus) / max(zFocus * pc.dof.y, 0.5), 0.0, 1.0);
        if (coc > 0.02) {
          const vec2 DISC[12] = vec2[12](
            vec2( 0.000,  1.000), vec2( 0.866,  0.500), vec2( 0.866, -0.500),
            vec2( 0.000, -1.000), vec2(-0.866, -0.500), vec2(-0.866,  0.500),
            vec2( 0.000,  0.500), vec2( 0.433,  0.250), vec2( 0.433, -0.250),
            vec2( 0.000, -0.500), vec2(-0.433, -0.250), vec2(-0.433,  0.250));
          vec2 texel  = 1.0 / vec2(textureSize(hdrTex, 0));
          float rad   = coc * pc.dof.z;
          vec3  blur  = vec3(0.0);
          for (int i = 0; i < 12; ++i) {
            vec2 o = DISC[i] * rad * texel;
            blur += resolveLDR(vUvD + o, texture(aoTex, vUvD + o).r);
          }
          blur /= 12.0;
          rgbM = mix(rgbM, blur, coc * pc.dof.x);
        }
      }

      // Atmospheric distance haze — fade distant terrain + sky toward an
      // atmospheric tint with depth. grade2.y = intensity, flare.w = depth
      // start, dof.w = curve power. Default off.
      if (pc.grade2.y > 0.0) {
        float dz = texture(depthTex, vUvD).r;
        float h  = pow(clamp((dz - pc.flare.w) / max(1.0 - pc.flare.w, 1e-4),
                             0.0, 1.0), max(pc.dof.w, 0.05));
        rgbM = mix(rgbM, vec3(0.72, 0.80, 0.88), h * pc.grade2.y);
      }

      // Sharpen (unsharp mask) — counteracts FXAA / readback softness.
      if (pc.post.y > 0.0) {
        vec2 t = 1.0 / vec2(textureSize(hdrTex, 0));
        vec3 blur = 0.25 * (resolveLDR(vUvD + vec2(t.x, 0.0), aoC) +
                            resolveLDR(vUvD - vec2(t.x, 0.0), aoC) +
                            resolveLDR(vUvD + vec2(0.0, t.y), aoC) +
                            resolveLDR(vUvD - vec2(0.0, t.y), aoC));
        rgbM = clamp(rgbM + (rgbM - blur) * pc.post.y, 0.0, 1.0);
      }
      // Chromatic aberration — radial channel split that grows toward edges.
      if (pc.post.x > 0.0) {
        vec2 d = (vUvD - 0.5) * pc.post.x;
        rgbM.r = resolveLDR(vUvD + d, aoC).r;
        rgbM.b = resolveLDR(vUvD - d, aoC).b;
      }
      // Film grain — animated luminance noise (time-varied hash).
      if (pc.post.z > 0.0) {
        float n = fract(sin(dot(vUvD * (pc.post.w + 1.0),
                                vec2(12.9898, 78.233))) * 43758.5453);
        rgbM += (n - 0.5) * pc.post.z;
      }

      // God rays (volumetric light scattering as post-process).
      if (pc.godray.z > 0.0) {
        const int   N = 24;
        vec2  duv   = (vUvD - pc.godray.xy) * (0.55 / float(N));
        vec2  uv    = vUvD;
        float illum = 1.0;
        vec3  shaft = vec3(0.0);
        for (int i = 0; i < N; ++i) {
          uv -= duv;
          vec3  c = texture(hdrTex, clamp(uv, 0.0, 1.0)).rgb;
          float b = max(0.0, dot(c, vec3(0.299, 0.587, 0.114)) - 0.5);
          shaft += c * b * illum;
          illum *= pc.godray.w;
        }
        rgbM += clamp(shaft * (pc.godray.z / float(N)), 0.0, 1.0);
      }

      // Lens flare (Chapman screen-space): ghosts marched along the sun↔center
      // axis sampling HDR-bright sources, plus a halo ring.
      if (pc.flare.x > 0.0) {
        vec2 ghostVec = (vec2(0.5) - pc.godray.xy) * pc.flare.y;
        vec3 flareCol = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
          vec2 off = vUvD + ghostVec * float(i);
          float w  = pow(1.0 - clamp(distance(off, vec2(0.5)) / 0.5, 0.0, 1.0), 8.0);
          vec3  c  = texture(hdrTex, clamp(off, 0.0, 1.0)).rgb;
          flareCol += max(vec3(0.0), c - 1.0) * w;
        }
        vec2 haloVec = normalize(ghostVec + vec2(1e-5)) * pc.flare.z;
        float hw = pow(1.0 - clamp(distance(vUvD + haloVec, vec2(0.5)) / 0.5, 0.0, 1.0), 8.0);
        flareCol += max(vec3(0.0), texture(hdrTex, clamp(vUvD + haloVec, 0.0, 1.0)).rgb - 1.0) * hw;
        rgbM += clamp(flareCol * pc.flare.x * vec3(1.0, 0.9, 0.75) * 0.12, 0.0, 1.0);
      }

      // Weather — screen-space rain streaks / drifting snow (time-driven).
      float wTime = pc.post.w;
      if (pc.grade2.z > 0.0) {                 // rain
        float rain = 0.0;
        for (int k = 0; k < 2; ++k) {
          float cols  = 90.0 + float(k) * 55.0;
          float speed = 0.55 + float(k) * 0.35;
          vec2  uv2   = vUvD + vec2(vUvD.y * 0.06, 0.0);
          float c     = uv2.x * cols;
          float ci    = floor(c);
          float fx    = fract(c) - 0.5;
          float r     = fract(sin(ci * (45.13 + float(k) * 9.7)) * 43758.5453);
          float colOn  = step(0.80, r);
          float xline  = smoothstep(0.06, 0.0, abs(fx));
          float y      = uv2.y * 6.0 - wTime * speed * 7.0 + r * 17.0;
          float fy     = fract(y);
          float streak = smoothstep(0.0, 0.03, fy) * smoothstep(0.70, 0.15, fy);
          rain += colOn * xline * streak * (0.55 - float(k) * 0.12);
        }
        rgbM += clamp(rain, 0.0, 1.0) * pc.grade2.z * vec3(0.72, 0.78, 0.88);
      }
      if (pc.grade2.w > 0.0) {                 // snow
        float snow = 0.0;
        for (int k = 0; k < 3; ++k) {
          float sc  = 10.0 + float(k) * 14.0;
          float spd = 0.8 + float(k) * 0.5;
          vec2  p   = vUvD * sc * vec2(1.7, 1.0);
          p.y -= wTime * spd;
          p.x += sin(wTime * 0.5 + float(k) * 7.0 + p.y) * 0.6;
          vec2  f   = fract(p) - 0.5;
          vec2  id  = floor(p);
          float h   = fract(sin(dot(id, vec2(17.1, 93.7))) * 24634.6);
          snow += smoothstep(0.07, 0.0, length(f)) * step(0.85, h) * (0.7 - float(k) * 0.15);
        }
        rgbM += snow * pc.grade2.w * vec3(0.95);
      }

      // Vignette — smooth radial darkening, applied last (no aliasing).
      float vig = 1.0;
      if (pc.grade.w > 0.0) {
        vec2 d = vUv - 0.5;
        float r = dot(d, d) * 2.0;
        vig = mix(1.0, 1.0 - r, clamp(pc.grade.w, 0.0, 1.0));
      }
      outColor = vec4(rgbM * vig, 1.0);
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) return false;
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  compose_vs_ = device_->createShader(vd);
  compose_fs_ = device_->createShader(fd);
  if (!compose_vs_ || !compose_fs_) return false;

  // ---- sampler + descriptor set (binding 0 = HDR RT) ----
  SamplerDesc sd;
  sd.magFilter        = SamplerFilter::Linear;
  sd.minFilter        = SamplerFilter::Linear;
  sd.addressModeU     = SamplerAddressMode::ClampToEdge;
  sd.addressModeV     = SamplerAddressMode::ClampToEdge;
  sd.addressModeW     = SamplerAddressMode::ClampToEdge;
  sd.anisotropyEnable = false;
  compose_sampler_ = device_->createSampler(sd);
  if (!compose_sampler_) return false;

  // Four-binding layout: HDR + AO + Bloom + Depth. AO falls back to a 1×1
  // white texture, bloom to 1×1 black, depth to 1×1 (=1.0), so disabling any
  // is a shader no-op. Depth feeds the bilateral-AO blur / DoF / haze.
  DescriptorSetLayoutDesc dl;
  for (uint32_t b = 0; b < 4u; ++b) {
    DescriptorSetLayoutBinding lb;
    lb.binding         = b;
    lb.descriptorCount = 1u;
    lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    lb.descriptorType  = DescriptorType::CombinedImageSampler;
    dl.bindings.push_back(lb);
  }
  compose_desc_layout_ = device_->createDescriptorSetLayout(dl);
  if (!compose_desc_layout_) return false;

  DescriptorSetDesc dsd;
  auto addBinding = [&](uint32_t b, RHITextureViewPtr v) {
    DescriptorBinding db;
    db.binding     = b;
    db.type        = DescriptorType::CombinedImageSampler;
    db.textureView = v;
    db.sampler     = compose_sampler_;
    dsd.bindings.push_back(db);
  };
  addBinding(0u, hdr_view_);
  addBinding(1u, ao_default_view_);     // fallback until setAoView()
  addBinding(2u, bloom_default_view_);  // fallback until setBloomView()
  addBinding(3u, depth_default_view_);  // fallback until setDepthView()
  compose_desc_set_ = device_->createDescriptorSet(compose_desc_layout_, dsd);
  if (!compose_desc_set_) return false;

  // ---- pipeline ----
  PipelineLayoutDesc pll;
  pll.setLayouts.push_back(compose_desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  // 15 vec4 = 240B: editor's 8 (flags grade grade2 post godray flare dof cam)
  // + engine's 7 (sw_ring sw_strength heat_params heat_sources[4]).
  pcr.size   = sizeof(glm::vec4) * 15;
  pll.pushConstantRanges.push_back(pcr);
  compose_pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!compose_pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders                       = {compose_vs_, compose_fs_};
  // No vertex inputs — fullscreen triangle drawn via gl_VertexIndex.
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = false;
  pd.depthStencil.depthWriteEnable = false;
  pd.renderPass                    = compose_render_pass_;
  pd.pipelineLayout                = compose_pipeline_layout_;
  compose_pipeline_ = device_->createGraphicsPipeline(pd);
  return compose_pipeline_ != nullptr;
}

namespace {
// Re-builds the 3-binding compose descriptor set against the current AO /
// bloom views (or their 1×1 fallbacks).
}  // namespace

void HdrComposeTarget::rebuildDescriptorSet() {
  DescriptorSetDesc dsd;
  auto add = [&](uint32_t b, RHITextureViewPtr v) {
    DescriptorBinding db;
    db.binding     = b;
    db.type        = DescriptorType::CombinedImageSampler;
    db.textureView = v;
    db.sampler     = compose_sampler_;
    dsd.bindings.push_back(db);
  };
  add(0u, external_hdr_view_ ? external_hdr_view_ : hdr_view_);
  add(1u, ao_view_    ? ao_view_    : ao_default_view_);
  add(2u, bloom_view_ ? bloom_view_ : bloom_default_view_);
  add(3u, depth_view_ ? depth_view_ : depth_default_view_);
  compose_desc_set_ = device_->createDescriptorSet(compose_desc_layout_, dsd);
}

void HdrComposeTarget::setAoView(RHITextureViewPtr ao_view) {
  ao_view_ = ao_view;
  rebuildDescriptorSet();
}

void HdrComposeTarget::setBloomView(RHITextureViewPtr bloom_view) {
  bloom_view_ = bloom_view;
  rebuildDescriptorSet();
}

void HdrComposeTarget::setDepthView(RHITextureViewPtr depth_view) {
  depth_view_ = depth_view;
  rebuildDescriptorSet();
}

void HdrComposeTarget::recordCompose(const RHICommandBufferPtr& cmd,
                                       uint32_t image_index,
                                       bool  tonemap_enabled,
                                       float ao_strength,
                                       float bloom_intensity,
                                       bool  fxaa_enabled) {
  Rect2D area;
  area.offset = {0, 0};
  // Offscreen mode: ignore image_index, use the single owned framebuffer +
  // its fixed extent. Swapchain mode: behaviour unchanged.
  const RHIFramebufferPtr& fb =
      offscreen_mode_ ? offscreen_fb_ : compose_fbs_[image_index];
  area.extent = offscreen_mode_ ? offscreen_extent_ : device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {{0.0f, 0.0f, 0.0f, 1.0f}};
  cmd->beginRenderPass(compose_render_pass_, fb, area, clears);
  cmd->bindPipeline(compose_pipeline_);

  Viewport vp;
  vp.x = 0.0f; vp.y = 0.0f;
  vp.width    = static_cast<float>(area.extent.width);
  vp.height   = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd->setViewport(vp);
  cmd->setScissor(area);

  cmd->bindDescriptorSet(0u, compose_desc_set_, compose_pipeline_layout_);
  // ComposePC 240B (15×vec4). Field order/types MUST byte-match the GLSL
  // `layout(push_constant) PC` block above exactly — std140 vec4 alignment is
  // 16B and every member here is a vec4, so the packing is trivially correct.
  //   editor block  : flags grade grade2 post godray flare dof cam   (8×16=128B)
  //   engine block  : sw_ring sw_strength heat_params heat_sources[4] (7×16=112B)
  struct ComposePC {
    glm::vec4 flags;            // .x tonemap mode, .y ao, .z bloom, .w exposure
    glm::vec4 grade;            // .x fxaa, .y saturation, .z contrast, .w vignette
    glm::vec4 grade2;           // .x temperature, .y haze, .z rain, .w snow
    glm::vec4 post;             // .x chromatic, .y sharpen, .z grain, .w time
    glm::vec4 godray;           // .xy sun uv, .z intensity, .w decay
    glm::vec4 flare;            // .x intensity, .y ghost, .z halo, .w haze start
    glm::vec4 dof;              // .x intensity, .y range, .z max blur, .w haze power
    glm::vec4 cam;              // .x znear, .y zfar, .z srgb encode, .w reserved
    glm::vec4 sw_ring;          // .xy center_uv, .z radius_uv, .w thickness_uv
    glm::vec4 sw_strength;      // .x distortion strength(0=off), .yzw reserved
    glm::vec4 heat_params;      // .x time, .y global_strength, .z source_count, .w reserved
    glm::vec4 heat_sources[4];  // .xy uv, .z radius_uv, .w intensity
  };
  static_assert(sizeof(ComposePC) == sizeof(glm::vec4) * 15,
                "ComposePC must be 15 vec4 (240B) to match the GLSL block");

  const ComposeParams& p = params_;
  ComposePC pc{};
  // The 4 legacy recordCompose args OVERRIDE tonemap/ao/bloom/fxaa so existing
  // callers behave exactly as before; the remaining cinematic fields come from
  // the stored ComposeParams (all neutral/off by default → no regression).
  pc.flags  = glm::vec4(tonemap_enabled ? static_cast<float>(p.tonemap_mode) : 0.0f,
                        ao_strength,
                        bloom_intensity,
                        p.exposure);
  pc.grade  = glm::vec4(fxaa_enabled ? 1.0f : 0.0f,
                        p.grade_saturation, p.grade_contrast, p.vignette);
  pc.grade2 = glm::vec4(p.grade_temperature, p.haze_intensity, p.rain, p.snow);
  pc.post   = glm::vec4(p.chromatic_aberration, p.sharpen, p.grain, p.time);
  pc.godray = glm::vec4(p.sun_uv.x, p.sun_uv.y, p.godray_intensity, p.godray_decay);
  pc.flare  = glm::vec4(p.lensflare_intensity, p.lensflare_ghost, p.lensflare_halo,
                        p.haze_start);
  pc.dof    = glm::vec4(p.dof_intensity, p.dof_range, p.dof_max_blur, p.haze_power);
  pc.cam    = glm::vec4(p.znear, p.zfar, p.srgb_encode ? 1.0f : 0.0f, 0.0f);

  pc.sw_ring     = glm::vec4(shockwave_center_uv_,
                              shockwave_radius_uv_,
                              shockwave_thickness_uv_);
  pc.sw_strength = glm::vec4(shockwave_strength_, 0.0f, 0.0f, 0.0f);
  pc.heat_params = glm::vec4(heat_time_seconds_,
                              heat_global_strength_,
                              static_cast<float>(heat_source_count_),
                              0.0f);
  for (int i = 0; i < kMaxHeatHazeSources; ++i) {
    pc.heat_sources[i] = heat_sources_[i];
  }
  cmd->pushConstants(compose_pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(ComposePC), &pc);
  // 3 verts → 1 fullscreen triangle, no vertex/index buffers needed.
  cmd->draw(3u);

  cmd->endRenderPass();
}

void HdrComposeTarget::setShockwaveDistortion(glm::vec2 center_uv,
                                                float     radius_uv,
                                                float     thickness_uv,
                                                float     strength) {
  shockwave_center_uv_     = center_uv;
  shockwave_radius_uv_     = radius_uv;
  shockwave_thickness_uv_  = std::max(thickness_uv, 1e-4f);
  shockwave_strength_      = strength;
}

void HdrComposeTarget::setHeatHazeSources(const std::vector<glm::vec4>& sources,
                                            float time_seconds,
                                            float global_strength) {
  heat_time_seconds_    = time_seconds;
  heat_global_strength_ = global_strength;
  heat_source_count_    = std::min(static_cast<int>(sources.size()),
                                    kMaxHeatHazeSources);
  for (int i = 0; i < kMaxHeatHazeSources; ++i) {
    heat_sources_[i] = (i < heat_source_count_) ? sources[i] : glm::vec4(0.0f);
  }
}

}  // namespace hongtu::renderer
