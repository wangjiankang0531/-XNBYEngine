#include <gtest/gtest.h>

#include "hongtu/renderer/environment/environment_settings.hpp"

#include <filesystem>
#include <string>

using hongtu::renderer::EnvironmentSettings;

TEST(EnvironmentSettings, DefaultsAreBright) {
  EnvironmentSettings s;
  EXPECT_FLOAT_EQ(s.sun_intensity, 6.0f);
  EXPECT_FLOAT_EQ(s.sun_elevation_deg, 45.0f);
  EXPECT_TRUE(s.ibl_enabled);
  EXPECT_TRUE(s.procedural_sky);
  EXPECT_EQ(s.tonemap_mode, 1);
}

TEST(EnvironmentSettings, JsonRoundTrip) {
  EnvironmentSettings a;
  a.godray_intensity  = 0.8f;
  a.grade_saturation  = 1.6f;
  a.tod_enabled       = true;
  a.tod_hours         = 12.0f;

  auto tmp = std::filesystem::temp_directory_path() / "env_rt.environment.json";
  ASSERT_TRUE(EnvironmentSettings::save(tmp.string(), a));

  EnvironmentSettings b;
  ASSERT_TRUE(EnvironmentSettings::load(tmp.string(), b));
  EXPECT_FLOAT_EQ(b.godray_intensity, 0.8f);
  EXPECT_FLOAT_EQ(b.grade_saturation, 1.6f);
  EXPECT_TRUE(b.tod_enabled);
  EXPECT_FLOAT_EQ(b.tod_hours, 12.0f);
}

TEST(EnvironmentSettings, LoadsExistingEditorFormat) {
  // Tests run from build/tests/; anchor to the source tree via the CMake macro.
  std::string path = std::string(HT_ENGINE_SOURCE_DIR)
                   + "/test_assets/env_parity/presets/godray.environment.json";

  EnvironmentSettings s;
  ASSERT_TRUE(EnvironmentSettings::load(path, s));
  EXPECT_FLOAT_EQ(s.godray_intensity, 0.8f);
}
