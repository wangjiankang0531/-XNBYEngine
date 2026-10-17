#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  define SDL_MAIN_HANDLED
#  include <SDL2/SDL.h>
#  include <SDL2/SDL_vulkan.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace HongTu::RHI;
using HongTu::Asset::ShaderCompiler;

namespace {

  constexpr float kPi = 3.14159265358979323846f;

  struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv;
    float textureBlend = 0.0f;

    static VertexInputBindingDescription binding() {
      VertexInputBindingDescription b;
      b.binding = 0;
      b.stride = sizeof(Vertex);
      b.perInstance = false;
      return b;
    }

    static std::vector<VertexInputAttributeDescription> attributes() {
      std::vector<VertexInputAttributeDescription> attrs(5);
      attrs[0] = {0, 0, Format::R32G32B32_SFLOAT, offsetof(Vertex, position)};
      attrs[1] = {1, 0, Format::R32G32B32_SFLOAT, offsetof(Vertex, color)};
      attrs[2] = {2, 0, Format::R32G32B32_SFLOAT, offsetof(Vertex, normal)};
      attrs[3] = {3, 0, Format::R32G32_SFLOAT, offsetof(Vertex, uv)};
      attrs[4] = {4, 0, Format::R32_SFLOAT, offsetof(Vertex, textureBlend)};
      return attrs;
    }
  };

  struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
  };

  struct PushData {
    glm::mat4 mvp;
    glm::vec4 camera;
    glm::vec4 light;
    glm::vec4 fog;
    glm::vec4 weather;
  };

  struct BadalingWeatherSettings {
    std::string name = "clear";
    glm::vec3 lightDir = {-0.42f, -0.28f, 0.86f};
    float lightIntensity = 1.0f;
    glm::vec3 fogColor = {0.48f, 0.66f, 0.86f};
    float fogMax = 0.66f;
    float fogStart = 360.0f;
    float fogEnd = 1340.0f;
    float rain = 0.0f;
    float snow = 0.0f;
    float cloudAmount = 1.0f;
    glm::vec3 skyColor = {0.50f, 0.67f, 0.87f};
  };

  struct BadalingSceneStats {
    bool realAsset = false;
    uint32_t wallPaths = 0;
    uint32_t cloudClusters = 0;
    uint32_t watchTowers = 0;
    uint32_t broadleafTrees = 0;
    uint32_t pineTrees = 0;
    uint32_t shrubs = 0;
    uint32_t vertices = 0;
    uint32_t indices = 0;
  };

  struct TerrainTextureBinding {
    RHIDescriptorSetLayoutPtr layout;
    RHIDescriptorSetPtr descriptorSet;
    RHITexturePtr texture;
    RHITextureViewPtr view;
    RHISamplerPtr sampler;
  };

  struct BadalingAsset {
    bool attempted = false;
    bool loaded = false;
    uint32_t width = 0;
    uint32_t height = 0;
    float minX = -320.0f;
    float maxX = 320.0f;
    float minY = -320.0f;
    float maxY = 320.0f;
    float baseElevation = 0.0f;
    std::vector<float> heights;
    std::vector<uint8_t> colors;
    std::vector<std::vector<glm::vec2>> walls;
  };

  BadalingAsset& badalingAsset() {
    static BadalingAsset asset;
    return asset;
  }

  BadalingWeatherSettings& activeWeather() {
    static BadalingWeatherSettings settings;
    return settings;
  }

  BadalingSceneStats& activeSceneStats() {
    static BadalingSceneStats stats;
    return stats;
  }

  std::string normalizePresetName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    if (value == "overview")
      return "clear";
    return value;
  }

  bool configureBadalingPreset(const std::string& preset) {
    const std::string name = normalizePresetName(preset);
    BadalingWeatherSettings settings;
    settings.name = name;

    if (name == "clear") {
      settings.lightDir = {-0.42f, -0.28f, 0.86f};
      settings.lightIntensity = 1.0f;
      settings.fogColor = {0.48f, 0.66f, 0.86f};
      settings.fogMax = 0.58f;
      settings.fogStart = 430.0f;
      settings.fogEnd = 1480.0f;
      settings.skyColor = {0.50f, 0.67f, 0.87f};
    } else if (name == "cloudy") {
      settings.lightDir = {-0.35f, -0.36f, 0.72f};
      settings.lightIntensity = 0.70f;
      settings.fogColor = {0.55f, 0.63f, 0.70f};
      settings.fogMax = 0.72f;
      settings.fogStart = 300.0f;
      settings.fogEnd = 1120.0f;
      settings.cloudAmount = 1.50f;
      settings.skyColor = {0.43f, 0.50f, 0.58f};
    } else if (name == "rain") {
      settings.lightDir = {-0.28f, -0.40f, 0.58f};
      settings.lightIntensity = 0.56f;
      settings.fogColor = {0.34f, 0.40f, 0.47f};
      settings.fogMax = 0.82f;
      settings.fogStart = 180.0f;
      settings.fogEnd = 760.0f;
      settings.rain = 0.88f;
      settings.cloudAmount = 1.35f;
      settings.skyColor = {0.22f, 0.27f, 0.33f};
    } else if (name == "snow") {
      settings.lightDir = {-0.30f, -0.22f, 0.78f};
      settings.lightIntensity = 0.82f;
      settings.fogColor = {0.74f, 0.80f, 0.86f};
      settings.fogMax = 0.78f;
      settings.fogStart = 210.0f;
      settings.fogEnd = 850.0f;
      settings.snow = 0.90f;
      settings.cloudAmount = 1.20f;
      settings.skyColor = {0.66f, 0.72f, 0.78f};
    } else if (name == "fog") {
      settings.lightDir = {-0.22f, -0.18f, 0.62f};
      settings.lightIntensity = 0.52f;
      settings.fogColor = {0.70f, 0.73f, 0.76f};
      settings.fogMax = 0.92f;
      settings.fogStart = 70.0f;
      settings.fogEnd = 390.0f;
      settings.skyColor = {0.61f, 0.64f, 0.67f};
    } else if (name == "night") {
      settings.lightDir = {-0.18f, 0.22f, 0.46f};
      settings.lightIntensity = 0.22f;
      settings.fogColor = {0.06f, 0.08f, 0.12f};
      settings.fogMax = 0.70f;
      settings.fogStart = 260.0f;
      settings.fogEnd = 980.0f;
      settings.skyColor = {0.04f, 0.05f, 0.08f};
    } else {
      return false;
    }

    activeWeather() = settings;
    return true;
  }

  void overrideWeatherControls(bool hasRain,
                               float rain,
                               bool hasSnow,
                               float snow,
                               bool hasFogDensity,
                               float fogDensity,
                               bool hasCloudAmount,
                               float cloudAmount) {
    auto& settings = activeWeather();
    if (hasRain)
      settings.rain = glm::clamp(rain, 0.0f, 1.0f);
    if (hasSnow)
      settings.snow = glm::clamp(snow, 0.0f, 1.0f);
    if (hasFogDensity)
      settings.fogMax = glm::clamp(fogDensity, 0.0f, 0.96f);
    if (hasCloudAmount)
      settings.cloudAmount = glm::clamp(cloudAmount, 0.0f, 2.0f);
  }

  glm::vec3 sunDirectionFromAngles(float azimuthDegrees, float elevationDegrees) {
    const float az = glm::radians(azimuthDegrees);
    const float el = glm::radians(glm::clamp(elevationDegrees, 2.0f, 88.0f));
    const float horizontal = std::cos(el);
    return glm::normalize(glm::vec3{std::sin(az) * horizontal, std::cos(az) * horizontal, std::sin(el)});
  }

  float sunAzimuthDegrees(const glm::vec3& direction) {
    const glm::vec3 d = glm::normalize(direction);
    float az = glm::degrees(std::atan2(d.x, d.y));
    return az < 0.0f ? az + 360.0f : az;
  }

  float sunElevationDegrees(const glm::vec3& direction) {
    const glm::vec3 d = glm::normalize(direction);
    return glm::degrees(std::asin(glm::clamp(d.z, -1.0f, 1.0f)));
  }

  void overrideSunAngles(bool hasAzimuth, float azimuthDegrees, bool hasElevation, float elevationDegrees) {
    auto& settings = activeWeather();
    const float az = hasAzimuth ? azimuthDegrees : sunAzimuthDegrees(settings.lightDir);
    const float el = hasElevation ? elevationDegrees : sunElevationDegrees(settings.lightDir);
    settings.lightDir = sunDirectionFromAngles(az, el);
  }

  ShaderStageFlags badalingPushStages() {
    return static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
           static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  }

  PushData makePushData(const glm::mat4& mvp, const glm::vec3& cameraPosition, float timeSeconds) {
    const auto& settings = activeWeather();
    PushData push;
    push.mvp = mvp;
    push.camera = glm::vec4(cameraPosition, timeSeconds);
    push.light = glm::vec4(glm::normalize(settings.lightDir), settings.lightIntensity);
    push.fog = glm::vec4(settings.fogColor, settings.fogMax);
    push.weather = glm::vec4(settings.fogStart, settings.fogEnd, settings.rain, settings.snow);
    return push;
  }

  bool writeBadalingSceneStats(const std::string& path) {
    if (path.empty())
      return true;

    std::ofstream output(path);
    if (!output) {
      std::cerr << "failed to open stats output: " << path << std::endl;
      return false;
    }

    const auto& stats = activeSceneStats();
    const auto& weather = activeWeather();
    output << "{\n";
    output << "  \"preset\": \"" << weather.name << "\",\n";
    output << "  \"real_asset\": " << (stats.realAsset ? "true" : "false") << ",\n";
    output << "  \"wall_paths\": " << stats.wallPaths << ",\n";
    output << "  \"cloud_clusters\": " << stats.cloudClusters << ",\n";
    output << "  \"watch_towers\": " << stats.watchTowers << ",\n";
    output << "  \"broadleaf_trees\": " << stats.broadleafTrees << ",\n";
    output << "  \"pine_trees\": " << stats.pineTrees << ",\n";
    output << "  \"shrubs\": " << stats.shrubs << ",\n";
    output << "  \"vertices\": " << stats.vertices << ",\n";
    output << "  \"indices\": " << stats.indices << ",\n";
    output << "  \"sun_azimuth_degrees\": " << sunAzimuthDegrees(weather.lightDir) << ",\n";
    output << "  \"sun_elevation_degrees\": " << sunElevationDegrees(weather.lightDir) << ",\n";
    output << "  \"light_intensity\": " << weather.lightIntensity << ",\n";
    output << "  \"fog_density\": " << weather.fogMax << ",\n";
    output << "  \"cloud_amount\": " << weather.cloudAmount << ",\n";
    output << "  \"rain\": " << weather.rain << ",\n";
    output << "  \"snow\": " << weather.snow << "\n";
    output << "}\n";
    return true;
  }

  template <typename T> bool readBinary(std::ifstream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(input);
  }

  bool loadBadalingAsset() {
    auto& asset = badalingAsset();
    if (asset.attempted)
      return asset.loaded;
    asset.attempted = true;

    const std::vector<std::string> candidates = {
        "assets/badaling/badaling_terrain.bin",
        "../assets/badaling/badaling_terrain.bin",
        "../../../../assets/badaling/badaling_terrain.bin",
        "D:/Project/yhq/htengine/assets/badaling/badaling_terrain.bin",
    };

    std::ifstream input;
    std::string path;
    for (const auto& candidate : candidates) {
      input.open(candidate, std::ios::binary);
      if (input) {
        path = candidate;
        break;
      }
      input.clear();
    }
    if (!input)
      return false;

    char magic[8] = {};
    input.read(magic, sizeof(magic));
    if (std::memcmp(magic, "HTBDALNG", 8) != 0)
      return false;

    uint32_t version = 0;
    uint32_t wallCount = 0;
    if (!readBinary(input, version) || !readBinary(input, asset.width) || !readBinary(input, asset.height) ||
        !readBinary(input, wallCount)) {
      return false;
    }
    if (version < 2 || asset.width < 2 || asset.height < 2 || asset.width > 4096 || asset.height > 4096) {
      return false;
    }

    if (!readBinary(input, asset.minX) || !readBinary(input, asset.maxX) || !readBinary(input, asset.minY) ||
        !readBinary(input, asset.maxY) || !readBinary(input, asset.baseElevation)) {
      return false;
    }

    const size_t pixelCount = static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height);
    asset.heights.resize(pixelCount);
    asset.colors.resize(pixelCount * 3);
    input.read(reinterpret_cast<char*>(asset.heights.data()),
               static_cast<std::streamsize>(asset.heights.size() * sizeof(float)));
    input.read(reinterpret_cast<char*>(asset.colors.data()), static_cast<std::streamsize>(asset.colors.size()));
    if (!input)
      return false;

    asset.walls.clear();
    asset.walls.reserve(wallCount);
    for (uint32_t i = 0; i < wallCount; ++i) {
      uint32_t pointCount = 0;
      if (!readBinary(input, pointCount))
        return false;
      if (pointCount < 2 || pointCount > 4096)
        return false;
      std::vector<glm::vec2> pathPoints(pointCount);
      input.read(reinterpret_cast<char*>(pathPoints.data()),
                 static_cast<std::streamsize>(pointCount * sizeof(glm::vec2)));
      if (!input)
        return false;
      asset.walls.push_back(std::move(pathPoints));
    }

    std::sort(asset.walls.begin(), asset.walls.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    asset.loaded = true;
    std::cout << "Loaded real Badaling assets: " << path << " (" << asset.width << "x" << asset.height
              << ", wall paths=" << asset.walls.size() << ")" << std::endl;
    return true;
  }

  float assetU(float x) {
    const auto& asset = badalingAsset();
    return glm::clamp((x - asset.minX) / std::max(asset.maxX - asset.minX, 0.001f), 0.0f, 1.0f);
  }

  float assetV(float y) {
    const auto& asset = badalingAsset();
    return glm::clamp((y - asset.minY) / std::max(asset.maxY - asset.minY, 0.001f), 0.0f, 1.0f);
  }

  float saturate(float v) {
    return glm::clamp(v, 0.0f, 1.0f);
  }

  float hash21(float x, float y) {
    return std::fmod(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f, 1.0f);
  }

  float positiveHash21(float x, float y) {
    float h = hash21(x, y);
    return h < 0.0f ? h + 1.0f : h;
  }

  float valueNoise(float x, float y) {
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float fx = x - ix;
    const float fy = y - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);

    const float a = positiveHash21(ix, iy);
    const float b = positiveHash21(ix + 1.0f, iy);
    const float c = positiveHash21(ix, iy + 1.0f);
    const float d = positiveHash21(ix + 1.0f, iy + 1.0f);
    return glm::mix(glm::mix(a, b, ux), glm::mix(c, d, ux), uy);
  }

  float fbm(float x, float y) {
    float sum = 0.0f;
    float amp = 0.55f;
    float freq = 1.0f;
    for (int i = 0; i < 5; ++i) {
      sum += (valueNoise(x * freq, y * freq) * 2.0f - 1.0f) * amp;
      freq *= 2.03f;
      amp *= 0.48f;
    }
    return sum;
  }

  float sampleAssetHeight(float x, float y) {
    const auto& asset = badalingAsset();
    if (!asset.loaded || asset.heights.empty())
      return 0.0f;
    const float fx = assetU(x) * static_cast<float>(asset.width - 1);
    const float fy = assetV(y) * static_cast<float>(asset.height - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    const uint32_t x1 = std::min(x0 + 1, asset.width - 1);
    const uint32_t y1 = std::min(y0 + 1, asset.height - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const auto at = [&](uint32_t px, uint32_t py) {
      return asset.heights[static_cast<size_t>(py) * asset.width + px];
    };
    const float a = glm::mix(at(x0, y0), at(x1, y0), tx);
    const float b = glm::mix(at(x0, y1), at(x1, y1), tx);
    return glm::mix(a, b, ty);
  }

  glm::vec3 sampleAssetColor(float x, float y) {
    const auto& asset = badalingAsset();
    if (!asset.loaded || asset.colors.empty())
      return {0.24f, 0.34f, 0.18f};
    const float fx = assetU(x) * static_cast<float>(asset.width - 1);
    const float fy = assetV(y) * static_cast<float>(asset.height - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    const uint32_t x1 = std::min(x0 + 1, asset.width - 1);
    const uint32_t y1 = std::min(y0 + 1, asset.height - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const auto at = [&](uint32_t px, uint32_t py) {
      const size_t i = (static_cast<size_t>(py) * asset.width + px) * 3;
      return glm::vec3{
          static_cast<float>(asset.colors[i + 0]) / 255.0f,
          static_cast<float>(asset.colors[i + 1]) / 255.0f,
          static_cast<float>(asset.colors[i + 2]) / 255.0f,
      };
    };
    const glm::vec3 a = glm::mix(at(x0, y0), at(x1, y0), tx);
    const glm::vec3 b = glm::mix(at(x0, y1), at(x1, y1), tx);
    return glm::mix(a, b, ty);
  }

  float assetOutsideDistance(float x, float y) {
    const auto& asset = badalingAsset();
    const float cx = glm::clamp(x, asset.minX, asset.maxX);
    const float cy = glm::clamp(y, asset.minY, asset.maxY);
    return glm::length(glm::vec2{x - cx, y - cy});
  }

  float ridgeCenterY(float x) {
    const float t = (x + 260.0f) / 520.0f;
    return 42.0f * std::sin(t * kPi * 2.4f) + 18.0f * std::sin(t * kPi * 7.0f);
  }

  float terrainHeight(float x, float y) {
    if (loadBadalingAsset()) {
      const auto& asset = badalingAsset();
      const float cx = glm::clamp(x, asset.minX, asset.maxX);
      const float cy = glm::clamp(y, asset.minY, asset.maxY);
      const float edge = sampleAssetHeight(cx, cy);
      const float outside = glm::length(glm::vec2{x - cx, y - cy});
      if (outside < 0.01f)
        return edge;

      const float t = saturate(outside / 560.0f);
      const float broad = 14.0f * std::sin(x * 0.0065f + y * 0.0037f) + 10.0f * std::cos(x * 0.0031f - y * 0.0072f) +
                          18.0f * fbm(x * 0.0048f + 17.0f, y * 0.0048f - 9.0f);
      const float distantLift = 22.0f * t + 10.0f * std::sin((x - y) * 0.0038f);
      return std::max(0.0f, edge * (1.0f - 0.18f * t) + broad * (0.35f + 0.65f * t) + distantLift);
    }

    const float ridge = ridgeCenterY(x);
    const float dist = y - ridge;
    const float mainRidge = 58.0f * std::exp(-(dist * dist) / (2.0f * 42.0f * 42.0f));
    const float sideRidge = 23.0f * std::exp(-((y + 105.0f) * (y + 105.0f)) / (2.0f * 80.0f * 80.0f));
    const float shoulder = 13.0f * std::exp(-((dist - 58.0f) * (dist - 58.0f)) / (2.0f * 34.0f * 34.0f));
    const float ravine = -10.0f * std::exp(-((dist + 62.0f) * (dist + 62.0f)) / (2.0f * 28.0f * 28.0f));
    const float rolling =
        11.0f * std::sin(x * 0.027f) + 8.0f * std::cos(y * 0.031f) + 5.0f * std::sin((x + y) * 0.018f);
    const float rockyDetail = 7.0f * fbm(x * 0.018f, y * 0.018f) + 3.0f * fbm(x * 0.055f + 19.0f, y * 0.055f - 7.0f);
    const float ridgeRoughness = (1.0f - saturate(std::abs(dist) / 115.0f)) * 5.5f * std::sin(x * 0.071f + y * 0.033f);
    return mainRidge + sideRidge + shoulder + ravine + rolling + rockyDetail + ridgeRoughness;
  }

  glm::vec3 terrainNormal(float x, float y) {
    const float step =
        loadBadalingAsset()
            ? std::max((badalingAsset().maxX - badalingAsset().minX) / static_cast<float>(badalingAsset().width - 1),
                       1.0f)
            : 2.4f;
    const float hx = terrainHeight(x + step, y) - terrainHeight(x - step, y);
    const float hy = terrainHeight(x, y + step) - terrainHeight(x, y - step);
    return glm::normalize(glm::vec3{-hx, -hy, step * 2.0f});
  }

  glm::vec3 terrainColor(float x, float y, float z, const glm::vec3& normal) {
    if (loadBadalingAsset()) {
      const float slope = 1.0f - normal.z;
      const float outside = assetOutsideDistance(x, y);
      glm::vec3 c = sampleAssetColor(x, y);
      const float greenSignal = glm::clamp(c.g - std::max(c.r, c.b), 0.0f, 0.22f);
      const float canopy = saturate(0.38f + greenSignal * 3.8f + c.g * 0.52f);
      const float canopyNoise = saturate(0.48f + 0.52f * fbm(x * 0.020f + 6.0f, y * 0.020f - 11.0f));
      const glm::vec3 deepGreen = glm::mix(glm::vec3{0.08f, 0.24f, 0.09f}, glm::vec3{0.19f, 0.42f, 0.16f}, canopyNoise);
      c = glm::mix(c, deepGreen, canopy * 0.54f);

      const float bareRockMix = saturate(slope * 1.15f + (fbm(x * 0.030f - 4.0f, y * 0.030f + 8.0f) - 0.10f) * 0.48f);
      const glm::vec3 warmRock = {0.47f, 0.38f, 0.27f};
      const float scrub = saturate(0.42f + 0.58f * fbm(x * 0.012f - 31.0f, y * 0.012f + 14.0f));
      const float soilVein = saturate((fbm(x * 0.026f + scrub * 2.4f, y * 0.018f - 5.0f) + 0.28f) * 0.72f);
      const float foliageFine = saturate(0.50f + 0.50f * valueNoise(x * 0.115f + 2.0f, y * 0.115f - 7.0f));
      c = glm::mix(c, glm::vec3{0.24f, 0.40f, 0.17f}, scrub * 0.16f);
      c = glm::mix(c, warmRock, bareRockMix * 0.25f + soilVein * 0.13f);
      c *= 0.90f + foliageFine * 0.18f;

      if (outside > 0.0f) {
        const float distanceFade = saturate(outside / 720.0f);
        c = glm::mix(c, glm::vec3{0.23f, 0.42f, 0.32f}, saturate(outside / 260.0f) * 0.55f);
        c = glm::mix(c, glm::vec3{0.28f, 0.48f, 0.60f}, distanceFade * 0.42f);
      }
      return glm::clamp(c * 1.12f, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    const float ridge = std::abs(y - ridgeCenterY(x));
    const float slope = 1.0f - normal.z;
    const float patch = saturate(0.50f + 0.50f * fbm(x * 0.035f + 4.0f, y * 0.035f - 6.0f));

    glm::vec3 grass = glm::mix(glm::vec3{0.15f, 0.30f, 0.14f}, glm::vec3{0.25f, 0.39f, 0.18f}, patch);
    glm::vec3 dry = {0.48f, 0.43f, 0.28f};
    glm::vec3 rock = {0.39f, 0.35f, 0.29f};
    const float h = saturate((z + 8.0f) / 105.0f);
    glm::vec3 c = glm::mix(grass, dry, h * 0.72f);
    c = glm::mix(c, rock, saturate(slope * 2.4f));
    if (ridge < 28.0f)
      c = glm::mix(c, rock, 0.28f);
    return c * (0.92f + 0.08f * patch);
  }

  uint32_t addVertex(Mesh& mesh, const glm::vec3& p, const glm::vec3& c, const glm::vec3& n = {0.0f, 0.0f, 1.0f}) {
    mesh.vertices.push_back({p, c, glm::normalize(n), {0.0f, 0.0f}, 0.0f});
    return static_cast<uint32_t>(mesh.vertices.size() - 1);
  }

  void addTri(Mesh& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
  }

  void addQuad(Mesh& mesh,
               const glm::vec3& a,
               const glm::vec3& b,
               const glm::vec3& c,
               const glm::vec3& d,
               const glm::vec3& color) {
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
    const uint32_t ia = addVertex(mesh, a, color, normal);
    const uint32_t ib = addVertex(mesh, b, color, normal);
    const uint32_t ic = addVertex(mesh, c, color, normal);
    const uint32_t id = addVertex(mesh, d, color, normal);
    addTri(mesh, ia, ib, ic);
    addTri(mesh, ia, ic, id);
  }

  void addBox(Mesh& mesh, const glm::vec3& center, const glm::vec3& half, const glm::vec3& color) {
    const glm::vec3 p000 = center + glm::vec3{-half.x, -half.y, -half.z};
    const glm::vec3 p100 = center + glm::vec3{half.x, -half.y, -half.z};
    const glm::vec3 p110 = center + glm::vec3{half.x, half.y, -half.z};
    const glm::vec3 p010 = center + glm::vec3{-half.x, half.y, -half.z};
    const glm::vec3 p001 = center + glm::vec3{-half.x, -half.y, half.z};
    const glm::vec3 p101 = center + glm::vec3{half.x, -half.y, half.z};
    const glm::vec3 p111 = center + glm::vec3{half.x, half.y, half.z};
    const glm::vec3 p011 = center + glm::vec3{-half.x, half.y, half.z};

    addQuad(mesh, p001, p101, p111, p011, color * 1.10f);
    addQuad(mesh, p000, p010, p110, p100, color * 0.65f);
    addQuad(mesh, p000, p001, p011, p010, color * 0.86f);
    addQuad(mesh, p100, p110, p111, p101, color * 0.82f);
    addQuad(mesh, p010, p011, p111, p110, color * 0.92f);
    addQuad(mesh, p000, p100, p101, p001, color * 0.76f);
  }

  void addOrientedBox(Mesh& mesh,
                      const glm::vec2& center,
                      const glm::vec2& forward,
                      float halfLength,
                      float halfWidth,
                      float centerZ,
                      float halfZ,
                      const glm::vec3& color) {
    glm::vec2 dir = forward;
    const float len = glm::length(dir);
    if (len < 0.001f)
      return;
    dir /= len;
    const glm::vec2 n = {-dir.y, dir.x};
    const glm::vec2 dl = dir * halfLength;
    const glm::vec2 dw = n * halfWidth;

    const glm::vec2 p00 = center - dl - dw;
    const glm::vec2 p10 = center + dl - dw;
    const glm::vec2 p11 = center + dl + dw;
    const glm::vec2 p01 = center - dl + dw;
    const float z0 = centerZ - halfZ;
    const float z1 = centerZ + halfZ;

    const glm::vec3 a0 = {p00.x, p00.y, z0};
    const glm::vec3 b0 = {p10.x, p10.y, z0};
    const glm::vec3 c0 = {p11.x, p11.y, z0};
    const glm::vec3 d0 = {p01.x, p01.y, z0};
    const glm::vec3 a1 = {p00.x, p00.y, z1};
    const glm::vec3 b1 = {p10.x, p10.y, z1};
    const glm::vec3 c1 = {p11.x, p11.y, z1};
    const glm::vec3 d1 = {p01.x, p01.y, z1};

    addQuad(mesh, a1, b1, c1, d1, color * 1.08f);
    addQuad(mesh, a0, d0, c0, b0, color * 0.60f);
    addQuad(mesh, a0, a1, d1, d0, color * 0.82f);
    addQuad(mesh, b0, c0, c1, b1, color * 0.78f);
    addQuad(mesh, d0, d1, c1, c0, color * 0.94f);
    addQuad(mesh, a0, b0, b1, a1, color * 0.72f);
  }

  void addCylinder(
      Mesh& mesh, const glm::vec3& baseCenter, float radius, float height, int sides, const glm::vec3& color) {
    const uint32_t bottomCenter = addVertex(mesh, baseCenter, color * 0.62f, {0.0f, 0.0f, -1.0f});
    const uint32_t topCenter =
        addVertex(mesh, baseCenter + glm::vec3{0.0f, 0.0f, height}, color * 1.08f, {0.0f, 0.0f, 1.0f});
    std::vector<uint32_t> bottom;
    std::vector<uint32_t> top;
    bottom.reserve(sides);
    top.reserve(sides);

    for (int i = 0; i < sides; ++i) {
      const float a = static_cast<float>(i) / static_cast<float>(sides) * kPi * 2.0f;
      const glm::vec3 offset = {std::cos(a) * radius, std::sin(a) * radius, 0.0f};
      const glm::vec3 normal = glm::normalize(glm::vec3{offset.x, offset.y, 0.28f});
      const float shade = 0.76f + 0.24f * saturate(std::cos(a - 0.8f) * 0.5f + 0.5f);
      bottom.push_back(addVertex(mesh, baseCenter + offset, color * shade, normal));
      top.push_back(
          addVertex(mesh, baseCenter + offset + glm::vec3{0.0f, 0.0f, height}, color * (shade + 0.08f), normal));
    }

    for (int i = 0; i < sides; ++i) {
      const int j = (i + 1) % sides;
      addTri(mesh, bottomCenter, bottom[j], bottom[i]);
      addTri(mesh, topCenter, top[i], top[j]);
      addTri(mesh, bottom[i], bottom[j], top[j]);
      addTri(mesh, bottom[i], top[j], top[i]);
    }
  }

  void addFrustum(Mesh& mesh,
                  const glm::vec3& baseCenter,
                  float bottomRadius,
                  float topRadius,
                  float height,
                  int sides,
                  const glm::vec3& color) {
    const uint32_t topCenter =
        addVertex(mesh, baseCenter + glm::vec3{0.0f, 0.0f, height}, color * 1.12f, {0.0f, 0.0f, 1.0f});
    std::vector<uint32_t> bottom;
    std::vector<uint32_t> top;
    bottom.reserve(sides);
    top.reserve(sides);

    for (int i = 0; i < sides; ++i) {
      const float a = static_cast<float>(i) / static_cast<float>(sides) * kPi * 2.0f;
      const glm::vec3 lowOffset = {std::cos(a) * bottomRadius, std::sin(a) * bottomRadius, 0.0f};
      const glm::vec3 highOffset = {std::cos(a) * topRadius, std::sin(a) * topRadius, height};
      const glm::vec3 normal =
          glm::normalize(glm::vec3{std::cos(a), std::sin(a), (bottomRadius - topRadius) / std::max(height, 0.01f)});
      const float shade = 0.70f + 0.30f * saturate(std::cos(a - 0.6f) * 0.5f + 0.5f);
      bottom.push_back(addVertex(mesh, baseCenter + lowOffset, color * shade, normal));
      top.push_back(addVertex(mesh, baseCenter + highOffset, color * (shade + 0.08f), normal));
    }

    for (int i = 0; i < sides; ++i) {
      const int j = (i + 1) % sides;
      addTri(mesh, bottom[i], bottom[j], top[j]);
      addTri(mesh, bottom[i], top[j], top[i]);
      if (topRadius < 0.12f) {
        addTri(mesh, topCenter, top[i], top[j]);
      }
    }
  }

  void addEllipsoid(Mesh& mesh,
                    const glm::vec3& center,
                    const glm::vec3& radius,
                    const glm::vec3& color,
                    int rings = 6,
                    int slices = 16) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int iy = 0; iy <= rings; ++iy) {
      const float v = static_cast<float>(iy) / static_cast<float>(rings);
      const float theta = -kPi * 0.5f + v * kPi;
      const float ct = std::cos(theta);
      const float st = std::sin(theta);
      for (int ix = 0; ix <= slices; ++ix) {
        const float u = static_cast<float>(ix) / static_cast<float>(slices);
        const float phi = u * kPi * 2.0f;
        const glm::vec3 unit = {ct * std::cos(phi), ct * std::sin(phi), st};
        const glm::vec3 p = center + glm::vec3{unit.x * radius.x, unit.y * radius.y, unit.z * radius.z};
        const glm::vec3 n = glm::normalize(glm::vec3{
            unit.x / std::max(radius.x, 0.01f),
            unit.y / std::max(radius.y, 0.01f),
            unit.z / std::max(radius.z, 0.01f),
        });
        mesh.vertices.push_back({p, color, n, {0.0f, 0.0f}, 0.0f});
      }
    }
    for (int iy = 0; iy < rings; ++iy) {
      for (int ix = 0; ix < slices; ++ix) {
        const uint32_t a = base + static_cast<uint32_t>(iy * (slices + 1) + ix);
        const uint32_t b = a + 1;
        const uint32_t c = a + static_cast<uint32_t>(slices + 1);
        const uint32_t d = c + 1;
        addTri(mesh, a, c, b);
        addTri(mesh, b, c, d);
      }
    }
  }

  void addCloudCluster(Mesh& mesh, const glm::vec3& center, float scale) {
    ++activeSceneStats().cloudClusters;
    const glm::vec3 white = {0.92f, 0.96f, 1.00f};
    const glm::vec3 shade = {0.70f, 0.80f, 0.90f};
    addEllipsoid(mesh, center, {42.0f * scale, 16.0f * scale, 7.0f * scale}, white, 5, 14);
    addEllipsoid(mesh,
                 center + glm::vec3{-28.0f * scale, 2.0f * scale, -1.5f * scale},
                 {28.0f * scale, 12.0f * scale, 5.2f * scale},
                 shade,
                 5,
                 12);
    addEllipsoid(mesh,
                 center + glm::vec3{24.0f * scale, -2.0f * scale, 0.0f},
                 {30.0f * scale, 13.0f * scale, 5.8f * scale},
                 white * 0.96f,
                 5,
                 12);
    addEllipsoid(mesh,
                 center + glm::vec3{0.0f, 6.0f * scale, 5.0f * scale},
                 {23.0f * scale, 11.0f * scale, 7.0f * scale},
                 white,
                 5,
                 12);
  }

  size_t visibleCloudSlots(size_t slotCount) {
    const float amount = glm::clamp(activeWeather().cloudAmount, 0.0f, 2.0f);
    return std::min(slotCount, static_cast<size_t>(std::round(static_cast<float>(slotCount) * amount * 0.5f)));
  }

  void addCloudSlots(Mesh& mesh, const std::vector<std::pair<glm::vec3, float>>& slots) {
    const size_t visible = visibleCloudSlots(slots.size());
    const float scaleBoost = 0.86f + glm::clamp(activeWeather().cloudAmount, 0.0f, 2.0f) * 0.14f;
    for (size_t i = 0; i < visible; ++i) {
      addCloudCluster(mesh, slots[i].first, slots[i].second * scaleBoost);
    }
  }

  void addSkyElements(Mesh& mesh) {
    if (loadBadalingAsset()) {
      const auto& asset = badalingAsset();
      const float cx = (asset.minX + asset.maxX) * 0.5f;
      const float cy = (asset.minY + asset.maxY) * 0.5f;
      addCloudSlots(mesh,
                    {
                        {{cx - 780.0f, cy + 760.0f, 270.0f}, 1.65f},
                        {{cx - 220.0f, cy + 1080.0f, 315.0f}, 1.35f},
                        {{cx + 520.0f, cy + 820.0f, 295.0f}, 1.55f},
                        {{cx + 910.0f, cy + 120.0f, 280.0f}, 1.25f},
                        {{cx + 540.0f, cy - 760.0f, 300.0f}, 1.45f},
                        {{cx - 260.0f, cy - 940.0f, 285.0f}, 1.35f},
                        {{cx - 880.0f, cy - 240.0f, 310.0f}, 1.20f},
                        {{cx + 80.0f, cy + 360.0f, 360.0f}, 0.82f},
                    });
      addEllipsoid(mesh,
                   {asset.maxX + 420.0f, asset.maxY + 420.0f, 430.0f},
                   {90.0f, 90.0f, 18.0f},
                   {0.76f, 0.78f, 0.70f},
                   7,
                   18);
      addEllipsoid(
          mesh, {asset.maxX + 420.0f, asset.maxY + 420.0f, 430.0f}, {36.0f, 36.0f, 36.0f}, {1.0f, 0.86f, 0.48f}, 8, 18);
      return;
    }
    addCloudSlots(mesh, {{{-260.0f, 420.0f, 210.0f}, 1.0f}, {{190.0f, 460.0f, 235.0f}, 1.1f}});
  }

  void addViewSkyElements(Mesh& mesh, const std::vector<std::vector<glm::vec2>>& paths) {
    if (paths.empty() || paths.front().size() < 12) {
      addSkyElements(mesh);
      return;
    }

    const auto& path = paths.front();
    const size_t i = path.size() / 2;
    const glm::vec2 p = path[i];
    const glm::vec2 q = path[std::min(i + 10, path.size() - 1)];
    glm::vec2 dir2 = q - p;
    if (glm::length(dir2) < 0.001f)
      dir2 = {1.0f, 0.0f};
    dir2 = glm::normalize(dir2);
    const glm::vec2 side = {-dir2.y, dir2.x};
    const float ground = terrainHeight(p.x, p.y);
    const glm::vec2 focus = p + dir2 * 24.0f;
    const glm::vec2 skyBase = focus + dir2 * 780.0f;

    addCloudSlots(mesh,
                  {
                      {{skyBase.x + side.x * 260.0f, skyBase.y + side.y * 260.0f, ground + 145.0f}, 1.45f},
                      {{skyBase.x - side.x * 180.0f + dir2.x * 120.0f,
                        skyBase.y - side.y * 180.0f + dir2.y * 120.0f,
                        ground + 170.0f},
                       1.20f},
                      {{skyBase.x - side.x * 500.0f + dir2.x * 260.0f,
                        skyBase.y - side.y * 500.0f + dir2.y * 260.0f,
                        ground + 130.0f},
                       1.70f},
                      {{skyBase.x + side.x * 620.0f + dir2.x * 360.0f,
                        skyBase.y + side.y * 620.0f + dir2.y * 360.0f,
                        ground + 155.0f},
                       1.35f},
                      {{skyBase.x + side.x * 160.0f - dir2.x * 220.0f,
                        skyBase.y + side.y * 160.0f - dir2.y * 220.0f,
                        ground + 190.0f},
                       1.10f},
                      {{skyBase.x - side.x * 760.0f + dir2.x * 520.0f,
                        skyBase.y - side.y * 760.0f + dir2.y * 520.0f,
                        ground + 150.0f},
                       1.25f},
                      {{skyBase.x + side.x * 820.0f + dir2.x * 700.0f,
                        skyBase.y + side.y * 820.0f + dir2.y * 700.0f,
                        ground + 182.0f},
                       1.18f},
                      {{skyBase.x - side.x * 260.0f + dir2.x * 680.0f,
                        skyBase.y - side.y * 260.0f + dir2.y * 680.0f,
                        ground + 210.0f},
                       0.92f},
                  });

    const glm::vec3 sunCenter = {
        skyBase.x + side.x * 520.0f + dir2.x * 520.0f, skyBase.y + side.y * 520.0f + dir2.y * 520.0f, ground + 250.0f};
    addEllipsoid(mesh, sunCenter, {98.0f, 98.0f, 18.0f}, {0.78f, 0.80f, 0.70f}, 7, 18);
    addEllipsoid(mesh, sunCenter, {34.0f, 34.0f, 34.0f}, {1.0f, 0.88f, 0.50f}, 8, 18);
  }

  void addSegmentPrism(Mesh& mesh,
                       const glm::vec2& a,
                       const glm::vec2& b,
                       float width,
                       float zOffset,
                       float height,
                       const glm::vec3& color) {
    glm::vec2 dir = b - a;
    const float len = glm::length(dir);
    if (len < 0.001f)
      return;
    dir /= len;
    const glm::vec2 n = {-dir.y, dir.x};
    const glm::vec2 al = a + n * (width * 0.5f);
    const glm::vec2 ar = a - n * (width * 0.5f);
    const glm::vec2 bl = b + n * (width * 0.5f);
    const glm::vec2 br = b - n * (width * 0.5f);

    const auto p0 = [zOffset](const glm::vec2& p) {
      return glm::vec3{p.x, p.y, terrainHeight(p.x, p.y) + zOffset};
    };
    const auto p1 = [zOffset, height](const glm::vec2& p) {
      return glm::vec3{p.x, p.y, terrainHeight(p.x, p.y) + zOffset + height};
    };

    glm::vec3 al0 = p0(al);
    glm::vec3 ar0 = p0(ar);
    glm::vec3 bl0 = p0(bl);
    glm::vec3 br0 = p0(br);
    glm::vec3 al1 = p1(al);
    glm::vec3 ar1 = p1(ar);
    glm::vec3 bl1 = p1(bl);
    glm::vec3 br1 = p1(br);

    addQuad(mesh, al1, bl1, br1, ar1, color * 1.10f);
    addQuad(mesh, al0, ar0, br0, bl0, color * 0.56f);
    addQuad(mesh, al0, bl0, bl1, al1, color * 0.82f);
    addQuad(mesh, ar0, ar1, br1, br0, color * 0.74f);
    addQuad(mesh, al0, al1, ar1, ar0, color * 0.68f);
    addQuad(mesh, bl0, br0, br1, bl1, color * 0.88f);
  }

  std::vector<glm::vec2> makeProceduralWallPath() {
    std::vector<glm::vec2> path;
    constexpr int kPoints = 156;
    path.reserve(kPoints);
    for (int i = 0; i < kPoints; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kPoints - 1);
      const float x = -258.0f + 516.0f * t;
      const float y = ridgeCenterY(x) + 8.5f * std::sin(t * kPi * 11.0f) + 4.8f * std::cos(t * kPi * 3.0f) +
                      2.4f * std::sin(t * kPi * 29.0f);
      path.push_back({x, y});
    }
    return path;
  }

  std::vector<glm::vec2> smoothDisplayWallPath(const std::vector<glm::vec2>& path) {
    if (path.size() < 4)
      return path;

    std::vector<glm::vec2> reduced;
    reduced.reserve(path.size());
    reduced.push_back(path.front());
    glm::vec2 last = path.front();
    for (size_t i = 1; i + 1 < path.size(); ++i) {
      if (glm::length(path[i] - last) >= 9.0f) {
        reduced.push_back(path[i]);
        last = path[i];
      }
    }
    if (glm::length(path.back() - reduced.back()) > 1.0f) {
      reduced.push_back(path.back());
    }
    if (reduced.size() < 4)
      return reduced;

    for (int pass = 0; pass < 2; ++pass) {
      std::vector<glm::vec2> next = reduced;
      for (size_t i = 1; i + 1 < reduced.size(); ++i) {
        next[i] = reduced[i] * 0.50f + (reduced[i - 1] + reduced[i + 1]) * 0.25f;
      }
      reduced = std::move(next);
    }
    return reduced;
  }

  float wallPathLength(const std::vector<glm::vec2>& path) {
    float total = 0.0f;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
      total += glm::length(path[i + 1] - path[i]);
    }
    return total;
  }

  std::vector<size_t> scenicWallOrder();

  bool satelliteLooksLikeWallConnector(const glm::vec2& a, const glm::vec2& b) {
    const float dist = glm::length(b - a);
    if (dist < 18.0f)
      return true;
    if (!loadBadalingAsset() || dist > 210.0f)
      return false;

    int paleSamples = 0;
    int usableSamples = 0;
    for (int i = 1; i <= 7; ++i) {
      const float t = static_cast<float>(i) / 8.0f;
      const glm::vec2 p = glm::mix(a, b, t);
      if (assetOutsideDistance(p.x, p.y) > 0.01f)
        continue;
      const glm::vec3 c = sampleAssetColor(p.x, p.y);
      const float luma = c.r * 0.30f + c.g * 0.59f + c.b * 0.11f;
      const float chroma = std::max({std::abs(c.r - c.g), std::abs(c.g - c.b), std::abs(c.r - c.b)});
      const bool paleStoneOrPath = luma > 0.31f && chroma < 0.18f && c.g < 0.48f;
      const bool dryLinearCut = luma > 0.27f && c.r > c.g * 0.86f && c.g > c.b * 0.86f;
      paleSamples += (paleStoneOrPath || dryLinearCut) ? 1 : 0;
      ++usableSamples;
    }
    return dist < 72.0f || (usableSamples > 0 && paleSamples >= 2);
  }

  std::vector<glm::vec2> reversedPath(const std::vector<glm::vec2>& path) {
    return std::vector<glm::vec2>(path.rbegin(), path.rend());
  }

  std::vector<std::vector<glm::vec2>> makeConnectedWallPaths() {
    const auto& walls = badalingAsset().walls;
    if (walls.empty())
      return {};

    const auto order = scenicWallOrder();
    std::vector<std::vector<glm::vec2>> displayPaths(walls.size());
    for (size_t i = 0; i < walls.size(); ++i) {
      displayPaths[i] = smoothDisplayWallPath(walls[i]);
    }

    std::vector<bool> used(walls.size(), false);
    std::vector<std::vector<glm::vec2>> chains;
    for (size_t seed : order) {
      if (seed >= walls.size() || used[seed] || displayPaths[seed].size() < 2)
        continue;
      std::vector<glm::vec2> chain = displayPaths[seed];
      used[seed] = true;

      bool grew = true;
      while (grew) {
        grew = false;
        float best = 185.0f;
        size_t bestIndex = static_cast<size_t>(-1);
        int mode = 0;

        for (size_t candidate : order) {
          if (candidate >= walls.size() || used[candidate] || displayPaths[candidate].size() < 2)
            continue;
          const auto& path = displayPaths[candidate];
          const glm::vec2 chainFront = chain.front();
          const glm::vec2 chainBack = chain.back();
          const glm::vec2 pathFront = path.front();
          const glm::vec2 pathBack = path.back();
          const std::array<std::pair<float, int>, 4> tests = {{
              {glm::length(chainBack - pathFront), 0},
              {glm::length(chainBack - pathBack), 1},
              {glm::length(chainFront - pathBack), 2},
              {glm::length(chainFront - pathFront), 3},
          }};
          for (const auto& test : tests) {
            if (test.first >= best)
              continue;
            glm::vec2 a = chainBack;
            glm::vec2 b = pathFront;
            if (test.second == 1)
              b = pathBack;
            if (test.second == 2) {
              a = chainFront;
              b = pathBack;
            } else if (test.second == 3) {
              a = chainFront;
              b = pathFront;
            }
            if (!satelliteLooksLikeWallConnector(a, b))
              continue;
            best = test.first;
            bestIndex = candidate;
            mode = test.second;
          }
        }

        if (bestIndex != static_cast<size_t>(-1)) {
          const auto& path = displayPaths[bestIndex];
          if (mode == 0) {
            chain.insert(chain.end(), path.begin(), path.end());
          } else if (mode == 1) {
            const auto r = reversedPath(path);
            chain.insert(chain.end(), r.begin(), r.end());
          } else if (mode == 2) {
            chain.insert(chain.begin(), path.begin(), path.end());
          } else {
            const auto r = reversedPath(path);
            chain.insert(chain.begin(), r.begin(), r.end());
          }
          used[bestIndex] = true;
          grew = true;
        }
      }

      if (wallPathLength(chain) > 80.0f) {
        chains.push_back(smoothDisplayWallPath(chain));
      }
    }

    std::sort(chains.begin(), chains.end(), [](const auto& a, const auto& b) {
      return wallPathLength(a) > wallPathLength(b);
    });
    if (chains.size() > 3)
      chains.resize(3);
    return chains;
  }

  std::vector<size_t> scenicWallOrder() {
    const auto& walls = badalingAsset().walls;
    std::vector<size_t> order;
    std::vector<bool> used(walls.size(), false);
    const auto add = [&](size_t i) {
      if (i < walls.size() && !used[i]) {
        order.push_back(i);
        used[i] = true;
      }
    };

    add(2);
    add(1);
    add(3);
    add(4);
    add(0);
    for (size_t i = 5; i < walls.size(); ++i)
      add(i);
    return order;
  }

  std::vector<glm::vec2> makeWallPath() {
    if (loadBadalingAsset() && !badalingAsset().walls.empty()) {
      const auto connected = makeConnectedWallPaths();
      if (!connected.empty())
        return connected.front();
    }
    return makeProceduralWallPath();
  }

  std::vector<std::vector<glm::vec2>> makeWallPaths() {
    if (loadBadalingAsset() && !badalingAsset().walls.empty()) {
      const auto connected = makeConnectedWallPaths();
      if (!connected.empty())
        return connected;
    }
    return {makeProceduralWallPath()};
  }

  void addTerrainPatch(Mesh& mesh, float minX, float maxX, float minY, float maxY, int gridX, int gridY) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

    for (int iy = 0; iy <= gridY; ++iy) {
      for (int ix = 0; ix <= gridX; ++ix) {
        const float u = static_cast<float>(ix) / static_cast<float>(gridX);
        const float v = static_cast<float>(iy) / static_cast<float>(gridY);
        const float x = glm::mix(minX, maxX, u);
        const float y = glm::mix(minY, maxY, v);
        const float z = terrainHeight(x, y);
        const glm::vec3 normal = terrainNormal(x, y);
        const bool textured = loadBadalingAsset() && assetOutsideDistance(x, y) < 0.01f;
        const glm::vec2 uv = textured ? glm::vec2{assetU(x), assetV(y)} : glm::vec2{0.0f, 0.0f};
        mesh.vertices.push_back({{x, y, z}, terrainColor(x, y, z, normal), normal, uv, textured ? 0.72f : 0.0f});
      }
    }

    for (int iy = 0; iy < gridY; ++iy) {
      for (int ix = 0; ix < gridX; ++ix) {
        const uint32_t a = base + static_cast<uint32_t>(iy * (gridX + 1) + ix);
        const uint32_t b = a + 1;
        const uint32_t c = a + static_cast<uint32_t>(gridX + 1);
        const uint32_t d = c + 1;
        addTri(mesh, a, c, b);
        addTri(mesh, b, c, d);
      }
    }
  }

  void addTerrain(Mesh& mesh) {
    const bool realAsset = loadBadalingAsset();
    constexpr float kSize = 640.0f;
    if (!realAsset) {
      addTerrainPatch(mesh, -kSize * 0.5f, kSize * 0.5f, -kSize * 0.5f, kSize * 0.5f, 172, 172);
      return;
    }

    const auto& asset = badalingAsset();
    addTerrainPatch(mesh, asset.minX, asset.maxX, asset.minY, asset.maxY, 448, 448);

    constexpr float kMargin = 760.0f;
    addTerrainPatch(mesh, asset.minX, asset.maxX, asset.maxY, asset.maxY + kMargin, 160, 56);
    addTerrainPatch(mesh, asset.minX, asset.maxX, asset.minY - kMargin, asset.minY, 160, 56);
    addTerrainPatch(mesh, asset.minX - kMargin, asset.minX, asset.minY, asset.maxY, 56, 160);
    addTerrainPatch(mesh, asset.maxX, asset.maxX + kMargin, asset.minY, asset.maxY, 56, 160);
    addTerrainPatch(mesh, asset.minX - kMargin, asset.minX, asset.maxY, asset.maxY + kMargin, 42, 42);
    addTerrainPatch(mesh, asset.maxX, asset.maxX + kMargin, asset.maxY, asset.maxY + kMargin, 42, 42);
    addTerrainPatch(mesh, asset.minX - kMargin, asset.minX, asset.minY - kMargin, asset.minY, 42, 42);
    addTerrainPatch(mesh, asset.maxX, asset.maxX + kMargin, asset.minY - kMargin, asset.minY, 42, 42);
  }

  float distanceToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float d = glm::dot(ab, ab);
    if (d < 0.001f)
      return glm::length(p - a);
    const float t = saturate(glm::dot(p - a, ab) / d);
    return glm::length(p - (a + ab * t));
  }

  float distanceToPath(const glm::vec2& p, const std::vector<glm::vec2>& path) {
    float best = 100000.0f;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
      best = std::min(best, distanceToSegment(p, path[i], path[i + 1]));
    }
    return best;
  }

  float distanceToPaths(const glm::vec2& p, const std::vector<std::vector<glm::vec2>>& paths) {
    float best = 100000.0f;
    for (const auto& path : paths) {
      best = std::min(best, distanceToPath(p, path));
    }
    return best;
  }

  void addTowerBattlements(
      Mesh& mesh, const glm::vec2& p, float z, float scale, float halfX, float halfY, const glm::vec3& stone) {
    const float step = 5.0f * scale;
    const float merlonHalf = 1.05f * scale;
    for (float x = -halfX + step * 0.45f; x < halfX; x += step) {
      addBox(mesh, {p.x + x, p.y - halfY, z}, {merlonHalf, 1.1f * scale, 1.9f * scale}, stone * 1.12f);
      addBox(mesh, {p.x + x, p.y + halfY, z}, {merlonHalf, 1.1f * scale, 1.9f * scale}, stone * 1.05f);
    }
    for (float y = -halfY + step * 0.45f; y < halfY; y += step) {
      addBox(mesh, {p.x - halfX, p.y + y, z}, {1.1f * scale, merlonHalf, 1.9f * scale}, stone * 0.98f);
      addBox(mesh, {p.x + halfX, p.y + y, z}, {1.1f * scale, merlonHalf, 1.9f * scale}, stone * 1.08f);
    }
  }

  void addOrientedTowerBattlements(Mesh& mesh,
                                   const glm::vec2& p,
                                   const glm::vec2& forward,
                                   float z,
                                   float scale,
                                   float halfLength,
                                   float halfWidth,
                                   const glm::vec3& stone) {
    glm::vec2 dir = forward;
    if (glm::length(dir) < 0.001f)
      dir = {1.0f, 0.0f};
    dir = glm::normalize(dir);
    const glm::vec2 n = {-dir.y, dir.x};
    const float step = 4.6f * scale;
    const float merlonHalf = 0.72f * scale;
    for (float x = -halfLength + step * 0.45f; x < halfLength; x += step) {
      addOrientedBox(
          mesh, p + dir * x + n * halfWidth, dir, merlonHalf, 0.58f * scale, z, 1.05f * scale, stone * 1.08f);
      addOrientedBox(mesh, p + dir * x - n * halfWidth, dir, merlonHalf, 0.58f * scale, z, 1.05f * scale, stone);
    }
    for (float y = -halfWidth + step * 0.45f; y < halfWidth; y += step) {
      addOrientedBox(mesh, p + n * y + dir * halfLength, n, merlonHalf, 0.58f * scale, z, 1.05f * scale, stone * 1.04f);
      addOrientedBox(mesh, p + n * y - dir * halfLength, n, merlonHalf, 0.58f * scale, z, 1.05f * scale, stone * 0.98f);
    }
  }

  void addWatchTower(Mesh& mesh, const glm::vec2& p, float scale, const glm::vec3& stone) {
    ++activeSceneStats().watchTowers;
    const float z = terrainHeight(p.x, p.y);
    const glm::vec3 dark = {0.12f, 0.10f, 0.08f};
    addBox(mesh, {p.x, p.y, z + 2.2f * scale}, {13.5f * scale, 11.5f * scale, 2.2f * scale}, stone * 0.80f);
    addBox(mesh, {p.x, p.y, z + 9.8f * scale}, {10.6f * scale, 9.0f * scale, 7.8f * scale}, stone * 0.92f);
    addBox(mesh, {p.x, p.y, z + 18.0f * scale}, {12.8f * scale, 10.8f * scale, 2.4f * scale}, stone * 1.04f);
    addBox(mesh, {p.x, p.y, z + 21.8f * scale}, {10.2f * scale, 8.2f * scale, 1.3f * scale}, {0.24f, 0.20f, 0.16f});

    addTowerBattlements(mesh, p, z + 24.0f * scale, scale, 10.6f * scale, 8.8f * scale, stone);

    for (int sx : {-1, 1}) {
      addBox(mesh,
             {p.x + sx * 10.72f * scale, p.y - 3.0f * scale, z + 11.4f * scale},
             {0.16f * scale, 1.5f * scale, 1.9f * scale},
             dark);
      addBox(mesh,
             {p.x + sx * 10.72f * scale, p.y + 3.0f * scale, z + 11.4f * scale},
             {0.16f * scale, 1.5f * scale, 1.9f * scale},
             dark);
    }
    for (int sy : {-1, 1}) {
      addBox(mesh,
             {p.x - 3.2f * scale, p.y + sy * 9.12f * scale, z + 11.4f * scale},
             {1.5f * scale, 0.16f * scale, 1.9f * scale},
             dark);
      addBox(mesh,
             {p.x + 3.2f * scale, p.y + sy * 9.12f * scale, z + 11.4f * scale},
             {1.5f * scale, 0.16f * scale, 1.9f * scale},
             dark);
    }

    for (int sx : {-1, 1}) {
      for (int sy : {-1, 1}) {
        addBox(mesh,
               {p.x + sx * 8.8f * scale, p.y + sy * 7.1f * scale, z + 19.8f * scale},
               {1.5f * scale, 1.5f * scale, 2.2f * scale},
               stone * 0.96f);
      }
    }
  }

  void addPhotoTower(Mesh& mesh, const glm::vec2& p, const glm::vec2& forward, float scale, const glm::vec3& stone) {
    ++activeSceneStats().watchTowers;
    const float z = terrainHeight(p.x, p.y);
    const glm::vec3 dark = {0.07f, 0.06f, 0.05f};
    glm::vec2 dir = forward;
    if (glm::length(dir) < 0.001f)
      dir = {1.0f, 0.0f};
    dir = glm::normalize(dir);
    const glm::vec2 n = {-dir.y, dir.x};

    addOrientedBox(mesh, p, dir, 8.4f * scale, 6.3f * scale, z + 1.1f * scale, 1.1f * scale, stone * 0.78f);
    addOrientedBox(mesh, p, dir, 6.7f * scale, 5.1f * scale, z + 5.2f * scale, 4.0f * scale, stone * 0.98f);
    addOrientedBox(mesh, p, dir, 7.5f * scale, 5.8f * scale, z + 9.7f * scale, 0.8f * scale, stone * 0.98f);
    addOrientedTowerBattlements(
        mesh, p, dir, z + 11.0f * scale, scale * 0.45f, 6.8f * scale, 5.1f * scale, stone * 0.92f);

    for (float side : {-1.0f, 1.0f}) {
      for (float offset : {-2.0f, 2.0f}) {
        addOrientedBox(mesh,
                       p + n * side * 5.18f * scale + dir * offset * scale,
                       dir,
                       0.68f * scale,
                       0.08f * scale,
                       z + 5.8f * scale,
                       1.05f * scale,
                       dark);
      }
    }
    for (float end : {-1.0f, 1.0f}) {
      for (float offset : {-1.8f, 1.8f}) {
        addOrientedBox(mesh,
                       p + dir * end * 6.78f * scale + n * offset * scale,
                       n,
                       0.58f * scale,
                       0.08f * scale,
                       z + 5.8f * scale,
                       1.05f * scale,
                       dark);
      }
    }
  }

  void addBroadleafTree(Mesh& mesh, float x, float y, float z, float scale, const glm::vec3& leaf) {
    ++activeSceneStats().broadleafTrees;
    const glm::vec3 trunk = {0.20f, 0.12f, 0.06f};
    addCylinder(mesh, {x, y, z}, 0.34f * scale, 3.2f * scale, 6, trunk);
    addFrustum(mesh, {x, y, z + 2.4f * scale}, 2.8f * scale, 1.7f * scale, 2.6f * scale, 8, leaf * 0.88f);
    addFrustum(mesh,
               {x + 0.35f * scale, y - 0.18f * scale, z + 4.0f * scale},
               2.4f * scale,
               0.7f * scale,
               2.2f * scale,
               8,
               leaf * 1.03f);
  }

  void addPineTree(Mesh& mesh, float x, float y, float z, float scale, const glm::vec3& leaf) {
    ++activeSceneStats().pineTrees;
    const glm::vec3 trunk = {0.18f, 0.10f, 0.05f};
    addCylinder(mesh, {x, y, z}, 0.28f * scale, 4.8f * scale, 6, trunk);
    addFrustum(mesh, {x, y, z + 1.5f * scale}, 2.7f * scale, 0.15f * scale, 3.2f * scale, 8, leaf * 0.80f);
    addFrustum(mesh, {x, y, z + 3.4f * scale}, 2.2f * scale, 0.12f * scale, 3.0f * scale, 8, leaf * 0.94f);
    addFrustum(mesh, {x, y, z + 5.2f * scale}, 1.6f * scale, 0.10f * scale, 2.5f * scale, 8, leaf * 1.08f);
  }

  void addShrub(Mesh& mesh, float x, float y, float z, float scale, const glm::vec3& leaf) {
    ++activeSceneStats().shrubs;
    addFrustum(mesh, {x, y, z}, 1.6f * scale, 0.55f * scale, 1.4f * scale, 7, leaf * 0.86f);
    addFrustum(mesh,
               {x + 0.8f * scale, y - 0.25f * scale, z + 0.2f * scale},
               1.2f * scale,
               0.40f * scale,
               1.1f * scale,
               7,
               leaf * 1.05f);
  }

  void addVegetation(Mesh& mesh, const std::vector<std::vector<glm::vec2>>& paths) {
    const glm::vec3 broadleaf = {0.10f, 0.29f, 0.11f};
    const glm::vec3 pine = {0.06f, 0.22f, 0.09f};
    const glm::vec3 shrub = {0.16f, 0.34f, 0.13f};
    const bool realAsset = loadBadalingAsset();
    if (realAsset) {
      const auto& asset = badalingAsset();
      const int treeCandidates = 760;
      for (int i = 0; i < treeCandidates; ++i) {
        const float rx = positiveHash21(static_cast<float>(i) * 2.43f, 41.0f);
        const float ry = positiveHash21(static_cast<float>(i) * 1.79f, 83.0f);
        const float x = glm::mix(asset.minX, asset.maxX, rx);
        const float y = glm::mix(asset.minY, asset.maxY, ry);
        if (distanceToPaths({x, y}, paths) < 23.0f)
          continue;

        const glm::vec3 color = sampleAssetColor(x, y);
        const float greenSignal = color.g - std::max(color.r, color.b);
        const float density = glm::clamp(0.10f + greenSignal * 4.2f + color.g * 0.58f, 0.0f, 0.72f);
        if (positiveHash21(static_cast<float>(i), 271.0f) > density)
          continue;

        const float z = terrainHeight(x, y);
        const float hx = terrainHeight(x + 5.0f, y) - terrainHeight(x - 5.0f, y);
        const float hy = terrainHeight(x, y + 5.0f) - terrainHeight(x, y - 5.0f);
        if (glm::length(glm::vec2{hx, hy}) > 10.5f)
          continue;

        const float scale = 0.34f + 0.34f * positiveHash21(static_cast<float>(i), 319.0f);
        const glm::vec3 leaf = glm::mix(
            glm::vec3{0.10f, 0.27f, 0.09f}, glm::vec3{0.25f, 0.47f, 0.17f}, positiveHash21(x + 5.0f, y - 7.0f));
        addBroadleafTree(mesh, x, y, z, scale, leaf);
      }

      const int shrubCandidates = 1250;
      for (int i = 0; i < shrubCandidates; ++i) {
        const float rx = positiveHash21(static_cast<float>(i) * 1.61f, 301.0f);
        const float ry = positiveHash21(static_cast<float>(i) * 2.17f, 517.0f);
        const float x = glm::mix(asset.minX, asset.maxX, rx);
        const float y = glm::mix(asset.minY, asset.maxY, ry);
        if (distanceToPaths({x, y}, paths) < 14.0f)
          continue;

        const glm::vec3 color = sampleAssetColor(x, y);
        const float greenSignal = color.g - std::max(color.r, color.b);
        if (greenSignal < -0.01f && color.g < 0.18f)
          continue;

        const float z = terrainHeight(x, y);
        const float hx = terrainHeight(x + 5.0f, y) - terrainHeight(x - 5.0f, y);
        const float hy = terrainHeight(x, y + 5.0f) - terrainHeight(x, y - 5.0f);
        if (glm::length(glm::vec2{hx, hy}) > 13.0f)
          continue;

        const float density = glm::clamp(0.26f + greenSignal * 3.2f + color.g * 0.55f, 0.0f, 0.78f);
        if (positiveHash21(static_cast<float>(i), 733.0f) > density)
          continue;

        const float scale = 0.26f + 0.34f * positiveHash21(static_cast<float>(i), 907.0f);
        const glm::vec3 leaf =
            glm::mix(glm::vec3{0.12f, 0.28f, 0.10f}, glm::vec3{0.24f, 0.45f, 0.16f}, positiveHash21(x, y));
        addShrub(mesh, x, y, z, scale, leaf);
      }
      return;
    }
    const auto& asset = badalingAsset();
    const float minX = realAsset ? asset.minX : -310.0f;
    const float maxX = realAsset ? asset.maxX : 310.0f;
    const float minY = realAsset ? asset.minY : -304.0f;
    const float maxY = realAsset ? asset.maxY : 304.0f;

    const int treeCandidates = 560;
    for (int i = 0; i < treeCandidates; ++i) {
      const float rx = positiveHash21(static_cast<float>(i) * 1.37f, 15.3f);
      const float ry = positiveHash21(static_cast<float>(i) * 2.11f, 91.7f);
      const float x = glm::mix(minX, maxX, rx);
      const float y = glm::mix(minY, maxY, ry);
      const glm::vec2 p = {x, y};
      const float distWall = distanceToPaths(p, paths);
      if (distWall < 16.0f)
        continue;
      if (!realAsset && std::abs(y - ridgeCenterY(x)) < 10.0f && distWall < 32.0f)
        continue;

      const float z = terrainHeight(x, y);
      const float hx = terrainHeight(x + 2.0f, y) - terrainHeight(x - 2.0f, y);
      const float hy = terrainHeight(x, y + 2.0f) - terrainHeight(x, y - 2.0f);
      const float slope = glm::length(glm::vec2{hx, hy}) * 0.5f;
      if (slope > 9.0f)
        continue;

      float density = 0.52f + 0.32f * valueNoise(x * 0.024f + 8.0f, y * 0.024f - 3.0f);
      if (realAsset) {
        const glm::vec3 color = sampleAssetColor(x, y);
        const float greenSignal = color.g - std::max(color.r, color.b);
        density = glm::clamp(0.18f + greenSignal * 3.6f + color.g * 0.45f, 0.0f, 0.86f);
        if (greenSignal < 0.015f && color.g < 0.16f)
          continue;
      }
      if (positiveHash21(static_cast<float>(i), 44.0f) > density)
        continue;

      const float scale = (0.62f + 0.95f * positiveHash21(static_cast<float>(i), 7.0f)) * (realAsset ? 0.42f : 1.0f);
      const float kind = positiveHash21(static_cast<float>(i), 23.0f);
      if (kind < (realAsset ? 0.08f : 0.23f)) {
        addPineTree(mesh, x, y, z, scale, pine * (0.82f + 0.30f * positiveHash21(x, y)));
      } else if (kind < 0.78f) {
        addBroadleafTree(mesh, x, y, z, scale, broadleaf * (0.84f + 0.32f * positiveHash21(x + 9.0f, y - 4.0f)));
      } else {
        addShrub(mesh, x, y, z, scale, shrub * (0.80f + 0.26f * positiveHash21(x - 12.0f, y + 2.0f)));
      }
    }

    const int shrubCandidates = 180;
    for (int i = 0; i < shrubCandidates; ++i) {
      const float rx = positiveHash21(static_cast<float>(i), 113.0f);
      const float ry = positiveHash21(static_cast<float>(i), 157.0f);
      const float x = glm::mix(minX, maxX, rx);
      const float y = glm::mix(minY, maxY, ry);
      if (distanceToPaths({x, y}, paths) < 11.0f)
        continue;
      if (realAsset) {
        const glm::vec3 color = sampleAssetColor(x, y);
        if (color.g < 0.12f || color.g - std::max(color.r, color.b) < 0.0f)
          continue;
      }
      const float z = terrainHeight(x, y);
      const float scale = (0.55f + 0.65f * positiveHash21(static_cast<float>(i), 205.0f)) * (realAsset ? 0.45f : 1.0f);
      addShrub(mesh, x, y, z, scale, shrub * (0.75f + 0.35f * positiveHash21(x, y)));
    }
  }

  Mesh buildBadalingScene() {
    activeSceneStats() = {};
    Mesh mesh;
    mesh.vertices.reserve(340000);
    mesh.indices.reserve(900000);

    const bool realAsset = loadBadalingAsset();
    activeSceneStats().realAsset = realAsset;
    addTerrain(mesh);

    const auto paths = makeWallPaths();
    activeSceneStats().wallPaths = static_cast<uint32_t>(paths.size());
    addViewSkyElements(mesh, paths);
    const glm::vec3 stone = realAsset ? glm::vec3{0.94f, 0.84f, 0.66f} : glm::vec3{0.58f, 0.53f, 0.45f};
    const glm::vec3 parapet = realAsset ? glm::vec3{0.99f, 0.91f, 0.73f} : glm::vec3{0.66f, 0.61f, 0.53f};
    const glm::vec3 mortar = realAsset ? glm::vec3{0.70f, 0.60f, 0.47f} : glm::vec3{0.33f, 0.30f, 0.26f};

    for (size_t pathIndex = 0; pathIndex < paths.size(); ++pathIndex) {
      const auto& path = paths[pathIndex];
      if (path.size() < 2)
        continue;
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        const glm::vec2 a = path[i];
        const glm::vec2 b = path[i + 1];
        glm::vec2 dir = glm::normalize(b - a);
        glm::vec2 n = {-dir.y, dir.x};

        if (realAsset) {
          addSegmentPrism(mesh, a, b, 9.8f, 1.05f, 3.05f, stone);
          addSegmentPrism(mesh, a, b, 6.4f, 3.98f, 0.16f, mortar * 1.10f);
          addSegmentPrism(mesh, a + n * 4.05f, b + n * 4.05f, 0.92f, 4.08f, 1.10f, parapet);
          addSegmentPrism(mesh, a - n * 4.05f, b - n * 4.05f, 0.92f, 4.08f, 1.10f, parapet * 0.96f);
          if (i % 9 == 0) {
            const glm::vec2 p = glm::mix(a, b, 0.5f);
            const float z = terrainHeight(p.x, p.y) + 4.16f;
            addOrientedBox(mesh, p, dir, 0.14f, 2.95f, z, 0.040f, mortar * 0.70f);
          }
        } else {
          addSegmentPrism(mesh, a, b, 10.4f, 1.1f, 4.8f, stone);
          addSegmentPrism(mesh, a, b, 6.4f, 5.82f, 0.18f, mortar * 1.16f);
          addSegmentPrism(mesh, a + n * 4.25f, b + n * 4.25f, 1.45f, 5.7f, 3.0f, parapet);
          addSegmentPrism(mesh, a - n * 4.25f, b - n * 4.25f, 1.45f, 5.7f, 3.0f, parapet * 0.95f);
          addSegmentPrism(mesh, a + n * 5.08f, b + n * 5.08f, 0.14f, 6.75f, 0.14f, mortar);
          addSegmentPrism(mesh, a - n * 5.08f, b - n * 5.08f, 0.14f, 6.75f, 0.14f, mortar * 0.92f);

          const float len = glm::length(b - a);
          const int count = std::max(1, static_cast<int>(len / 7.8f));
          for (int j = 0; j < count; ++j) {
            const float t = (static_cast<float>(j) + 0.5f) / static_cast<float>(count);
            const glm::vec2 p = glm::mix(a, b, t);
            for (float side : {-1.0f, 1.0f}) {
              const glm::vec2 q = p + n * side * 4.88f;
              const float z = terrainHeight(q.x, q.y) + 9.95f;
              addOrientedBox(mesh, q, dir, 0.82f, 0.62f, z, 1.10f, parapet * (side > 0.0f ? 1.08f : 1.02f));
            }
          }

          if (i % 4 == 0) {
            const glm::vec2 p = glm::mix(a, b, 0.52f);
            const float z = terrainHeight(p.x, p.y) + 6.12f;
            addOrientedBox(mesh, p, dir, 0.18f, 2.95f, z, 0.055f, mortar * 0.72f);
          }
        }
      }

      if (!realAsset && pathIndex < 4 && path.size() > 18) {
        const size_t towerStep = std::max<size_t>(32, path.size() / 3);
        for (size_t i = std::min<size_t>(12, path.size() - 1); i < path.size(); i += towerStep) {
          addWatchTower(mesh, path[i], (i % (towerStep * 2) == 12) ? 1.02f : 0.82f, stone);
        }
      } else if (realAsset && pathIndex < 2 && path.size() > 28) {
        const std::array<size_t, 3> towerIndices = {
            std::min<size_t>(10, path.size() - 1),
            path.size() / 2,
            path.size() - 12,
        };
        for (size_t idx : towerIndices) {
          if (idx < path.size()) {
            const size_t prev = idx > 0 ? idx - 1 : idx;
            const size_t next = std::min(idx + 1, path.size() - 1);
            glm::vec2 towerDir = path[next] - path[prev];
            if (glm::length(towerDir) < 0.001f && next + 1 < path.size()) {
              towerDir = path[next + 1] - path[idx];
            }
            addPhotoTower(mesh, path[idx], towerDir, pathIndex == 0 ? 1.15f : 0.92f, stone);
          }
        }
      }
    }

    addVegetation(mesh, paths);
    activeSceneStats().vertices = static_cast<uint32_t>(mesh.vertices.size());
    activeSceneStats().indices = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
  }

  template <typename T>
  RHIBufferPtr makeHostBuffer(RHIDevice* device, const std::vector<T>& data, BufferUsageFlagBits usage) {
    if (data.empty())
      return nullptr;
    BufferDesc desc;
    desc.size = data.size() * sizeof(T);
    desc.usage = static_cast<BufferUsageFlags>(usage);
    desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    auto buffer = device->createBuffer(desc);
    if (!buffer)
      return nullptr;
    void* mapped = buffer->map();
    std::memcpy(mapped, data.data(), static_cast<size_t>(desc.size));
    buffer->unmap();
    return buffer;
  }

  bool createBadalingShaders(RHIDevice* device, RHIShaderPtr& vertexShader, RHIShaderPtr& fragmentShader) {
    ShaderCompiler compiler;
    const char* vs = R"glsl(
        layout(location = 0) in vec3 inPos;
        layout(location = 1) in vec3 inColor;
        layout(location = 2) in vec3 inNormal;
        layout(location = 3) in vec2 inUv;
        layout(location = 4) in float inTextureBlend;
        layout(location = 0) out vec3 outColor;
        layout(location = 1) out float outFog;
        layout(location = 2) out vec2 outUv;
        layout(location = 3) out float outTextureBlend;
        layout(push_constant) uniform PC {
            mat4 mvp;
            vec4 camera;
            vec4 light;
            vec4 fog;
            vec4 weather;
        } pc;
        void main() {
            gl_Position = pc.mvp * vec4(inPos, 1.0);
            vec3 n = normalize(inNormal);
            vec3 lightDir = normalize(pc.light.xyz);
            float frontLight = max(dot(n, lightDir), 0.0);
            float backFill = max(dot(-n, lightDir), 0.0) * 0.22;
            float hemi = clamp(n.z * 0.5 + 0.5, 0.0, 1.0);
            float shade = 0.36 + frontLight * (0.52 * pc.light.w) + backFill + hemi * 0.18;
            float dist = distance(pc.camera.xyz, inPos);
            float fogWidth = max(pc.weather.y - pc.weather.x, 1.0);
            outFog = clamp((dist - pc.weather.x) / fogWidth, 0.0, pc.fog.w);
            outColor = inColor * shade;
            outUv = inUv;
            outTextureBlend = inTextureBlend;
        }
    )glsl";
    const char* fs = R"glsl(
        layout(location = 0) in vec3 inColor;
        layout(location = 1) in float inFog;
        layout(location = 2) in vec2 inUv;
        layout(location = 3) in float inTextureBlend;
        layout(set = 0, binding = 0) uniform sampler2D terrainTexture;
        layout(push_constant) uniform PC {
            mat4 mvp;
            vec4 camera;
            vec4 light;
            vec4 fog;
            vec4 weather;
        } pc;
        layout(location = 0) out vec4 outColor;
        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }
        void main() {
            vec3 tex = texture(terrainTexture, inUv).rgb;
            vec3 base = mix(inColor, tex, clamp(inTextureBlend, 0.0, 1.0));
            vec3 color = mix(base, pc.fog.rgb, inFog);

            float rain = clamp(pc.weather.z, 0.0, 1.0);
            float snow = clamp(pc.weather.w, 0.0, 1.0);
            float time = pc.camera.w;
            vec2 px = gl_FragCoord.xy;

            float rainColumn = step(0.84 - rain * 0.22,
                                    hash(vec2(floor(px.x * 0.075), floor((px.y + time * 220.0) * 0.028))));
            float rainStreak = smoothstep(0.24, 0.0,
                                          abs(fract((px.y + px.x * 0.82 + time * 360.0) * 0.045) - 0.5));
            color = mix(color, vec3(0.66, 0.78, 0.88), rainColumn * rainStreak * rain * 0.58);

            vec2 snowCell = floor((px + vec2(time * 16.0, time * 44.0)) / 20.0);
            vec2 snowLocal = fract((px + vec2(time * 16.0, time * 44.0)) / 20.0) - 0.5;
            snowLocal += vec2(hash(snowCell) - 0.5, hash(snowCell + 13.7) - 0.5) * 0.32;
            float flake = smoothstep(0.15, 0.0, length(snowLocal));
            color = mix(color, vec3(0.93, 0.96, 1.0), flake * snow * 0.76);

            outColor = vec4(color, 1.0);
        }
    )glsl";

    auto vr = compiler.compileGLSLToSPIRV(vs, "main", "vertex");
    auto fr = compiler.compileGLSLToSPIRV(fs, "main", "fragment");
    if (!vr.success || !fr.success) {
      std::cerr << "Shader compile failed: " << vr.errorMessage << " " << fr.errorMessage << std::endl;
      return false;
    }

    ShaderDesc vertexDesc;
    vertexDesc.stage = ShaderStageFlagBits::Vertex;
    vertexDesc.spirvCode = vr.spirvCode;
    vertexShader = device->createShader(vertexDesc);

    ShaderDesc fragmentDesc;
    fragmentDesc.stage = ShaderStageFlagBits::Fragment;
    fragmentDesc.spirvCode = fr.spirvCode;
    fragmentShader = device->createShader(fragmentDesc);
    return vertexShader && fragmentShader;
  }

  RHIPipelinePtr createBadalingPipeline(RHIDevice* device,
                                        const RHIRenderPassPtr& renderPass,
                                        const RHIPipelineLayoutPtr& pipelineLayout,
                                        const RHIShaderPtr& vertexShader,
                                        const RHIShaderPtr& fragmentShader,
                                        FillMode fillMode) {
    GraphicsPipelineDesc pd;
    pd.shaders = {vertexShader, fragmentShader};
    pd.vertexInput.bindings = {Vertex::binding()};
    pd.vertexInput.attributes = Vertex::attributes();
    pd.primitiveTopology = PrimitiveTopology::TriangleList;
    pd.rasterization.cullMode = CullMode::None;
    pd.rasterization.fillMode = fillMode;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthWriteEnable = true;
    pd.depthStencil.depthCompareOp = CompareOp::Less;
    pd.renderPass = renderPass;
    pd.pipelineLayout = pipelineLayout;
    ColorBlendAttachmentState blend;
    blend.blendEnable = false;
    pd.colorBlendAttachments.push_back(blend);
    return device->createGraphicsPipeline(pd);
  }

  bool createTerrainTextureBinding(RHIDevice* device, TerrainTextureBinding& out) {
    DescriptorSetLayoutDesc layoutDesc;
    DescriptorSetLayoutBinding binding;
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    binding.descriptorType = DescriptorType::CombinedImageSampler;
    layoutDesc.bindings.push_back(binding);
    out.layout = device->createDescriptorSetLayout(layoutDesc);
    if (!out.layout)
      return false;

    uint32_t width = 1;
    uint32_t height = 1;
    std::vector<uint8_t> rgba = {255, 255, 255, 255};
    if (loadBadalingAsset() && !badalingAsset().colors.empty()) {
      const auto& asset = badalingAsset();
      width = asset.width;
      height = asset.height;
      rgba.resize(static_cast<size_t>(width) * height * 4);
      for (size_t i = 0, j = 0; i < asset.colors.size(); i += 3, j += 4) {
        rgba[j + 0] = asset.colors[i + 0];
        rgba[j + 1] = asset.colors[i + 1];
        rgba[j + 2] = asset.colors[i + 2];
        rgba[j + 3] = 255;
      }
    }

    TextureDesc textureDesc;
    textureDesc.type = TextureType::Texture2D;
    textureDesc.extent = {width, height, 1};
    textureDesc.mipLevels = 1;
    textureDesc.arrayLayers = 1;
    textureDesc.format = Format::R8G8B8A8_UNORM;
    textureDesc.usage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst;
    textureDesc.sampleCount = 1;
    out.texture = device->createTexture(textureDesc);
    if (!out.texture)
      return false;
    out.view = out.texture->createView();
    if (!out.view)
      return false;

    RHIDevice::MipData mip;
    mip.width = width;
    mip.height = height;
    mip.depth = 1;
    mip.data = rgba.data();
    mip.dataSize = rgba.size();
    device->uploadTextureData(out.texture, {mip}, 0);

    SamplerDesc samplerDesc;
    samplerDesc.magFilter = SamplerFilter::Linear;
    samplerDesc.minFilter = SamplerFilter::Linear;
    samplerDesc.mipmapMode = SamplerMipmapMode::Linear;
    samplerDesc.addressModeU = SamplerAddressMode::ClampToEdge;
    samplerDesc.addressModeV = SamplerAddressMode::ClampToEdge;
    samplerDesc.addressModeW = SamplerAddressMode::ClampToEdge;
    out.sampler = device->createSampler(samplerDesc);
    if (!out.sampler)
      return false;

    DescriptorSetDesc setDesc;
    DescriptorBinding descriptor;
    descriptor.binding = 0;
    descriptor.type = DescriptorType::CombinedImageSampler;
    descriptor.textureView = out.view;
    descriptor.sampler = out.sampler;
    setDesc.bindings.push_back(descriptor);
    out.descriptorSet = device->createDescriptorSet(out.layout, setDesc);
    return out.descriptorSet != nullptr;
  }

  bool renderOffscreenBadaling(const std::string& screenshotPath, const std::string& statsPath, int width, int height) {
    auto factory = Backend::createFactory(Backend::Type::Vulkan);
    if (!factory) {
      std::cerr << "createFactory failed" << std::endl;
      return false;
    }

    RHIDevice* device = factory->createDevice(false);
    if (!device) {
      std::cerr << "createDevice failed" << std::endl;
      Backend::destroyFactory(factory);
      return false;
    }
    struct DeviceFactoryGuard {
      RHIFactory* factory = nullptr;
      RHIDevice* device = nullptr;

      ~DeviceFactoryGuard() {
        if (factory && device)
          factory->destroyDevice(device);
        if (factory)
          Backend::destroyFactory(factory);
      }
    } deviceFactoryGuard{factory, device};
    std::cout << "GPU: " << device->getProperties().deviceName << std::endl;

    auto target = device->createOffscreenTarget(
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), Format::R8G8B8A8_UNORM);
    if (!target) {
      std::cerr << "createOffscreenTarget failed" << std::endl;
      return false;
    }

    RHIShaderPtr vertexShader;
    RHIShaderPtr fragmentShader;
    if (!createBadalingShaders(device, vertexShader, fragmentShader)) {
      return false;
    }

    TerrainTextureBinding terrainTexture;
    if (!createTerrainTextureBinding(device, terrainTexture)) {
      std::cerr << "terrain texture creation failed" << std::endl;
      return false;
    }

    PipelineLayoutDesc layoutDesc;
    layoutDesc.setLayouts.push_back(terrainTexture.layout);
    PushConstantRange pushRange;
    pushRange.stageFlags = badalingPushStages();
    pushRange.offset = 0;
    pushRange.size = sizeof(PushData);
    layoutDesc.pushConstantRanges.push_back(pushRange);
    auto pipelineLayout = device->createPipelineLayout(layoutDesc);
    auto pipeline = createBadalingPipeline(
        device, target->getRenderPass(), pipelineLayout, vertexShader, fragmentShader, FillMode::Solid);
    if (!pipelineLayout || !pipeline) {
      std::cerr << "pipeline creation failed" << std::endl;
      return false;
    }

    Mesh scene = buildBadalingScene();
    if (!writeBadalingSceneStats(statsPath)) {
      return false;
    }
    auto vertexBuffer = makeHostBuffer(device, scene.vertices, BufferUsageFlagBits::Vertex);
    auto indexBuffer = makeHostBuffer(device, scene.indices, BufferUsageFlagBits::Index);
    if (!vertexBuffer || !indexBuffer) {
      std::cerr << "scene buffer creation failed" << std::endl;
      return false;
    }

    glm::vec3 eye = {-235.0f, -126.0f, 128.0f};
    glm::vec3 focus = {-30.0f, 2.0f, terrainHeight(-30.0f, 2.0f) + 22.0f};
    const auto wallPath = makeWallPath();
    if (loadBadalingAsset() && wallPath.size() > 12) {
      const size_t i = wallPath.size() / 2;
      const glm::vec2 p = wallPath[i];
      const glm::vec2 q = wallPath[std::min(i + 10, wallPath.size() - 1)];
      glm::vec2 dir2 = q - p;
      if (glm::length(dir2) < 0.001f)
        dir2 = {1.0f, 0.0f};
      dir2 = glm::normalize(dir2);
      const glm::vec2 side = {-dir2.y, dir2.x};
      const float ground = terrainHeight(p.x, p.y);
      focus = {p.x + dir2.x * 24.0f, p.y + dir2.y * 24.0f, ground + 18.0f};
      eye = {p.x - dir2.x * 150.0f + side.x * 108.0f, p.y - dir2.y * 150.0f + side.y * 108.0f, ground + 96.0f};
    }
    glm::mat4 view = glm::lookAt(eye, focus, {0.0f, 0.0f, 1.0f});
    glm::mat4 proj =
        glm::perspective(glm::radians(50.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 1800.0f);
    proj[1][1] *= -1.0f;
    PushData push = makePushData(proj * view, eye, 0.0f);

    auto commandBuffer = device->createCommandBuffer();
    auto fence = device->createFence(false);
    commandBuffer->begin();

    Rect2D area;
    area.offset = {0, 0};
    area.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    const auto& weather = activeWeather();
    std::vector<std::array<float, 4>> clearColors = {
        {weather.skyColor.r, weather.skyColor.g, weather.skyColor.b, 1.0f}};
    commandBuffer->beginRenderPass(target->getRenderPass(), target->getFramebuffer(), area, clearColors, 1.0f, 0);

    Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer->setViewport(viewport);
    commandBuffer->setScissor(area);
    commandBuffer->bindPipeline(pipeline);
    commandBuffer->bindDescriptorSet(0, terrainTexture.descriptorSet, pipelineLayout);
    commandBuffer->pushConstants(pipelineLayout, badalingPushStages(), 0, sizeof(push), &push);
    commandBuffer->bindVertexBuffer(0, vertexBuffer);
    commandBuffer->bindIndexBuffer(indexBuffer);
    commandBuffer->drawIndexed(static_cast<uint32_t>(scene.indices.size()));
    commandBuffer->endRenderPass();
    commandBuffer->end();

    device->submit({commandBuffer}, {}, {}, fence);
    fence->wait();
    device->waitIdle();

    std::vector<uint8_t> rgba;
    uint32_t outW = 0;
    uint32_t outH = 0;
    if (!target->readbackToBuffer(rgba, outW, outH)) {
      std::cerr << "readbackToBuffer failed" << std::endl;
      return false;
    }

    if (stbi_write_png(screenshotPath.c_str(),
                       static_cast<int>(outW),
                       static_cast<int>(outH),
                       4,
                       rgba.data(),
                       static_cast<int>(outW) * 4) == 0) {
      std::cerr << "stbi_write_png failed" << std::endl;
      return false;
    }

    std::cout << "[badaling_demo] wrote " << screenshotPath << " (" << outW << "x" << outH << ")" << std::endl;
    commandBuffer.reset();
    fence.reset();
    pipeline.reset();
    pipelineLayout.reset();
    vertexBuffer.reset();
    indexBuffer.reset();
    vertexShader.reset();
    fragmentShader.reset();
    target.reset();
    return true;
  }

  class BadalingDemoApp {
  public:
    bool initialize();
    void run(int maxFrames);
    void shutdown();
    void requestStartupFullscreen();
    void toggleFullscreen();

  private:
#ifdef _WIN32
    HINSTANCE hinstance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool running_ = true;
    std::array<bool, 256> keys_{};
    bool fullscreen_ = false;
    WINDOWPLACEMENT windowPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD windowedStyle_ = 0;
#else
    SDL_Window* window_ = nullptr;
#endif
    int windowWidth_ = 1280;
    int windowHeight_ = 720;
    bool startupFullscreen_ = false;

    RHIDevice* device_ = nullptr;
    RHIShaderPtr vertexShader_;
    RHIShaderPtr fragmentShader_;
    RHIBufferPtr vertexBuffer_;
    RHIBufferPtr indexBuffer_;
    RHITexturePtr depthTexture_;
    RHITextureViewPtr depthView_;
    TerrainTextureBinding terrainTexture_;
    RHIPipelinePtr solidPipeline_;
    RHIPipelinePtr wirePipeline_;
    RHIPipelineLayoutPtr pipelineLayout_;
    RHIRenderPassPtr renderPass_;
    std::vector<RHIFramebufferPtr> framebuffers_;
    RHICommandBufferPtr commandBuffer_;
    RHISemaphorePtr imageAvailable_;
    RHISemaphorePtr renderFinished_;
    RHIFencePtr inFlight_;

    uint32_t indexCount_ = 0;
    glm::vec3 camPos_ = {-205.0f, -72.0f, 92.0f};
    float camYaw_ = 1.05f;
    float camPitch_ = -0.22f;
    float moveSpeed_ = 58.0f;
    bool dragging_ = false;
    bool panning_ = false;
    bool wireframe_ = false;
    bool autoTour_ = true;
    glm::vec2 lastMouse_ = {0.0f, 0.0f};
    std::vector<glm::vec2> tourPath_;
    float tourT_ = 0.0f;
    uint64_t frameCount_ = 0;
    float elapsedSeconds_ = 0.0f;
    double fpsTimer_ = 0.0;

    bool initWindow();
    bool initVulkan();
    bool createDepth();
    bool createShaders();
    bool createRenderPass();
    bool createFramebuffers();
    bool createPipelines();
    bool createSync();
    bool createSceneBuffers();
    void resetCamera();
    void panGround(const glm::vec2& delta);
#ifdef _WIN32
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleWin32Message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
#else
    void handleEvent(const SDL_Event& event);
#endif
    void update(float dt);
    void renderFrame();
  };

  bool BadalingDemoApp::initialize() {
    std::cout << "Initializing Xineng Benyuan Badaling roaming demo..." << std::endl;
    tourPath_ = makeWallPath();
    resetCamera();
    if (!initWindow())
      return false;
    if (!initVulkan())
      return false;
    if (!createDepth() || !createShaders() || !createRenderPass() || !createFramebuffers() || !createPipelines() ||
        !createSync() || !createSceneBuffers()) {
      shutdown();
      return false;
    }

    std::cout << "\n=== Controls ===\n"
              << "Left mouse drag   Pan terrain\n"
              << "Right mouse drag  Look around\n"
              << "WASD              Move\n"
              << "Q/E               Down/Up\n"
              << "Shift             Fast move\n"
              << "T                 Toggle auto tour\n"
              << "--fullscreen      Safe fullscreen at startup\n"
              << "F                 Toggle wireframe\n"
              << "R                 Reset camera\n"
              << "Esc               Quit\n";
    return true;
  }

  void BadalingDemoApp::requestStartupFullscreen() {
    startupFullscreen_ = true;
  }

  bool BadalingDemoApp::initWindow() {
#ifdef _WIN32
    hinstance_ = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = BadalingDemoApp::windowProc;
    wc.hInstance = hinstance_;
    wc.lpszClassName = L"XinengBenyuanBadalingDemoWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    if (startupFullscreen_) {
      MONITORINFO monitorInfo{};
      monitorInfo.cbSize = sizeof(MONITORINFO);
      if (GetMonitorInfoW(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &monitorInfo)) {
        posX = monitorInfo.rcMonitor.left;
        posY = monitorInfo.rcMonitor.top;
        windowWidth_ = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        windowHeight_ = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        style = WS_POPUP;
        fullscreen_ = true;
      }
    }
    windowedStyle_ = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    RECT rect{0, 0, windowWidth_, windowHeight_};
    if (!startupFullscreen_)
      AdjustWindowRect(&rect, style, FALSE);

    hwnd_ = CreateWindowExW(0,
                            wc.lpszClassName,
                            L"Xineng Benyuan - Badaling Great Wall Roaming",
                            style | WS_VISIBLE,
                            posX,
                            posY,
                            rect.right - rect.left,
                            rect.bottom - rect.top,
                            nullptr,
                            nullptr,
                            hinstance_,
                            this);
    if (!hwnd_) {
      std::cerr << "CreateWindowExW failed" << std::endl;
      return false;
    }
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
#else
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
      return false;
    }
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
      std::cerr << "SDL_Vulkan_LoadLibrary warning: " << SDL_GetError() << std::endl;
    }
    uint32_t flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
    if (startupFullscreen_)
      flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window_ = SDL_CreateWindow("Xineng Benyuan - Badaling Great Wall Roaming",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               windowWidth_,
                               windowHeight_,
                               flags);
    if (!window_) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
      SDL_Quit();
      return false;
    }
    return true;
#endif
  }

  bool BadalingDemoApp::initVulkan() {
    auto factory = Backend::createFactory(Backend::Type::Vulkan);
    if (!factory)
      return false;

#ifdef _WIN32
    SwapchainDesc swapchain;
    swapchain.extent = {static_cast<uint32_t>(windowWidth_), static_cast<uint32_t>(windowHeight_)};
    swapchain.format = Format::R8G8B8A8_SRGB;
    swapchain.imageCount = 3;
    swapchain.vsync = false;
    device_ = factory->createDeviceWithWin32Swapchain(hinstance_, hwnd_, swapchain, false);
#else
    unsigned int extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window_, &extensionCount, nullptr);
    std::vector<const char*> extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(window_, &extensionCount, extensions.data());

    SwapchainDesc swapchain;
    swapchain.extent = {static_cast<uint32_t>(windowWidth_), static_cast<uint32_t>(windowHeight_)};
    swapchain.format = Format::R8G8B8A8_SRGB;
    swapchain.imageCount = 3;
    swapchain.vsync = false;
    device_ = factory->createDeviceWithSwapchain(window_, swapchain, false, extensions);
#endif
    Backend::destroyFactory(factory);

    if (!device_)
      return false;
    std::cout << "GPU: " << device_->getProperties().deviceName << std::endl;
    return true;
  }

  bool BadalingDemoApp::createDepth() {
    TextureDesc depthDesc;
    depthDesc.type = TextureType::Texture2D;
    depthDesc.extent = {static_cast<uint32_t>(windowWidth_), static_cast<uint32_t>(windowHeight_), 1};
    depthDesc.format = Format::D32_SFLOAT;
    depthDesc.usage = static_cast<TextureUsageFlags>(TextureUsageFlagBits::DepthStencilAttachment);
    depthDesc.sampleCount = 1;
    depthTexture_ = device_->createTexture(depthDesc);
    if (!depthTexture_)
      return false;
    depthView_ = depthTexture_->createView();
    return depthView_ != nullptr;
  }

  bool BadalingDemoApp::createShaders() {
    return createBadalingShaders(device_, vertexShader_, fragmentShader_);
  }

  bool BadalingDemoApp::createRenderPass() {
    RenderPassDesc desc;

    AttachmentDescription color;
    color.format = device_->getSwapchainFormat();
    color.sampleCount = 1;
    color.initialLayout = ImageLayout::Undefined;
    color.finalLayout = ImageLayout::PresentSrc;
    color.loadOpClear = true;
    color.storeOpStore = true;
    desc.attachments.push_back(color);

    AttachmentDescription depth;
    depth.format = Format::D32_SFLOAT;
    depth.sampleCount = 1;
    depth.initialLayout = ImageLayout::Undefined;
    depth.finalLayout = ImageLayout::DepthStencilAttachmentOptimal;
    depth.loadOpClear = true;
    depth.storeOpStore = false;
    desc.attachments.push_back(depth);

    SubpassDescription subpass;
    AttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = ImageLayout::ColorAttachmentOptimal;
    subpass.colorAttachments.push_back(colorRef);
    subpass.hasDepthStencil = true;
    subpass.depthStencilAttachment.attachment = 1;
    subpass.depthStencilAttachment.layout = ImageLayout::DepthStencilAttachmentOptimal;
    desc.subpasses.push_back(subpass);

    SubpassDependency dep;
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput) |
                       static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
    dep.dstStageMask = dep.srcStageMask;
    dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite) |
                        static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
    dep.byRegion = true;
    desc.dependencies.push_back(dep);

    renderPass_ = device_->createRenderPass(desc);
    return renderPass_ != nullptr;
  }

  bool BadalingDemoApp::createFramebuffers() {
    const auto extent = device_->getSwapchainExtent();
    for (uint32_t i = 0; i < device_->getSwapchainImageCount(); ++i) {
      auto color = device_->getSwapchainImage(i)->createView();
      auto fb = device_->createFramebuffer(renderPass_, {color, depthView_}, extent, 1);
      if (!fb)
        return false;
      framebuffers_.push_back(fb);
    }
    return true;
  }

  bool BadalingDemoApp::createPipelines() {
    if (!createTerrainTextureBinding(device_, terrainTexture_))
      return false;

    PipelineLayoutDesc layoutDesc;
    layoutDesc.setLayouts.push_back(terrainTexture_.layout);
    PushConstantRange push;
    push.stageFlags = badalingPushStages();
    push.offset = 0;
    push.size = sizeof(PushData);
    layoutDesc.pushConstantRanges.push_back(push);
    pipelineLayout_ = device_->createPipelineLayout(layoutDesc);
    if (!pipelineLayout_)
      return false;

    auto binding = Vertex::binding();
    auto attributes = Vertex::attributes();

    (void)binding;
    (void)attributes;
    solidPipeline_ =
        createBadalingPipeline(device_, renderPass_, pipelineLayout_, vertexShader_, fragmentShader_, FillMode::Solid);
    wirePipeline_ = createBadalingPipeline(
        device_, renderPass_, pipelineLayout_, vertexShader_, fragmentShader_, FillMode::Wireframe);
    commandBuffer_ = device_->createCommandBuffer();
    return solidPipeline_ && wirePipeline_ && commandBuffer_;
  }

  bool BadalingDemoApp::createSync() {
    imageAvailable_ = device_->createSemaphore();
    renderFinished_ = device_->createSemaphore();
    inFlight_ = device_->createFence(true);
    return imageAvailable_ && renderFinished_ && inFlight_;
  }

  bool BadalingDemoApp::createSceneBuffers() {
    Mesh scene = buildBadalingScene();
    indexCount_ = static_cast<uint32_t>(scene.indices.size());
    vertexBuffer_ = makeHostBuffer(device_, scene.vertices, BufferUsageFlagBits::Vertex);
    indexBuffer_ = makeHostBuffer(device_, scene.indices, BufferUsageFlagBits::Index);
    std::cout << "Scene: " << scene.vertices.size() << " vertices, " << indexCount_ / 3 << " triangles" << std::endl;
    return vertexBuffer_ && indexBuffer_;
  }

  void BadalingDemoApp::resetCamera() {
    if (loadBadalingAsset() && tourPath_.size() > 12) {
      const size_t i = tourPath_.size() / 2;
      const glm::vec2 p = tourPath_[i];
      const glm::vec2 q = tourPath_[std::min(i + 10, tourPath_.size() - 1)];
      glm::vec2 dir2 = q - p;
      if (glm::length(dir2) < 0.001f)
        dir2 = {1.0f, 0.0f};
      dir2 = glm::normalize(dir2);
      const glm::vec2 side = {-dir2.y, dir2.x};
      const float ground = terrainHeight(p.x, p.y);
      camPos_ = {p.x - dir2.x * 118.0f + side.x * 84.0f, p.y - dir2.y * 118.0f + side.y * 84.0f, ground + 72.0f};
      const glm::vec3 target = {p.x + dir2.x * 18.0f, p.y + dir2.y * 18.0f, ground + 13.0f};
      const glm::vec3 dir = glm::normalize(target - camPos_);
      camYaw_ = std::atan2(dir.x, dir.y);
      camPitch_ = std::asin(glm::clamp(dir.z, -1.0f, 1.0f));
      tourT_ = 0.42f;
      return;
    }

    camPos_ = {-205.0f, -72.0f, 92.0f};
    camYaw_ = 1.05f;
    camPitch_ = -0.22f;
    tourT_ = 0.0f;
  }

  void BadalingDemoApp::panGround(const glm::vec2& delta) {
    const glm::vec3 forward = glm::normalize(glm::vec3{std::sin(camYaw_), std::cos(camYaw_), 0.0f});
    const glm::vec3 right = glm::normalize(glm::vec3{forward.y, -forward.x, 0.0f});
    const float scale = glm::clamp(std::max(camPos_.z, 40.0f) * 0.0042f, 0.12f, 1.25f);
    camPos_ -= right * (delta.x * scale);
    camPos_ += forward * (delta.y * scale);
  }

  void BadalingDemoApp::toggleFullscreen() {
#ifdef _WIN32
    if (!hwnd_)
      return;
    if (device_) {
      std::cout << "Runtime fullscreen resize is disabled for stability; restart with --fullscreen.\n";
      return;
    }
    if (!fullscreen_) {
      windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
      windowPlacement_.length = sizeof(WINDOWPLACEMENT);
      GetWindowPlacement(hwnd_, &windowPlacement_);
      MONITORINFO monitorInfo{};
      monitorInfo.cbSize = sizeof(MONITORINFO);
      if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_,
                     HWND_TOP,
                     monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = true;
      }
    } else {
      SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_ | WS_VISIBLE);
      SetWindowPlacement(hwnd_, &windowPlacement_);
      SetWindowPos(
          hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      fullscreen_ = false;
    }
#else
    if (!window_)
      return;
    const uint32_t flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(window_, SDL_GetWindowFlags(window_) & flag ? 0 : flag);
#endif
  }

#ifdef _WIN32
  LRESULT CALLBACK BadalingDemoApp::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    BadalingDemoApp* app = nullptr;
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      app = static_cast<BadalingDemoApp*>(create->lpCreateParams);
      app->hwnd_ = hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
      app = reinterpret_cast<BadalingDemoApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
      return app->handleWin32Message(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  LRESULT BadalingDemoApp::handleWin32Message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto mousePos = [](LPARAM value) {
      const POINTS p = MAKEPOINTS(value);
      return glm::vec2{static_cast<float>(p.x), static_cast<float>(p.y)};
    };

    switch (message) {
      case WM_CLOSE:
        running_ = false;
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        running_ = false;
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
      case WM_LBUTTONDOWN:
        panning_ = true;
        lastMouse_ = mousePos(lparam);
        SetCapture(hwnd);
        return 0;
      case WM_LBUTTONUP:
        panning_ = false;
        if (!dragging_)
          ReleaseCapture();
        return 0;
      case WM_RBUTTONDOWN:
        dragging_ = true;
        lastMouse_ = mousePos(lparam);
        SetCapture(hwnd);
        return 0;
      case WM_RBUTTONUP:
        dragging_ = false;
        if (!panning_)
          ReleaseCapture();
        return 0;
      case WM_MOUSEMOVE:
        if (dragging_ || panning_) {
          const glm::vec2 cur = mousePos(lparam);
          const glm::vec2 delta = cur - lastMouse_;
          if (dragging_) {
            camYaw_ -= delta.x * 0.003f;
            camPitch_ -= delta.y * 0.003f;
            camPitch_ = glm::clamp(camPitch_, -1.35f, 0.35f);
          } else {
            panGround(delta);
          }
          lastMouse_ = cur;
          autoTour_ = false;
        }
        return 0;
      case WM_MOUSEWHEEL:
        camPos_.z += static_cast<float>(static_cast<short>(HIWORD(wparam))) / WHEEL_DELTA * 6.0f;
        autoTour_ = false;
        return 0;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
        if (wparam < keys_.size())
          keys_[wparam] = true;
        switch (wparam) {
          case VK_ESCAPE:
            running_ = false;
            DestroyWindow(hwnd);
            return 0;
          case VK_F11:
            toggleFullscreen();
            return 0;
          case VK_RETURN:
            if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
              toggleFullscreen();
              return 0;
            }
            break;
          case 'F':
            wireframe_ = !wireframe_;
            return 0;
          case 'T':
            autoTour_ = !autoTour_;
            return 0;
          case 'R':
            resetCamera();
            return 0;
          default:
            break;
        }
        return 0;
      case WM_KEYUP:
      case WM_SYSKEYUP:
        if (wparam < keys_.size())
          keys_[wparam] = false;
        return 0;
      default:
        break;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
#else
  void BadalingDemoApp::handleEvent(const SDL_Event& event) {
    switch (event.type) {
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_RIGHT) {
          dragging_ = true;
          lastMouse_ = {static_cast<float>(event.button.x), static_cast<float>(event.button.y)};
        } else if (event.button.button == SDL_BUTTON_LEFT) {
          panning_ = true;
          lastMouse_ = {static_cast<float>(event.button.x), static_cast<float>(event.button.y)};
        }
        break;
      case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_RIGHT)
          dragging_ = false;
        if (event.button.button == SDL_BUTTON_LEFT)
          panning_ = false;
        break;
      case SDL_MOUSEMOTION:
        if (dragging_ || panning_) {
          glm::vec2 cur = {static_cast<float>(event.motion.x), static_cast<float>(event.motion.y)};
          glm::vec2 delta = cur - lastMouse_;
          if (dragging_) {
            camYaw_ -= delta.x * 0.003f;
            camPitch_ -= delta.y * 0.003f;
            camPitch_ = glm::clamp(camPitch_, -1.35f, 0.35f);
          } else {
            panGround(delta);
          }
          lastMouse_ = cur;
          autoTour_ = false;
        }
        break;
      case SDL_MOUSEWHEEL:
        camPos_.z += static_cast<float>(event.wheel.y) * 6.0f;
        autoTour_ = false;
        break;
      case SDL_KEYDOWN:
        switch (event.key.keysym.sym) {
          case SDLK_F11:
            toggleFullscreen();
            break;
          case SDLK_RETURN:
            if ((event.key.keysym.mod & KMOD_ALT) != 0)
              toggleFullscreen();
            break;
          case SDLK_f:
            wireframe_ = !wireframe_;
            break;
          case SDLK_t:
            autoTour_ = !autoTour_;
            break;
          case SDLK_r:
            resetCamera();
            break;
          default:
            break;
        }
        break;
    }
  }
#endif

  void BadalingDemoApp::update(float dt) {
#ifdef _WIN32
    const auto keyDown = [this](int key) {
      return key >= 0 && static_cast<size_t>(key) < keys_.size() && keys_[key];
    };
    const float speed = moveSpeed_ * (keyDown(VK_SHIFT) ? 3.0f : 1.0f) * dt;
#else
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    const float speed = moveSpeed_ * (keys[SDL_SCANCODE_LSHIFT] ? 3.0f : 1.0f) * dt;
#endif

    if (autoTour_ && tourPath_.size() > 3) {
      tourT_ = std::fmod(tourT_ + dt * 0.035f, 1.0f);
      const float pos = tourT_ * static_cast<float>(tourPath_.size() - 1);
      const int i = glm::clamp(static_cast<int>(pos), 0, static_cast<int>(tourPath_.size() - 2));
      const float localT = pos - static_cast<float>(i);
      const glm::vec2 p = glm::mix(tourPath_[i], tourPath_[i + 1], localT);
      const glm::vec2 q = tourPath_[std::min(i + 3, static_cast<int>(tourPath_.size() - 1))];
      const glm::vec2 dir2 = glm::normalize(q - p);
      camPos_ = {p.x - dir2.x * 22.0f, p.y - dir2.y * 22.0f, terrainHeight(p.x, p.y) + 35.0f};
      glm::vec3 target = {q.x, q.y, terrainHeight(q.x, q.y) + 12.0f};
      glm::vec3 dir = glm::normalize(target - camPos_);
      camYaw_ = std::atan2(dir.x, dir.y);
      camPitch_ = std::asin(glm::clamp(dir.z, -1.0f, 1.0f));
    }

    bool manual = false;
    glm::vec3 forward = {
        std::sin(camYaw_) * std::cos(camPitch_), std::cos(camYaw_) * std::cos(camPitch_), std::sin(camPitch_)};
    glm::vec3 right = {std::cos(camYaw_), -std::sin(camYaw_), 0.0f};
#ifdef _WIN32
    if (keyDown('W')) {
      camPos_ += forward * speed;
      manual = true;
    }
    if (keyDown('S')) {
      camPos_ -= forward * speed;
      manual = true;
    }
    if (keyDown('A')) {
      camPos_ -= right * speed;
      manual = true;
    }
    if (keyDown('D')) {
      camPos_ += right * speed;
      manual = true;
    }
    if (keyDown('E')) {
      camPos_.z += speed;
      manual = true;
    }
    if (keyDown('Q')) {
      camPos_.z -= speed;
      manual = true;
    }
#else
    if (keys[SDL_SCANCODE_W]) {
      camPos_ += forward * speed;
      manual = true;
    }
    if (keys[SDL_SCANCODE_S]) {
      camPos_ -= forward * speed;
      manual = true;
    }
    if (keys[SDL_SCANCODE_A]) {
      camPos_ -= right * speed;
      manual = true;
    }
    if (keys[SDL_SCANCODE_D]) {
      camPos_ += right * speed;
      manual = true;
    }
    if (keys[SDL_SCANCODE_E]) {
      camPos_.z += speed;
      manual = true;
    }
    if (keys[SDL_SCANCODE_Q]) {
      camPos_.z -= speed;
      manual = true;
    }
#endif
    if (manual)
      autoTour_ = false;

    const float ground = terrainHeight(camPos_.x, camPos_.y) + 4.0f;
    camPos_.z = std::max(camPos_.z, ground);
  }

  void BadalingDemoApp::renderFrame() {
    inFlight_->wait();
    inFlight_->reset();
    const uint32_t imageIndex = device_->acquireNextImage(imageAvailable_);

    glm::vec3 lookDir = {
        std::sin(camYaw_) * std::cos(camPitch_), std::cos(camYaw_) * std::cos(camPitch_), std::sin(camPitch_)};
    glm::mat4 view = glm::lookAt(camPos_, camPos_ + lookDir, {0.0f, 0.0f, 1.0f});
    glm::mat4 proj = glm::perspective(
        glm::radians(62.0f), static_cast<float>(windowWidth_) / static_cast<float>(windowHeight_), 0.1f, 1200.0f);
    proj[1][1] *= -1.0f;
    PushData push = makePushData(proj * view, camPos_, elapsedSeconds_);

    commandBuffer_->reset();
    commandBuffer_->begin();

    Rect2D area;
    area.offset = {0, 0};
    area.extent = device_->getSwapchainExtent();
    const auto& weather = activeWeather();
    std::vector<std::array<float, 4>> clearColors = {
        {weather.skyColor.r, weather.skyColor.g, weather.skyColor.b, 1.0f}};
    commandBuffer_->beginRenderPass(renderPass_, framebuffers_[imageIndex], area, clearColors, 1.0f, 0);

    Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(area.extent.width);
    viewport.height = static_cast<float>(area.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer_->setViewport(viewport);
    commandBuffer_->setScissor(area);

    commandBuffer_->bindPipeline(wireframe_ ? wirePipeline_ : solidPipeline_);
    commandBuffer_->bindDescriptorSet(0, terrainTexture_.descriptorSet, pipelineLayout_);
    commandBuffer_->pushConstants(pipelineLayout_, badalingPushStages(), 0, sizeof(push), &push);
    commandBuffer_->bindVertexBuffer(0, vertexBuffer_);
    commandBuffer_->bindIndexBuffer(indexBuffer_);
    commandBuffer_->drawIndexed(indexCount_);

    commandBuffer_->endRenderPass();
    commandBuffer_->end();

    device_->submit({commandBuffer_}, {imageAvailable_}, {renderFinished_}, inFlight_);
    device_->present(renderFinished_);
  }

  void BadalingDemoApp::run(int maxFrames) {
#ifdef _WIN32
    running_ = true;
#else
    bool running = true;
    SDL_Event event;
#endif
    auto last = std::chrono::high_resolution_clock::now();
    int frames = 0;

#ifdef _WIN32
    while (running_) {
#else
    while (running) {
#endif
      auto now = std::chrono::high_resolution_clock::now();
      const float dt = std::chrono::duration<float>(now - last).count();
      last = now;

#ifdef _WIN32
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
          running_ = false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (!running_)
        break;
#else
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
          running = false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
          running = false;
        handleEvent(event);
      }
#endif

      update(std::min(dt, 0.05f));
      elapsedSeconds_ += dt;
      renderFrame();

      ++frames;
      ++frameCount_;
      fpsTimer_ += dt;
      if (fpsTimer_ >= 1.0) {
        const int fps = static_cast<int>(static_cast<double>(frameCount_) / fpsTimer_);
        std::string title = "Xineng Benyuan | Badaling Great Wall | " + std::to_string(fps) + " FPS" + " | " +
                            activeWeather().name + (autoTour_ ? " | Auto tour" : " | Manual") +
                            (wireframe_ ? " | Wireframe" : "");
#ifdef _WIN32
        std::wstring wideTitle(title.begin(), title.end());
        SetWindowTextW(hwnd_, wideTitle.c_str());
#else
        SDL_SetWindowTitle(window_, title.c_str());
#endif
        frameCount_ = 0;
        fpsTimer_ = 0.0;
      }

#ifdef _WIN32
      if (maxFrames > 0 && frames >= maxFrames)
        running_ = false;
#else
      if (maxFrames > 0 && frames >= maxFrames)
        running = false;
#endif
    }

    if (device_)
      device_->waitIdle();
  }

  void BadalingDemoApp::shutdown() {
    if (device_) {
      device_->waitIdle();
      commandBuffer_.reset();
      inFlight_.reset();
      imageAvailable_.reset();
      renderFinished_.reset();
      solidPipeline_.reset();
      wirePipeline_.reset();
      pipelineLayout_.reset();
      framebuffers_.clear();
      renderPass_.reset();
      depthView_.reset();
      depthTexture_.reset();
      terrainTexture_.descriptorSet.reset();
      terrainTexture_.sampler.reset();
      terrainTexture_.view.reset();
      terrainTexture_.texture.reset();
      terrainTexture_.layout.reset();
      vertexBuffer_.reset();
      indexBuffer_.reset();
      vertexShader_.reset();
      fragmentShader_.reset();
      delete device_;
      device_ = nullptr;
    }
#ifdef _WIN32
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
#else
    if (window_) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    SDL_Quit();
#endif
  }

} // namespace

int main(int argc, char** argv) {
  int maxFrames = 0;
  int width = 1280;
  int height = 720;
  bool startFullscreen = false;
  std::string screenshotPath;
  std::string statsPath;
  std::string presetName = "clear";
  bool hasSunAzimuth = false;
  bool hasSunElevation = false;
  bool hasRainOverride = false;
  bool hasSnowOverride = false;
  bool hasFogDensityOverride = false;
  bool hasCloudAmountOverride = false;
  float sunAzimuth = 0.0f;
  float sunElevation = 0.0f;
  float rainOverride = 0.0f;
  float snowOverride = 0.0f;
  float fogDensityOverride = 0.0f;
  float cloudAmountOverride = 0.0f;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--frames=", 0) == 0) {
      maxFrames = std::stoi(arg.substr(9));
    } else if (arg.rfind("--screenshot=", 0) == 0) {
      screenshotPath = arg.substr(13);
    } else if (arg.rfind("--stats=", 0) == 0) {
      statsPath = arg.substr(8);
    } else if (arg.rfind("--preset=", 0) == 0) {
      presetName = arg.substr(9);
    } else if (arg.rfind("--weather=", 0) == 0) {
      presetName = arg.substr(10);
    } else if (arg.rfind("--sun-azimuth=", 0) == 0) {
      sunAzimuth = std::stof(arg.substr(14));
      hasSunAzimuth = true;
    } else if (arg.rfind("--sun-elevation=", 0) == 0) {
      sunElevation = std::stof(arg.substr(16));
      hasSunElevation = true;
    } else if (arg.rfind("--rain=", 0) == 0) {
      rainOverride = std::stof(arg.substr(7));
      hasRainOverride = true;
    } else if (arg.rfind("--snow=", 0) == 0) {
      snowOverride = std::stof(arg.substr(7));
      hasSnowOverride = true;
    } else if (arg.rfind("--fog-density=", 0) == 0) {
      fogDensityOverride = std::stof(arg.substr(14));
      hasFogDensityOverride = true;
    } else if (arg.rfind("--clouds=", 0) == 0) {
      cloudAmountOverride = std::stof(arg.substr(9));
      hasCloudAmountOverride = true;
    } else if (arg.rfind("--width=", 0) == 0) {
      width = std::stoi(arg.substr(8));
    } else if (arg.rfind("--height=", 0) == 0) {
      height = std::stoi(arg.substr(9));
    } else if (arg == "--fullscreen") {
      startFullscreen = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "badaling_demo [--preset=clear|cloudy|rain|snow|fog|night] "
                   "[--sun-azimuth=DEG --sun-elevation=DEG] "
                   "[--rain=0..1 --snow=0..1 --fog-density=0..1 --clouds=0..2] "
                   "[--frames=N] [--fullscreen] [--screenshot=PATH --stats=PATH --width=W --height=H]\n";
      return 0;
    }
  }

  if (!configureBadalingPreset(presetName)) {
    std::cerr << "Unknown preset: " << presetName << " (expected clear, cloudy, rain, snow, fog, night)\n";
    return 2;
  }
  if (hasSunAzimuth || hasSunElevation) {
    overrideSunAngles(hasSunAzimuth, sunAzimuth, hasSunElevation, sunElevation);
  }
  overrideWeatherControls(hasRainOverride,
                          rainOverride,
                          hasSnowOverride,
                          snowOverride,
                          hasFogDensityOverride,
                          fogDensityOverride,
                          hasCloudAmountOverride,
                          cloudAmountOverride);

  if (!screenshotPath.empty()) {
    return renderOffscreenBadaling(screenshotPath, statsPath, width, height) ? 0 : 1;
  }

  BadalingDemoApp app;
  if (startFullscreen)
    app.requestStartupFullscreen();
  if (!app.initialize())
    return 1;
  app.run(maxFrames);
  app.shutdown();
  return 0;
}
