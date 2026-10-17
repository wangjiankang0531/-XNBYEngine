#include "hongtu/renderer/environment/environment_settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace hongtu::renderer {

using nlohmann::json;

namespace {
glm::vec3 readVec3(const json& j, glm::vec3 fallback) {
  if (j.is_array() && j.size() == 3) {
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
  }
  return fallback;
}
json writeVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
}  // namespace

bool EnvironmentSettings::load(const std::string& path, EnvironmentSettings& out) {
  std::ifstream f(path);
  if (!f) return false;
  json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) return false;

  std::string kind = j.value("sky_kind", "solid_color");
  out.sky_kind     = (kind == "hdr") ? EnvironmentSettings::SkyKind::Hdr
                                      : EnvironmentSettings::SkyKind::SolidColor;
  out.sky_color        = readVec3(j["sky_color"], out.sky_color);
  out.procedural_sky   = j.value("procedural_sky", true);
  out.sky_cloud_amount = j.value("sky_cloud_amount", 0.45f);
  out.sky_hdr          = j.value("sky_hdr", "");
  out.sky_rotation_deg = j.value("sky_rotation_deg", 0.0f);

  out.sun_azimuth_deg   = j.value("sun_azimuth_deg",   145.0f);
  out.sun_elevation_deg = j.value("sun_elevation_deg",  45.0f);
  out.sun_color         = readVec3(j["sun_color"], out.sun_color);
  out.sun_intensity     = j.value("sun_intensity",     6.0f);
  out.sun_casts_shadows = j.value("sun_casts_shadows", true);

  out.ibl_enabled   = j.value("ibl_enabled",   true);
  out.ibl_intensity = j.value("ibl_intensity",  0.5f);
  out.ibl_hdr       = j.value("ibl_hdr",        "");

  out.ambient_enabled   = j.value("ambient_enabled",   true);
  out.ambient_color     = readVec3(j["ambient_color"], out.ambient_color);
  out.ambient_intensity = j.value("ambient_intensity", 0.2f);

  // Clamp to the current mode range — older docs may carry 4 (mip viz before
  // the UV-gradient mode was removed); it maps onto today's 3 = mip.
  out.render_mode     = std::clamp(j.value("render_mode", 0), 0, 3);
  out.tonemap_mode    = j.value("tonemap_mode", 1);
  out.shadow_strength = j.value("shadow_strength", 0.5f);

  out.ssao_enabled   = j.value("ssao_enabled",    false);
  out.ssao_radius_m  = j.value("ssao_radius_m",   0.3f);
  out.ssao_intensity = j.value("ssao_intensity",  0.8f);
  out.ssao_bias      = j.value("ssao_bias",       0.025f);

  out.bloom_enabled   = j.value("bloom_enabled",   false);
  out.bloom_intensity = j.value("bloom_intensity", 0.25f);
  out.bloom_threshold = j.value("bloom_threshold", 1.0f);
  out.bloom_radius    = j.value("bloom_radius",    1.0f);

  out.fog_enabled = j.value("fog_enabled", false);
  out.fog_color   = readVec3(j["fog_color"], out.fog_color);
  out.fog_density = j.value("fog_density", 0.005f);
  out.fog_start   = j.value("fog_start",   50.0f);
  out.fog_end     = j.value("fog_end",     1500.0f);

  out.fxaa_enabled         = j.value("fxaa_enabled",         false);
  out.grade_saturation     = j.value("grade_saturation",     1.0f);
  out.grade_contrast       = j.value("grade_contrast",       1.0f);
  out.grade_temperature    = j.value("grade_temperature",    0.0f);
  out.vignette_strength    = j.value("vignette_strength",    0.0f);
  out.chromatic_aberration = j.value("chromatic_aberration", 0.0f);
  out.sharpen_strength     = j.value("sharpen_strength",     0.0f);
  out.grain_strength       = j.value("grain_strength",       0.0f);
  out.godray_intensity     = j.value("godray_intensity",     0.0f);
  out.godray_decay         = j.value("godray_decay",         0.96f);
  out.lensflare_intensity  = j.value("lensflare_intensity",  0.0f);
  out.rain_amount          = j.value("rain_amount",          0.0f);
  out.snow_amount          = j.value("snow_amount",          0.0f);
  out.dof_intensity        = j.value("dof_intensity",        0.0f);
  out.dof_range            = j.value("dof_range",            0.08f);
  out.dof_max_blur         = j.value("dof_max_blur",         8.0f);
  out.haze_intensity       = j.value("haze_intensity",       0.0f);
  out.haze_start           = j.value("haze_start",           0.6f);
  out.haze_power           = j.value("haze_power",           2.0f);

  out.cloud_enabled    = j.value("cloud_enabled",    false);
  out.cloud_coverage   = j.value("cloud_coverage",   0.5f);
  out.cloud_density    = j.value("cloud_density",    1.0f);
  out.cloud_bottom_alt = j.value("cloud_bottom_alt", 2000.0f);
  out.cloud_top_alt    = j.value("cloud_top_alt",    7000.0f);

  out.tod_enabled = j.value("tod_enabled", false);
  out.tod_hours   = std::clamp(j.value("tod_hours", 10.0f), 0.0f, 24.0f);
  out.tod_speed = std::clamp(j.value("tod_speed", 1.0f), 0.25f, 8.0f);
  // 读取播放状态:云渲染据此决定昼夜是否推进(暂停则静止)。
  out.tod_playing = j.value("tod_playing", false);

  return true;
}

bool EnvironmentSettings::save(const std::string& path, const EnvironmentSettings& s) {
  json j;
  j["schema_version"]      = EnvironmentSettings::kSchemaVersion;
  j["sky_kind"]            = (s.sky_kind == EnvironmentSettings::SkyKind::Hdr) ? "hdr" : "solid_color";
  j["sky_color"]           = writeVec3(s.sky_color);
  j["procedural_sky"]      = s.procedural_sky;
  j["sky_cloud_amount"]    = s.sky_cloud_amount;
  j["sky_hdr"]             = s.sky_hdr;
  j["sky_rotation_deg"]    = s.sky_rotation_deg;
  j["sun_azimuth_deg"]     = s.sun_azimuth_deg;
  j["sun_elevation_deg"]   = s.sun_elevation_deg;
  j["sun_color"]           = writeVec3(s.sun_color);
  j["sun_intensity"]       = s.sun_intensity;
  j["sun_casts_shadows"]   = s.sun_casts_shadows;
  j["ibl_enabled"]         = s.ibl_enabled;
  j["ibl_intensity"]       = s.ibl_intensity;
  j["ibl_hdr"]             = s.ibl_hdr;
  j["ambient_enabled"]     = s.ambient_enabled;
  j["ambient_color"]       = writeVec3(s.ambient_color);
  j["ambient_intensity"]   = s.ambient_intensity;
  j["render_mode"]         = s.render_mode;
  j["tonemap_mode"]        = s.tonemap_mode;
  j["shadow_strength"]     = s.shadow_strength;
  j["ssao_enabled"]        = s.ssao_enabled;
  j["ssao_radius_m"]       = s.ssao_radius_m;
  j["ssao_intensity"]      = s.ssao_intensity;
  j["ssao_bias"]           = s.ssao_bias;
  j["bloom_enabled"]       = s.bloom_enabled;
  j["bloom_intensity"]     = s.bloom_intensity;
  j["bloom_threshold"]     = s.bloom_threshold;
  j["bloom_radius"]        = s.bloom_radius;
  j["fog_enabled"]         = s.fog_enabled;
  j["fog_color"]           = writeVec3(s.fog_color);
  j["fog_density"]         = s.fog_density;
  j["fog_start"]           = s.fog_start;
  j["fog_end"]             = s.fog_end;
  j["fxaa_enabled"]        = s.fxaa_enabled;
  j["grade_saturation"]    = s.grade_saturation;
  j["grade_contrast"]      = s.grade_contrast;
  j["grade_temperature"]   = s.grade_temperature;
  j["vignette_strength"]   = s.vignette_strength;
  j["chromatic_aberration"] = s.chromatic_aberration;
  j["sharpen_strength"]     = s.sharpen_strength;
  j["grain_strength"]       = s.grain_strength;
  j["godray_intensity"]     = s.godray_intensity;
  j["godray_decay"]         = s.godray_decay;
  j["lensflare_intensity"]  = s.lensflare_intensity;
  j["rain_amount"]          = s.rain_amount;
  j["snow_amount"]          = s.snow_amount;
  j["dof_intensity"]        = s.dof_intensity;
  j["dof_range"]            = s.dof_range;
  j["dof_max_blur"]         = s.dof_max_blur;
  j["haze_intensity"]       = s.haze_intensity;
  j["haze_start"]           = s.haze_start;
  j["haze_power"]           = s.haze_power;
  j["cloud_enabled"]        = s.cloud_enabled;
  j["cloud_coverage"]       = s.cloud_coverage;
  j["cloud_density"]        = s.cloud_density;
  j["cloud_bottom_alt"]     = s.cloud_bottom_alt;
  j["cloud_top_alt"]        = s.cloud_top_alt;
  j["tod_enabled"]          = s.tod_enabled;
  j["tod_hours"]            = s.tod_hours;
  // tod_playing is editor-UI-only transient state — intentionally not written.
  j["tod_speed"]            = s.tod_speed;

  std::ofstream f(path);
  if (!f) return false;
  f << j.dump(2);
  return static_cast<bool>(f);
}

}  // namespace hongtu::renderer
