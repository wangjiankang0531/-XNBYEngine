#include <hongtu/renderer/pbr/skybox_pass.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <string>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool SkyboxPass::initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass) {
  device_ = device;
  ShaderCompiler compiler;

  // Fullscreen triangle emitted at NDC z = 1.0 (far plane) so depth test
  // LessOrEqual against the cleared 1.0 passes; depth-write off so any
  // subsequent geometry (depth < 1.0) overwrites the sky. The vertex
  // shader carries the world-space ray direction so the fragment shader
  // can hash per-direction without needing a separate unprojection.
  const char* vsrc = R"glsl(
    layout(push_constant) uniform PC {
      mat4 inv_view_proj;
      vec4 sky_params;
      vec4 sun_dir_ws;
      vec4 extra;
    } pc;
    layout(location = 0) out vec3 outDir;
    void main() {
      // Three-vertex fullscreen triangle covering NDC [-1,1] x [-1,1].
      vec2 ndc = vec2(
          (gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
          (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0);
      vec4 worldPos = pc.inv_view_proj * vec4(ndc, 1.0, 1.0);
      outDir      = worldPos.xyz / worldPos.w;
      gl_Position = vec4(ndc, 1.0, 1.0);
    }
  )glsl";

  // Procedural deep-space star field. Direct port of the globe demo's
  // skybox so the two demos share a single look.
  const char* fsrc = R"glsl(
    layout(push_constant) uniform PC {
      mat4 inv_view_proj;
      vec4 sky_params;   // .x mode: 0 starfield, 1 daylight; .w time
      vec4 sun_dir_ws;   // .xyz sun dir, .w sun disc intensity
      vec4 extra;        // .x inline_tonemap(原始贴图模式着色器内 ACES)
    } pc;
    layout(location = 0) in  vec3 inDir;
    layout(location = 0) out vec4 outColor;

    vec3 acesFilmic(vec3 x) {
      return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }

    float hash(vec2 p) {
      return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
    }

    // 平滑值噪声 + 分形叠加(FBM)—— 程序化云的基础。
    float vnoise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      f = f * f * (3.0 - 2.0 * f);
      float a = hash(i), b = hash(i + vec2(1.0, 0.0));
      float c = hash(i + vec2(0.0, 1.0)), e = hash(i + vec2(1.0, 1.0));
      return mix(mix(a, b, f.x), mix(c, e, f.x), f.y);
    }
    float fbm(vec2 p) {
      float v = 0.0, a = 0.5;
      for (int i = 0; i < 5; ++i) { v += a * vnoise(p); p = p * 2.02 + 7.3; a *= 0.5; }
      return v;
    }

    // 天空渐变(天顶深蓝→地平线雾白,黄昏暖橙),不含云和太阳。
    vec3 skyGradient(vec3 d, vec3 sd, float sunh) {
      vec3 sky_zenith  = mix(vec3(0.07, 0.12, 0.42), vec3(0.12, 0.33, 0.78), sunh);
      vec3 sky_horizon = mix(vec3(0.98, 0.55, 0.30), vec3(0.68, 0.81, 0.95), sunh);
      vec3 ground      = mix(vec3(0.20, 0.15, 0.13), vec3(0.45, 0.40, 0.32), sunh);
      float u = clamp(d.z, -1.0, 1.0);   // Z-up local world
      vec3 above = mix(sky_horizon, sky_zenith, smoothstep(0.0, 0.55, u));
      vec3 below = mix(sky_horizon, ground,     smoothstep(0.0, 0.6, -u));
      vec3 col = (u >= 0.0) ? above : below;
      // 黄昏:朝太阳方向的地平线加暖色辉光(随太阳走低而增强)。
      float dusk = (1.0 - sunh) * clamp(sd.z * 6.0 + 1.0, 0.0, 1.0);
      vec2  dh = normalize(d.xy + vec2(1e-5));
      vec2  sh = normalize(sd.xy + vec2(1e-5));
      float toward = max(dot(dh, sh), 0.0);
      col += vec3(0.92, 0.34, 0.09) * dusk * pow(toward, 3.0)
             * exp(-abs(d.z) * 5.0) * 0.8;
      return col;
    }

    // 程序化飘云:把视线投到天穹平面(d.xy/d.z),FBM 两层做蓬松云团,随
    // 时间缓慢平移。云顶受太阳照亮发白、云底偏灰,贴近地平线渐隐成雾。
    // 返回叠加云之后的颜色,density 输出供太阳被云遮挡用。
    vec3 applyClouds(vec3 col, vec3 d, vec3 sd, float sunh, float t, out float density) {
      density = 0.0;
      if (d.z < 0.03) return col;                 // 地平线以下不画云
      vec2 cp = d.xy / d.z;                        // 天穹投影(近地平线自然压缩)
      // 云带从近地平线一直铺到高空,只在最贴地平线那点淡出避免硬边。
      float horizonFade = smoothstep(0.03, 0.14, d.z);
      vec2 uv = cp * 0.55 + vec2(t * 0.006, t * 0.0035);  // 缩放 + 缓慢飘移
      float base   = fbm(uv);
      float detail = fbm(uv * 2.7 + base * 1.5);   // 域扭曲增加蓬松感
      float n = base * 0.60 + detail * 0.40;
      // 覆盖度由云量(pc.extra.y)驱动:0=晴空万里(高阈值少云)…1=阴天密云。
      float amount = clamp(pc.extra.y, 0.0, 1.0);
      float cov = mix(0.74, 0.26, amount) - 0.04 * sunh;
      density = smoothstep(cov, cov + 0.16, n) * horizonFade;
      float sunAmt = max(dot(d, sd), 0.0);
      // 云顶受阳发亮白,朝阳面更亮;云底偏冷灰。整体高对比于蓝天才看得见。
      vec3 lit      = mix(vec3(0.96, 0.97, 1.00), vec3(1.30, 1.26, 1.16), pow(sunAmt, 2.5));
      vec3 shadowed = vec3(0.62, 0.66, 0.76);
      vec3 cloud = mix(shadowed, lit, smoothstep(cov, cov + 0.40, n));
      cloud *= (0.65 + 0.35 * sunh);               // 黄昏整体压暗
      return mix(col, cloud, density * 0.96);
    }

    // Daylight sky — gradient + 飘云 + 太阳(被云按密度遮挡)。HDR-scale 输出;
    // ACES at compose compresses it. inlined 与 IBL analyticEnv 渐变一致。
    vec3 daylightSky(vec3 d, float t) {
      vec3 sd = normalize(pc.sun_dir_ws.xyz);
      // sunh: 1 = 太阳高悬(正午),0 = 太阳贴/低于地平线(黄昏调色板)。
      float sunh = clamp(sd.z * 3.0, 0.0, 1.0);
      vec3 col = skyGradient(d, sd, sunh);
      float density;
      col = applyClouds(col, d, sd, sunh, t, density);
      float cosang = dot(d, sd);
      // 可见太阳:核盘(~2.5°)+ 内晕 + 大柔光晕,做成一颗看得见的太阳(否则
      // 太小,用户找不到、也没法把它框进画面给体积光当光源)。被云按密度衰减。
      float occ = 1.0 - density * 0.85;
      // 核盘(放大到 ~2.5°,亮)
      col += vec3(1.0, 0.97, 0.90) * pc.sun_dir_ws.w *
             smoothstep(0.9985, 0.9994, cosang) * 60.0 * occ;
      // 内晕(紧、暖、亮)
      col += mix(vec3(1.0, 0.6, 0.3), vec3(1.0, 0.93, 0.78), sunh)
             * pow(max(cosang, 0.0), 200.0) * 4.0 * occ;
      // 外柔光晕(宽,让太阳在天上明显)
      col += mix(vec3(1.0, 0.55, 0.28), vec3(0.9, 0.93, 1.0), sunh)
             * pow(max(cosang, 0.0), 28.0) * 0.5 * occ;
      return col;
    }

    // Deep-space star field(从 main 提出,昼夜混合模式复用)。
    vec3 starfield(vec3 d) {
      const float PI = 3.14159265;
      float u = atan(d.z, d.x) * (0.5 / PI) + 0.5;
      float v = asin(clamp(d.y, -1.0, 1.0)) * (1.0 / PI) + 0.5;
      const float SCALE = 250.0;
      vec2 uv_s  = vec2(u, v) * SCALE;
      vec2 cell  = floor(uv_s);
      vec2 local = fract(uv_s);
      float presence = hash(cell);
      float ox       = hash(cell + vec2(37.3, 0.0));
      float oy       = hash(cell + vec2(0.0, 73.1));
      float szVar    = hash(cell + vec2(117.9, 0.0));
      float colorVar = hash(cell + vec2(157.3, 0.0));
      vec3 color = vec3(0.0, 0.0, 0.004);
      if (presence > 0.93) {
        vec2  center     = vec2(ox, oy);
        float dist       = length(local - center);
        float radius     = 0.025 + 0.055 * szVar;
        float brightness = max(0.0, 1.0 - dist / radius);
        brightness = brightness * brightness;
        brightness *= 0.4 + 0.6 * (presence - 0.93) / 0.07;
        vec3 warmStar = vec3(1.0, 0.95, 0.80);
        vec3 coolStar = vec3(0.85, 0.90, 1.00);
        color += mix(warmStar, coolStar, colorVar) * brightness;
      }
      float band = exp(-pow((v - 0.5) * 13.0, 2.0)) * 0.022;
      color += vec3(0.45, 0.35, 0.70) * band;
      return color;
    }

    void main() {
      vec3 d = normalize(inDir);
      float mode = pc.sky_params.x;

      if (mode > 1.5) {
        // 昼夜混合(编辑器 Time-of-Day):.y = 显示亮度,.z = night(0 昼…1 夜)。
        // 白天渐变(含黄昏调色板)随 night 淡出,星空淡入;地平线下保持暗色。
        float disp  = pc.sky_params.y > 0.0 ? pc.sky_params.y : 1.0;
        float night = clamp(pc.sky_params.z, 0.0, 1.0);
        vec3 day = daylightSky(d, pc.sky_params.w) * disp;
        vec3 nightcol = starfield(d) + vec3(0.010, 0.014, 0.030);  // 夜空微蓝底
        if (d.z < 0.0) nightcol = mix(nightcol, vec3(0.004, 0.005, 0.010),
                                      smoothstep(0.0, 0.3, -d.z));
        vec3 c = mix(day, nightcol, night);
        if (pc.extra.x > 0.5) c = acesFilmic(c);  // 原始贴图模式:着色器内 ACES 防过曝
        outColor = vec4(c, 1.0);
        return;
      }

      if (mode > 0.5) {
        // .y = display brightness — the IBL integrates the full-HDR gradient,
        // but ACES at compose pushes a 2.6-luminance horizon to paper white;
        // scaling the DISPLAYED sky keeps it blue without dimming the scene.
        float disp = pc.sky_params.y > 0.0 ? pc.sky_params.y : 1.0;
        vec3 c = daylightSky(d, pc.sky_params.w) * disp;
        if (pc.extra.x > 0.5) c = acesFilmic(c);
        outColor = vec4(c, 1.0);
        return;
      }

      outColor = vec4(starfield(d), 1.0);
    }
  )glsl";

  auto vr = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fr = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vr.success || !fr.success) return false;
  ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex;   vd.spirvCode = vr.spirvCode;
  ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
  vs_ = device_->createShader(vd);
  fs_ = device_->createShader(fd);
  if (!vs_ || !fs_) return false;

  PipelineLayoutDesc pll;
  PushConstantRange pcr;
  // 顶点重建方向、片元做天空着色 —— 两个阶段都读 push constants。
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size   = sizeof(PushData);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  GraphicsPipelineDesc pd;
  pd.shaders                       = {vs_, fs_};
  pd.primitiveTopology             = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode        = CullMode::None;
  pd.rasterization.fillMode        = FillMode::Solid;
  pd.depthStencil.depthTestEnable  = true;
  pd.depthStencil.depthWriteEnable = false;            // geometry overwrites
  pd.depthStencil.depthCompareOp   = CompareOp::LessOrEqual;  // pass at depth==1.0
  pd.renderPass                    = scene_render_pass;
  pd.pipelineLayout                = pipeline_layout_;
  pipeline_ = device_->createGraphicsPipeline(pd);
  return pipeline_ != nullptr;
}

void SkyboxPass::record(const RHICommandBufferPtr& cmd, const PushData& pc) {
  cmd->bindPipeline(pipeline_);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
      0, sizeof(pc), &pc);
  cmd->draw(3u);
}

}  // namespace hongtu::renderer
