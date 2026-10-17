#include "hongtu/renderer/environment/apply_environment.hpp"

#include "hongtu/renderer/environment/time_of_day.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace hongtu::renderer {

void applyEnvironment(const EnvironmentSettings& s, float time,
                      ForwardSceneRenderer::FrameParams& fp,
                      HdrComposeTarget::ComposeParams& cp) {
  // ---- Sun direction (azimuth/elevation → unit cartesian) ----
  // 方位角 az 从北顺时针,仰角 el。Z-up 世界:
  //   x = cos(el)*sin(az), y = cos(el)*cos(az), z = sin(el)
  // 与编辑器 editor_app.cpp:1698-1703 一致。
  const float az = glm::radians(s.sun_azimuth_deg);
  const float el = glm::radians(s.sun_elevation_deg);
  const float cy = std::cos(el);
  fp.sun_dir_world = glm::vec3(cy * std::sin(az), cy * std::cos(az), std::sin(el));
  fp.sun_intensity = s.sun_intensity;

  // ---- sky_scale 固定 1.0 ----
  // analyticEnv 是 HDR 幅度(~3),net IBL(sky_scale × ibl_intensity) 须维持 ~0.5
  // 否则天空漫射压死太阳/洗白纹理。与编辑器 lp.sky_scale=1.0f 一致。
  fp.sky_scale = 1.0f;

  // ---- IBL — ibl_enabled 门控 ----
  // 编辑器 lp.ibl_intensity = env.ibl_enabled ? env.ibl_intensity : 0.0f (ln 1710)
  fp.ibl_diffuse_scale = s.ibl_enabled ? s.ibl_intensity : 0.0f;

  // ---- Exposure 默认 1.0;tod 接管时覆盖 ----
  // 编辑器 lp.exposure = 1.0f (ln 1711)
  fp.exposure = 1.0f;

  // ---- Skylight bleed: 暗面地板 + 环境补光 ----
  // 基值:IBL 开 0.10,关 0.16(编辑器 ln 1715)。
  // 环境补光无论 IBL 开关都叠加(编辑器 ln 1718-1719)。
  fp.skylight_bleed = s.ibl_enabled ? 0.10f : 0.16f;
  if (s.ambient_enabled) fp.skylight_bleed += s.ambient_intensity * 0.3f;

  // ---- Render mode & shadow ----
  // 编辑器 ln 1720-1722
  fp.render_mode    = s.render_mode;
  fp.shadow_strength = s.sun_casts_shadows ? s.shadow_strength : 0.0f;

  // ---- Night factor 默认 0(白天) ----
  fp.night_factor = 0.0f;

  // ---- Skybox 默认:过程化天空 1(昼间渐变),tod 接管后切 2(昼夜混合) ----
  // procedural_sky 开时用模式 1(daylight),否则关闭(调用方让 clear_color_hdr 生效)。
  fp.skybox_enabled = s.procedural_sky;
  fp.skybox_mode    = 1;  // daylight;tod 接管后改为 2
  // 程序化天空云量(0 晴…1 多云)→ skybox extra.y(镜像编辑器 lighting_.sky_cloud_amount)。
  fp.sky_cloud_amount = s.sky_cloud_amount;

  // ---- Clear color(天空纯色底) ----
  // 编辑器 ln 1855:lp.sky_color = env.sky_color(恒用纯色底,HDR 归 IBL)。
  fp.clear_color_hdr = s.sky_color;

  // ---- Tod override block ----
  // 编辑器 ln 1733-1775:tod 接管太阳方向/强度/IBL/skylight/曝光/night_factor。
  // tod_playing/dt 是编辑器瞬态 UI,此处由调用方控制 tod_hours 推进;
  // tod_speed 供参考(编辑器每帧 dt*speed*(24/60))。
  // 此函数接收的 s.tod_hours 已由调用方按需推进(或静态);time 仅驱动纹理相位。
  if (s.tod_enabled) {
    const auto tod = evalTimeOfDay(s.tod_hours);
    fp.sun_dir_world      = tod.sun_dir;
    fp.sun_intensity      = tod.sun_intensity;
    // tod 内的 ibl_intensity 仍受 ibl_enabled 门控(编辑器 ln 1746)
    fp.ibl_diffuse_scale  = s.ibl_enabled ? tod.ibl_intensity : 0.0f;
    fp.skylight_bleed     = tod.skylight_bleed;
    fp.exposure           = tod.exposure;
    fp.night_factor       = tod.night_factor;
    fp.clear_color_hdr    = tod.sky_color;
    // 昼夜混合天空模式(编辑器注释 ln 1728-1730,skybox_pass mode>1.5 分支)
    fp.skybox_enabled     = true;
    fp.skybox_mode        = 2;
  }

  // ---- SSAO ----
  // 编辑器 ln 1778-1781:ssao_enabled 映射 ssao_strength(0/1 门控)
  fp.ssao_strength  = s.ssao_enabled ? 1.0f : 0.0f;
  fp.ssao_radius_m  = s.ssao_radius_m;
  fp.ssao_intensity = s.ssao_intensity;
  fp.ssao_bias      = s.ssao_bias;

  // ---- Bloom — bloom_enabled 门控 ----
  // 编辑器 ln 1782-1784
  fp.bloom_intensity = s.bloom_enabled ? s.bloom_intensity : 0.0f;
  fp.bloom_threshold = s.bloom_threshold;
  fp.bloom_radius    = s.bloom_radius;

  // ---- FXAA ----
  // 编辑器 ln 1786
  fp.fxaa_enabled = s.fxaa_enabled;

  // ===========================================================
  // Compose params (cp) — post-FX 直接从 settings 透传
  // ===========================================================

  // Tonemap & exposure — exposure 已由上方 tod 分支覆盖
  cp.tonemap_mode = s.tonemap_mode;
  cp.exposure     = fp.exposure;

  // AO / bloom / FXAA — 与 fp 一致(门控已在上方)
  cp.ao_strength     = fp.ssao_strength;
  cp.bloom_intensity = fp.bloom_intensity;
  cp.fxaa            = fp.fxaa_enabled;

  // Color grading (编辑器 ln 1787-1793)
  cp.grade_saturation  = s.grade_saturation;
  cp.grade_contrast    = s.grade_contrast;
  cp.grade_temperature = s.grade_temperature;

  // Vignette:settings 字段名 vignette_strength → cp 字段名 vignette
  cp.vignette            = s.vignette_strength;
  cp.chromatic_aberration = s.chromatic_aberration;
  cp.sharpen             = s.sharpen_strength;
  cp.grain               = s.grain_strength;

  // God rays / lens flare (编辑器 ln 1794-1796)
  cp.godray_intensity    = s.godray_intensity;
  cp.godray_decay        = s.godray_decay;
  cp.lensflare_intensity = s.lensflare_intensity;
  // lensflare_ghost / lensflare_halo 无对应 settings 字段,保持 ComposeParams 默认值

  // DOF (编辑器 ln 1799-1801)
  cp.dof_intensity = s.dof_intensity;
  cp.dof_range     = s.dof_range;
  cp.dof_max_blur  = s.dof_max_blur;

  // Haze (编辑器 ln 1802-1804)
  cp.haze_intensity = s.haze_intensity;
  cp.haze_start     = s.haze_start;
  cp.haze_power     = s.haze_power;

  // Weather (编辑器 ln 1797-1798)
  cp.rain = s.rain_amount;
  cp.snow = s.snow_amount;

  // Time 驱动纹理相位(grain/雨雪/热浪)
  cp.time = time;

  // sun_uv 留默认(调用方在有相机时通过投影矩阵覆盖)
  // znear / zfar 留默认(调用方在 FrameParams 中已有相机矩阵,覆盖 cp)
}

}  // namespace hongtu::renderer
