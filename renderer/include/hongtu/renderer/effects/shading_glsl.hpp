#pragma once

#include <string>

namespace hongtu::renderer::effects {

/// Shared GLSL snippets used by all effect-enabled forward shaders.
///
/// Both `local_demo` and the future PBR-enabled `earth_demo` (GeoEntity path)
/// embed these strings via `ShaderCompiler::compileGLSLToSPIRV` so the shading
/// model stays consistent across modes. Each function below returns a self-
/// contained block of GLSL; callers concatenate them with their own main().
class ShadingGLSL {
 public:
  // ---------------------------------------------------------------------------
  // Stage 1 — Tonemapping (linear HDR → linear LDR)
  // ---------------------------------------------------------------------------

  /// ACES filmic curve approximation by Krzysztof Narkowicz. Cheap and stable
  /// — close to the reference RRT+ODT for SDR display while keeping highlights
  /// from clipping. Output is linear [0,1]; subsequent sRGB framebuffer write
  /// gamma-encodes it automatically.
  static std::string acesTonemap() {
    return R"glsl(
vec3 acesFilmic(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
)glsl";
  }

  // ---------------------------------------------------------------------------
  // Stage 2 — PBR (Cook-Torrance + GGX) primitives
  // ---------------------------------------------------------------------------

  /// Schlick Fresnel approximation. F0 is the base reflectance; for
  /// dielectrics ≈ 0.04, for metals = the base color itself.
  static std::string fresnelSchlick() {
    return R"glsl(
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
)glsl";
  }

  /// GGX (Trowbridge-Reitz) normal distribution. α = roughness².
  static std::string distributionGGX() {
    return R"glsl(
float distributionGGX(vec3 N, vec3 H, float roughness) {
  float a      = roughness * roughness;
  float a2     = a * a;
  float NdotH  = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;
  float denom  = NdotH2 * (a2 - 1.0) + 1.0;
  return a2 / (3.14159265 * denom * denom);
}
)glsl";
  }

  /// Smith geometry term using Schlick-GGX. Used for both view and light
  /// directions, then multiplied.
  static std::string geometrySmith() {
    return R"glsl(
float geometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;     // direct-lighting remap
  return NdotV / (NdotV * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  return geometrySchlickGGX(NdotV, roughness)
       * geometrySchlickGGX(NdotL, roughness);
}
)glsl";
  }

  // ---------------------------------------------------------------------------
  // Stage 3 — Image-Based Lighting (analytic environment, no cubemap)
  // ---------------------------------------------------------------------------

  /// Analytic environment colour from a world-space direction. Three-zone
  /// gradient (zenith / horizon / nadir) tuned to look like a slightly hazy
  /// daylight sky. Values are deliberately in HDR scale (1-5 range) — ACES
  /// tonemap downstream compresses them. Caller chooses what +up means by
  /// passing the dot of dir with the scene up axis.
  static std::string analyticEnv() {
    return R"glsl(
vec3 analyticEnv(vec3 dir, float upAxis) {
  // Daylight overcast sky in HDR luminance scale.
  vec3 sky_zenith  = vec3(2.2, 2.6, 3.4);
  vec3 sky_horizon = vec3(2.6, 2.5, 2.3);
  vec3 ground      = vec3(0.45, 0.40, 0.32);
  float u = clamp(upAxis, -1.0, 1.0);
  vec3 above = mix(sky_horizon, sky_zenith, smoothstep(0.0, 0.6, u));
  vec3 below = mix(sky_horizon, ground,     smoothstep(0.0, 0.6, -u));
  return u >= 0.0 ? above : below;
}
)glsl";
  }

  /// Karis split-sum env-BRDF approximation: returns (x, y) where the IBL
  /// specular contribution is `prefilteredEnv * (F0 * x + y)`. Avoids the
  /// BRDF LUT texture — accurate enough for a real-time shader.
  /// Reference: "Real Shading in Unreal Engine 4", Brian Karis 2013.
  static std::string envBRDFApprox() {
    return R"glsl(
vec2 envBRDFApprox(float NdotV, float roughness) {
  const vec4 c0 = vec4(-1.0,  -0.0275, -0.572,  0.022);
  const vec4 c1 = vec4( 1.0,   0.0425,  1.04,  -0.04);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}
)glsl";
  }

  /// Full IBL evaluator using analytic environment + split-sum approximation.
  /// Stage 3 substitute for prefiltered cubemaps: takes a direction, returns
  /// a colour, with roughness controlling how much the reflection blurs
  /// toward the average env colour.
  static std::string evalAnalyticIBL() {
    return R"glsl(
vec3 evalAnalyticIBL(vec3 N, vec3 V, vec3 F0, vec3 albedo,
                     float roughness, float metallic, float skyScale) {
  // NOTE: analyticEnv lives in HDR scale (~2-3.4), so at face value the sky
  // irradiance is 3-4x the sun's diffuse term and textures wash out to pale
  // blue. Callers MUST compensate via their env scale product — the tuned
  // park/CRS pipelines use sky_scale 1.0 × ibl_diffuse 0.5 ≈ net 0.5, and
  // the editor now matches that. Don't scale here: the demo looks are
  // calibrated against these exact constants.
  vec3 R = reflect(-V, N);
  float NdotV = max(dot(N, V), 0.0);

  vec3 envN     = analyticEnv(N, N.z) * skyScale;
  vec3 envAvg   = (vec3(2.2, 2.6, 3.4) + vec3(0.45, 0.40, 0.32)) * 0.5 * skyScale;
  // 0.35 mix toward avg (was 0.65): keeps the irradiance directional so an
  // up-facing surface picks up zenith sky and a side-facing surface picks
  // up horizon — preserves texture/colour identity. Heavy averaging used to
  // tint everything toward grey-blue, washing materials out.
  // π factor: irradiance over a uniform hemisphere is π × sky_const; Lambert
  // BRDF has 1/π — we bake the π here, divide later, math is clean.
  const float PI = 3.14159265;
  vec3 irradiance = mix(envN, envAvg, 0.35) * PI;

  vec3 envR     = analyticEnv(R, R.z) * skyScale;
  vec3 prefilt  = mix(envR, envAvg, roughness * 0.85);

  // Karis split-sum env BRDF
  vec2 envBRDF  = envBRDFApprox(NdotV, roughness);
  vec3 specularIBL = prefilt * (F0 * envBRDF.x + envBRDF.y);

  // Diffuse contribution kills with Fresnel at grazing & with metallic.
  vec3 kS = F0 + (vec3(1.0) - F0)
           * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
  // Lambert BRDF has 1/π; we baked the π into irradiance above so divide
  // here. Net effect: diffuseIBL ≈ kD * sky * albedo, mathematically clean.
  vec3 diffuseIBL = kD * (irradiance / PI) * albedo;

  return diffuseIBL + specularIBL;
}

/// Additive skylight that doesn't depend on albedo — represents indirect
/// bounces from the sky onto a surface (real outdoor scenes never go to
/// pitch-black even with dark albedo because the sky bleeds everywhere).
/// The sky colour is luminance-normalised so `strength` IS the absolute HDR
/// floor added (×skyScale). ~0.05-0.20; metals get less of it. NOTE: this
/// used to multiply the raw HDR zenith (~3), so strength 0.35 injected a
/// +1.3 HDR albedo-independent veil that flattened every texture to white.
vec3 skylightBleed(vec3 N, float skyScale, float strength, float metallic) {
  // zenith / its Rec.709 luminance — keeps the blue tint, luminance = 1.
  vec3 skyCol   = vec3(2.2, 2.6, 3.4) / 2.57;
  // Bias toward upward-coming light (overcast skies dome the whole upper hemisphere).
  float upish   = clamp(N.z * 0.5 + 0.7, 0.3, 1.0);
  return skyCol * skyScale * strength * upish * (1.0 - metallic);
}

/// 程序化夜间窗灯(玻璃掩膜版):发光只落在贴图的"玻璃"像素上(暗色/
/// 偏蓝),程序化网格只决定"哪个房间亮灯"。城市资产的墙面贴图通常自带
/// 真实窗户 —— 随机亮点若不贴着贴图窗户走,近看就是撒在墙皮上的雪花
/// 噪点(city.glb 实测)。掩膜方案下网格与贴图对不齐也看不出来:
/// 不是玻璃的像素永远不亮。
/// 距离 LOD:房间格不足几个像素时淡出为玻璃区平均微光,防远景闪点。
/// night<=0 时整段跳过(白天零开销)。输出 HDR(亮房 ~2.2)供 bloom。
vec3 windowEmissive(vec3 wp, vec3 N, float night, vec3 albedo,
                    vec3 up, float hbias) {
  if (night <= 0.001) return vec3(0.0);
  // ENU 参数化:本地模式 up=(0,0,1)、hbias=0 时与平地公式逐项等价;
  // geo RTC 模式传锚点 up 与高度偏置,窗灯在地球上同样成立。
  float wallness = 1.0 - smoothstep(0.25, 0.45, abs(dot(N, up)));
  float h_m = dot(wp, up) + hbias;
  if (wallness <= 0.0 || h_m < 3.0) return vec3(0.0);
  // 玻璃掩膜:亮度低 和/或 蓝通道占优的像素视为玻璃。米色墙皮(高亮度)
  // 掩膜≈0 → 永不发光;玻璃幕墙(暗蓝)整面可亮,由房间格控制疏密。
  float lum = dot(albedo, vec3(0.299, 0.587, 0.114));
  float darkness = 1.0 - smoothstep(0.08, 0.40, lum);
  float blueness = clamp((albedo.b - max(albedo.r, albedo.g)) * 4.0, 0.0, 1.0);
  float glass = clamp(darkness * 0.9 + blueness * 0.5, 0.0, 1.0);
  if (glass <= 0.01) return vec3(0.0);
  vec3  t = normalize(cross(up, N));                // 水平沿墙切向
  vec2  cuv = vec2(dot(wp, t), h_m) / vec2(4.0, 3.2);   // 房间宽 × 层高(米)
  // 掺入量化法线方向,相邻朝向的楼面不同步(粗量化也容忍弧形墙)。
  float nq   = floor(atan(N.y, N.x) * 2.5);
  vec2  cell = floor(cuv);
  // 楼面级亮灯率:以 8×8 房间(约 32m×26m)为一个"楼面"取哈希,
  // 让一部分楼整栋黑、其余各有疏密 —— 全城均匀亮就是"密密麻麻"。
  float bh = fract(sin(dot(vec3(floor(cell.x / 8.0), floor(cell.y / 8.0), nq),
                           vec3(269.5, 183.3, 74.7))) * 43758.5453);
  float lit_ratio = bh < 0.45 ? 0.0 : mix(0.05, 0.25, fract(bh * 7.0));
  if (lit_ratio <= 0.0) return vec3(0.0);
  // 距离 LOD:fwidth 估算每房间格占的屏幕像素数,真正亚像素才切聚合。
  float px_cell = 1.0 / max(max(fwidth(cuv.x), fwidth(cuv.y)), 1e-6);
  float lod = smoothstep(1.2, 4.0, px_cell);
  // 楼层条带:~55% 楼层整层熄灯 —— 真实办公楼夜景是整层亮/暗,
  // 纯盐粒随机是"彩纸屑"观感的另一来源。
  float fh = fract(sin(dot(vec2(cell.y, bh * 100.0), vec2(41.3, 289.1)))
                   * 23421.63);
  float floor_on = step(0.45, fh);
  float hsh = fract(sin(dot(vec3(cell, nq), vec3(127.1, 311.7, 74.7)))
                    * 43758.5453);
  float lit  = step(1.0 - lit_ratio, hsh) * floor_on;
  vec3  wcol = mix(vec3(1.0, 0.70, 0.40), vec3(1.0, 0.90, 0.75),
                   fract(hsh * 17.0));
  // 近:把 4m 房间格细分成 2 扇"柔边窗扇"(圆滑)—— 借鉴烤制的单窗近看
  // 观感,取代原来的实心硬块。宽 smoothstep 软边即"圆滑"且自带抗锯齿;
  // 相邻格间留暗框,不再糊成一坨。窗扇居中放置,位置更规整。
  vec2  f  = fract(cuv);
  float wu = fract(f.x * 2.0);                        // 4m 格内放 2 扇窗(各 ~2m)
  float sx = smoothstep(0.12, 0.34, wu) * (1.0 - smoothstep(0.66, 0.88, wu));
  float sy = smoothstep(0.16, 0.36, f.y) * (1.0 - smoothstep(0.72, 0.92, f.y));
  float pane = sx * sy;
  // 远:极淡的玻璃区微光 —— 聚合项可见为均匀色块时比黑更难看,压低。
  vec3 near_term = wcol * (lit * pane * 2.6);
  vec3 far_term  = vec3(1.0, 0.80, 0.55) * (lit_ratio * 0.28);
  return mix(far_term, near_term, lod) * (glass * wallness * night);
}
)glsl";
  }

  // ---------------------------------------------------------------------------
  // Stage 3b — 点光源直接光照(Cook-Torrance,≤16 盏)
  // ---------------------------------------------------------------------------

  /// 累加 ≤16 盏点光源的直接光照。调用方须保证 fresnelSchlick /
  /// distributionGGX / geometrySmith 已在同一 fsrc 拼接中出现在本函数之前。
  /// UBO 布局与 PointLightBlock(C++ 侧)完全对应:set=3 binding=0。
  static std::string pointLights() {
    return R"glsl(
layout(std140, set = 3, binding = 0) uniform Lights {
  ivec4 count;            // .x = 盏数
  vec4  pos_radius[16];
  vec4  color_intensity[16];
} uLights;

// 累加 ≤16 盏点光源直接光照(Cook-Torrance,复用 BRDF helper)。
// 平滑距离衰减:att = clamp(1-(d/r)^2,0,1)^2 / (d^2 + 1)。
vec3 evalPointLights(vec3 P, vec3 N, vec3 V, vec3 albedo,
                     float metallic, float roughness, vec3 F0) {
  vec3 sum = vec3(0.0);
  int n = min(uLights.count.x, 16);
  for (int i = 0; i < n; ++i) {
    vec3  Lp = uLights.pos_radius[i].xyz;
    float r  = uLights.pos_radius[i].w;
    vec3  toL = Lp - P;
    float d  = length(toL);
    if (d > r) continue;
    vec3  L = toL / max(d, 1e-4);
    float att = clamp(1.0 - (d/r)*(d/r), 0.0, 1.0);
    att = att * att / (d*d + 1.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) continue;
    vec3  H = normalize(V + L);
    vec3  Fl = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float Dl = distributionGGX(N, H, roughness);
    float Gl = geometrySmith(N, V, L, roughness);
    vec3  spec = (Dl * Gl * Fl) / max(4.0 * max(dot(N,V),0.0) * NdotL, 1e-4);
    vec3  kD = (vec3(1.0) - Fl) * (1.0 - metallic);
    vec3  rad = uLights.color_intensity[i].rgb * uLights.color_intensity[i].a;
    sum += (kD * albedo / 3.14159265 + spec) * rad * att * NdotL;
  }
  return sum;
}
)glsl";
  }

  // ---------------------------------------------------------------------------
  // Stage 4 — Shadow mapping (single map, PCF)
  // ---------------------------------------------------------------------------

  /// PCF (Percentage Closer Filtering) 3×3 around the projected light-space
  /// position. Returns 1.0 = fully lit, 0.0 = fully shadowed. `bias` is a
  /// slope-scaled depth offset chosen by the caller to fight acne.
  static std::string shadowPCF() {
    return R"glsl(
float sampleShadowPCF(sampler2D shadowMap, vec3 lsCoords, float bias) {
  // Outside [0,1] frustum → not in shadow.
  if (lsCoords.x < 0.0 || lsCoords.x > 1.0 ||
      lsCoords.y < 0.0 || lsCoords.y > 1.0 ||
      lsCoords.z > 1.0) {
    return 1.0;
  }
  float refDepth = lsCoords.z - bias;
  vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float d = texture(shadowMap, lsCoords.xy + vec2(x, y) * texelSize).r;
      sum += (refDepth <= d) ? 1.0 : 0.0;
    }
  }
  return sum / 9.0;
}
)glsl";
  }

  /// Reconstruct a TBN matrix from world-space position + uv derivatives
  /// without needing a per-vertex tangent attribute. Approximation from
  /// Mikkelsen's "Surface gradient based bump mapping" — good enough for
  /// city/architectural assets that often lack TANGENT data.
  static std::string tbnFromDerivatives() {
    return R"glsl(
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  vec3 dp1  = dFdx(p);
  vec3 dp2  = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float m2 = max(dot(T, T), dot(B, B));
  // UV 退化三角形(decimate 网格常见)导数为 0 → inversesqrt(0)=INF →
  // 0*INF=NaN 法线 → 光照全 NaN → 整片闪黑块(云渲染流里取证定位)。
  // 退回以几何法线为准的正交帧 —— 这种面法线贴图本来就无意义。
  if (m2 < 1e-12) {
    vec3 up = abs(N.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 Tf = normalize(cross(up, N));
    return mat3(Tf, cross(N, Tf), N);
  }
  float invmax = inversesqrt(m2);
  return mat3(T * invmax, B * invmax, N);
}
)glsl";
  }
};

}  // namespace hongtu::renderer::effects
