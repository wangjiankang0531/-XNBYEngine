#pragma once

#include <glm/glm.hpp>

#include <string>

namespace hongtu::renderer {

/// Canonical engine-level description of scene environment / lighting.
///
/// Field list and default values are kept byte-for-byte compatible with the
/// editor's `EnvironmentDoc` so that `.environment.json` files written by the
/// editor can be read directly by the engine (and vice-versa).
///
/// The only editor-UI-only transient field intentionally omitted here is
/// `tod_playing` — it is ephemeral UI state that has no meaning in offline /
/// cloud rendering.
struct EnvironmentSettings {
  // ----- Schema ---------------------------------------------------------------
  static constexpr int kSchemaVersion = 1;

  // ----- Skybox ---------------------------------------------------------------
  enum class SkyKind { SolidColor, Hdr };
  SkyKind     sky_kind         = SkyKind::SolidColor;
  glm::vec3   sky_color        = {0.55f, 0.70f, 0.92f};
  /// Procedural sky (gradient + drifting clouds + sun). When false the raw
  /// `sky_color` is used as a solid background.
  bool        procedural_sky   = true;
  /// Cloud coverage in the procedural sky: 0 = clear, 0.5 = partly cloudy,
  /// 1 = fully overcast.
  float       sky_cloud_amount = 0.45f;
  std::string sky_hdr;                    // asset id (.hdr / .exr)
  float       sky_rotation_deg = 0.0f;

  // ----- Sun ------------------------------------------------------------------
  float     sun_azimuth_deg   = 145.0f;
  float     sun_elevation_deg = 45.0f;
  glm::vec3 sun_color         = {1.0f, 0.96f, 0.88f};
  float     sun_intensity     = 6.0f;
  bool      sun_casts_shadows = true;

  // ----- IBL ------------------------------------------------------------------
  bool        ibl_enabled   = true;
  float       ibl_intensity = 0.5f;
  std::string ibl_hdr;                    // asset id; if empty -> derive from sky_hdr

  // ----- Ambient fallback (no-IBL) -------------------------------------------
  bool      ambient_enabled   = true;
  glm::vec3 ambient_color     = {0.30f, 0.30f, 0.35f};
  float     ambient_intensity = 0.2f;

  // ----- Fog ------------------------------------------------------------------
  bool      fog_enabled = false;
  glm::vec3 fog_color   = {0.70f, 0.75f, 0.80f};
  float     fog_density = 0.005f;
  float     fog_start   = 50.0f;
  float     fog_end     = 1500.0f;

  // ----- Render / tonemap mode ------------------------------------------------
  /// 0 = raw albedo (no lighting), 1 = full PBR, 2 = normal viz, 3 = mip.
  int   render_mode     = 0;
  /// 0 = none, 1 = ACES, 2 = AgX, 3 = Filmic (Hable), 4 = Reinhard.
  int   tonemap_mode    = 1;
  float shadow_strength = 0.5f;

  // ----- SSAO -----------------------------------------------------------------
  bool  ssao_enabled   = false;
  float ssao_radius_m  = 0.3f;
  float ssao_intensity = 0.8f;
  float ssao_bias      = 0.025f;

  // ----- Bloom ----------------------------------------------------------------
  bool  bloom_enabled   = false;
  float bloom_intensity = 0.25f;
  float bloom_threshold = 1.0f;
  float bloom_radius    = 1.0f;

  // ----- Anti-aliasing + color grading ----------------------------------------
  bool  fxaa_enabled       = false;
  float grade_saturation   = 1.0f;
  float grade_contrast     = 1.0f;
  float grade_temperature  = 0.0f;
  float vignette_strength  = 0.0f;
  float chromatic_aberration = 0.0f;
  float sharpen_strength   = 0.0f;
  float grain_strength     = 0.0f;

  // ----- God rays / lens flare ------------------------------------------------
  float godray_intensity  = 0.0f;
  float godray_decay      = 0.96f;
  float lensflare_intensity = 0.0f;

  // ----- Weather overlay ------------------------------------------------------
  float rain_amount = 0.0f;
  float snow_amount = 0.0f;

  // ----- Depth of field -------------------------------------------------------
  float dof_intensity = 0.0f;
  float dof_range     = 0.08f;
  float dof_max_blur  = 8.0f;

  // ----- Atmospheric haze (geo mode only) ------------------------------------
  float haze_intensity = 0.0f;
  float haze_start     = 0.6f;
  float haze_power     = 2.0f;

  // ----- Volumetric clouds (geo mode only) ------------------------------------
  bool  cloud_enabled    = false;
  float cloud_coverage   = 0.5f;
  float cloud_density    = 1.0f;
  float cloud_bottom_alt = 2000.0f;
  float cloud_top_alt    = 7000.0f;

  // ----- Time of Day ----------------------------------------------------------
  bool  tod_enabled = false;
  float tod_hours   = 10.0f;   // [0, 24)
  float tod_speed   = 1.0f;    // 1x = 24h in 1 real minute
  // 是否正在『播放』昼夜推进。编辑器暂停时(tod_playing=false)云端应静止在
  // tod_hours,不可仅凭 tod_speed>0 就一直推进 —— 否则编辑器暂停、云端照动。
  bool  tod_playing = false;

  // ----- HDR sky tint (derived) -----------------------------------------------
  glm::vec3 hdr_sky_tint = {1.0f, 1.0f, 1.0f};

  // ----- I/O ------------------------------------------------------------------
  /// Load from a `.environment.json` file.  Missing keys keep struct defaults.
  /// Returns false if the file cannot be opened or JSON parsing fails.
  static bool load(const std::string& path, EnvironmentSettings& out);

  /// Save to a `.environment.json` file.  Returns false on I/O error.
  static bool save(const std::string& path, const EnvironmentSettings& s);
};

}  // namespace hongtu::renderer
