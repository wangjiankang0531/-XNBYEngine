#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/gis/tile/dem_baker.hpp>
#include <hongtu/renderer/globe/ancestor_fallback.hpp>
#include <hongtu/renderer/globe/earth_texture_loader.hpp>
#include <hongtu/renderer/globe/ellipsoid_mesh.hpp>
#include <hongtu/renderer/globe/globe_renderer.hpp>
#include <hongtu/renderer/globe/rgba8_mip_chain.hpp>
#include <hongtu/renderer/globe/tile_mesh_builder.hpp>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include "water_shading_glsl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
namespace hongtu::renderer {

  using namespace HongTu::RHI;
  using namespace HongTu::Asset;

  // ============================================================================
  // Internal vertex / push-constant structures
  // ============================================================================

  struct GlobeVertex {
    glm::vec3 position; // location 0
    glm::vec3 normal;   // location 1 -- used as color (imagery baked in)
    glm::vec2 uv;       // location 2

    static VertexInputBindingDescription getBindingDescription() {
      VertexInputBindingDescription b;
      b.binding = 0;
      b.stride = sizeof(GlobeVertex);
      b.perInstance = false;
      return b;
    }

    static std::vector<VertexInputAttributeDescription> getAttributeDescriptions() {
      std::vector<VertexInputAttributeDescription> attrs(3);
      attrs[0] = {0, 0, Format::R32G32B32_SFLOAT, offsetof(GlobeVertex, position)};
      attrs[1] = {1, 0, Format::R32G32B32_SFLOAT, offsetof(GlobeVertex, normal)};
      attrs[2] = {2, 0, Format::R32G32_SFLOAT, offsetof(GlobeVertex, uv)};
      return attrs;
    }
  };

  struct GlobePushData {
    glm::mat4 mvp;        // offset 0,  64 bytes
    glm::vec4 sun_dir;    // offset 64, 16 bytes (xyz = sun dir ECEF, w = cloud fade)
    glm::vec4 cam_rtc;    // offset 80, 16 bytes (xyz = camera in RTC, w = time s)
    uint32_t use_texture; // offset 96, 4 bytes
    float pad[3];         // offset 100, 12 bytes padding
  };
  // sizeof = 112 bytes

  gis::TileData makeFallbackTile(const gis::TileCoord& coord) {
    constexpr i32 kSize = 64;
    gis::TileData tile;
    tile.width = kSize;
    tile.height = kSize;
    tile.image_data.resize(static_cast<size_t>(kSize * kSize * 4));

    const auto bounds = coord.toBounds();
    for (i32 y = 0; y < kSize; ++y) {
      const f64 v = static_cast<f64>(y) / static_cast<f64>(kSize - 1);
      const f64 lat = bounds.north + (bounds.south - bounds.north) * v;

      for (i32 x = 0; x < kSize; ++x) {
        const f64 u = static_cast<f64>(x) / static_cast<f64>(kSize - 1);
        const f64 lon = bounds.west + (bounds.east - bounds.west) * u;

        const f64 wave = std::sin(lon * 6.0) * 0.35 + std::cos(lat * 9.0) * 0.35 + std::sin((lat + lon) * 5.0) * 0.3;
        const bool land = wave > -0.08 || std::abs(lat) > 1.1;
        const size_t idx = static_cast<size_t>((y * kSize + x) * 4);
        if (land) {
          tile.image_data[idx] = 90;
          tile.image_data[idx + 1] = 126;
          tile.image_data[idx + 2] = 76;
        } else {
          tile.image_data[idx] = 26;
          tile.image_data[idx + 1] = 76;
          tile.image_data[idx + 2] = 130;
        }
        tile.image_data[idx + 3] = 255;
      }
    }

    return tile;
  }

  // ============================================================================
  // Construction / Destruction
  // ============================================================================

  GlobeRenderer::GlobeRenderer(RHIDevice* device) : device_(device), download_queue_(http_client_) {
  }

  GlobeRenderer::~GlobeRenderer() {
    label_renderer_.cleanup();
    vector_layers_.cleanup();
    atmosphere_.cleanup();
    water_surfaces_.cleanup();
    skybox_.reset();
    gpu_cache_.clear();
    earth_desc_set_.reset();
    cloud_view_.reset();
    cloud_texture_.reset();
    earth_east_view_.reset();
    earth_west_view_.reset();
    earth_east_texture_.reset();
    earth_west_texture_.reset();
    earth_sampler_.reset();
    earth_desc_layout_.reset();
    pipeline_.reset();
    pipeline_layout_.reset();
    vertex_shader_.reset();
    fragment_shader_.reset();
    tile_sampler_.reset();
    tile_pipeline_.reset();
    tile_pipeline_layout_.reset();
    tile_desc_layout_.reset();
    tile_fs_.reset();
    tile_vs_.reset();
  }

  // ============================================================================
  // Configuration
  // ============================================================================

  void GlobeRenderer::addImageryLayer(std::shared_ptr<gis::ImageryProvider> provider) {
    if (!provider) {
      return;
    }
    if (imagery_layers_.empty()) {
      tile_cache_.setNamespace(provider->getCacheNamespace());
      // A sparse/offline provider may deliberately stop well below the
      // selector's generic z18 ceiling. Selecting beyond that ceiling only
      // creates unavailable leaves and prevents real z8/z10 parents from
      // becoming the drawing set during a direct camera jump.
      gis::TileQuadTreeSelector::Config config;
      config.min_level = std::max(config.min_level, provider->getMinLevel());
      config.max_level = std::min(config.max_level, provider->getMaxLevel());
      if (config.min_level > config.max_level) {
        config.min_level = config.max_level;
      }
      quad_tree_ = gis::TileQuadTreeSelector(config);
    }
    imagery_layers_.push_back(std::move(provider));
  }

  void GlobeRenderer::clearImageryLayers() {
    imagery_layers_.clear();
    // Drop everything that referenced the old provider's URLs: in-flight HTTP
    // downloads, decoded-but-not-uploaded pixels, GPU-cached textures, the
    // visible/drawing sets, and — crucially — the in-memory TileCache. The
    // The namespace change performed by the next addImageryLayer() isolates the
    // persistent cache. Memory is still cleared here to immediately release the
    // previous provider's decoded pixels and prevent transient cross-source use.
    download_queue_.cancelAll();
    tile_cache_.clearMemory();
    decoded_tiles_.clear();
    loading_tiles_.clear();
    unavailable_tiles_.clear();
    gpu_cache_.clear();
    visible_tiles_.clear();
    drawing_set_.clear();
    drawing_set_info_.clear();
    leaf_info_.clear();
    tile_cache_.setNamespace({});
  }

  void GlobeRenderer::setSunDirection(const glm::vec3& dir) {
    sun_dir_ecef_ = glm::normalize(dir);
    sun_animated_ = false;
  }

  int GlobeRenderer::addVectorLayer(const gis::FeatureCollection& fc, const VectorStyle& style) {
    return vector_layers_.addLayer(fc, style);
  }

  int GlobeRenderer::addWaterBody(std::vector<coordinate::Geodetic> boundary, const WaterStyle& style) {
    return water_surfaces_.addWaterBody(std::move(boundary), style);
  }

  void GlobeRenderer::removeWaterBody(int id) {
    water_surfaces_.removeWaterBody(id);
  }

  void GlobeRenderer::clearWaterBodies() {
    water_surfaces_.clearWaterBodies();
  }

  void GlobeRenderer::removeVectorLayer(int layer_id) {
    vector_layers_.removeLayer(layer_id);
  }

  void GlobeRenderer::setTileCachePath(const std::string& path) {
    tile_cache_.setCachePath(path);
  }

  void GlobeRenderer::setAssetRoot(const std::string& path) {
    asset_root_ = path;
  }

  void GlobeRenderer::setMemoryBudget(size_t bytes) {
    tile_cache_.setMemoryBudget(bytes);
  }

  void GlobeRenderer::setMaxConcurrentDownloads(i32 n) {
    download_queue_.setMaxConcurrent(n);
  }

  void GlobeRenderer::setScreenSize(f32 width, f32 height) {
    screen_width_ = width;
    screen_height_ = height;
  }

  void GlobeRenderer::setTileRenderingEnabled(bool enabled) {
    tile_rendering_enabled_ = enabled;
    if (!tile_rendering_enabled_) {
      visible_tiles_.clear();
      drawing_set_.clear();
      drawing_set_info_.clear();
      gpu_cache_.clear();
      decoded_tiles_.clear();
      loading_tiles_.clear();
      unavailable_tiles_.clear();
    }
  }

  void GlobeRenderer::setDemProvider(std::shared_ptr<gis::DemProvider> provider) {
    dem_provider_ = std::move(provider);
  }

  void GlobeRenderer::addLabel(const Label& label) {
    label_renderer_.addLabel(label);
  }

  void GlobeRenderer::clearLabels() {
    label_renderer_.clearLabels();
  }

  // ============================================================================
  // Initialization
  // ============================================================================

  bool GlobeRenderer::initialize(RHIRenderPassPtr render_pass) {
    if (!device_ || !render_pass) {
      spdlog::error("GlobeRenderer::initialize: null device or render_pass");
      return false;
    }

    // ------------------------------------------------------------------
    // 1. Compile shaders
    //    Vertex: passes position, vertex-color, UV through.
    //    Fragment: when use_texture=1 samples the west/east earth textures;
    //              otherwise uses vertex color (tile overlay fallback).
    // ------------------------------------------------------------------
    ShaderCompiler compiler;

    const char* vs_src = R"glsl(
    layout(location = 0) in vec3 inPos;
    layout(location = 1) in vec3 inColor;
    layout(location = 2) in vec2 inUV;
    layout(location = 0) out vec3 outColor;
    layout(location = 1) out vec2 outUV;
    layout(location = 2) out vec3 outWorldPos;  // RTC position, for water shading
    layout(push_constant) uniform PC {
        mat4  mvp;
        vec4  sun_dir;
        vec4  cam_rtc;
        uint  use_texture;
        float pad0; float pad1; float pad2;
    } pc;
    void main() {
        gl_Position = pc.mvp * vec4(inPos, 1.0);
        outColor = inColor;
        outUV = inUV;
        outWorldPos = inPos;
    }
  )glsl";

    // westTex covers globe u=[0,0.5] (lon -180° to 0°)
    // eastTex covers globe u=[0.5,1] (lon 0° to +180°)
    // cloudTex is equirectangular full-globe cloud overlay
    const char* fs_src = R"glsl(
    layout(set = 0, binding = 0) uniform sampler2D westTex;
    layout(set = 0, binding = 1) uniform sampler2D eastTex;
    layout(set = 0, binding = 2) uniform sampler2D cloudTex;
    layout(location = 0) in vec3 inColor;
    layout(location = 1) in vec2 inUV;
    layout(location = 2) in vec3 inWorldPos;
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4  mvp;
        vec4  sun_dir;
        vec4  cam_rtc;
        uint  use_texture;
        float pad0; float pad1; float pad2;
    } pc;
    void main() {
        if (pc.use_texture != 0u) {
            // Sample earth base color (west or east hemisphere)
            vec4 baseColor;
            if (inUV.x < 0.5) {
                baseColor = texture(westTex, vec2(inUV.x * 2.0, inUV.y));
            } else {
                baseColor = texture(eastTex, vec2((inUV.x - 0.5) * 2.0, inUV.y));
            }

            // Reconstruct surface normal in ECEF from spherical UV
            const float PI = 3.14159265;
            float lat = (0.5 - inUV.y) * PI;           // -PI/2 (south) to PI/2 (north)
            float lon = (inUV.x - 0.5) * 2.0 * PI;     // -PI to PI
            vec3 N = normalize(vec3(cos(lat)*cos(lon), cos(lat)*sin(lon), sin(lat)));

            // Lambertian sun lighting
            float NdotL = dot(N, pc.sun_dir.xyz);
            float diffuse = max(0.0, NdotL);

            // Soft terminator transition (twilight zone)
            float dayFactor = smoothstep(-0.12, 0.12, NdotL);

            // Ambient light (tiny — deep space, no sky)
            vec3 ambient = vec3(0.025, 0.025, 0.04);

            // Day color: ambient + Lambertian
            vec3 dayColor = baseColor.rgb * (ambient + vec3(0.9) * diffuse);

            // Night color: very dark with faint blue tint
            vec3 nightColor = baseColor.rgb * ambient;

            // Sunset/sunrise glow at terminator — orange-red fringe
            float sunsetGlow = exp(-abs(NdotL) * 7.0) * 0.18;
            vec3  sunsetColor = vec3(1.0, 0.45, 0.1) * sunsetGlow * max(0.0, 1.0 - diffuse * 3.0);

            // Blend day/night with terminator glow
            vec3 litColor = mix(nightColor, dayColor, dayFactor) + sunsetColor;

            // Cloud overlay (equirectangular, full globe UV).
            // pc.sun_dir.w = cloud fade factor [0=no clouds, 1=full clouds].
            float cloudDensity = texture(cloudTex, inUV).r;
            float cloudDiffuse = max(0.0, NdotL + 0.1);
            vec3 cloudColor = vec3(0.96, 0.96, 0.96) * (ambient + vec3(0.88) * cloudDiffuse);
            cloudColor *= dayFactor;

            float cloudBlend = cloudDensity * 0.65 * pc.sun_dir.w;
            vec3 finalColor = mix(litColor, cloudColor, cloudBlend);

            // Water shading on ocean pixels (M2.5). Same path as the streamed
            // tiles, so the base globe doesn't read as flat dark sea next to
            // glinting tile water at grazing/low altitude. waterMask rejects the
            // bright cloud overlay, so clouds don't glint. N is the geodetic
            // up reconstructed above; view from the camera (both RTC, ECEF-aligned).
            vec3 viewDir = normalize(pc.cam_rtc.xyz - inWorldPos);
            float mask = waterMask(finalColor);
            finalColor = applyWaterGlint(finalColor, N, inWorldPos, viewDir,
                                         pc.sun_dir.xyz, pc.cam_rtc.w, mask);

            outColor = vec4(finalColor, 1.0);
        } else {
            outColor = vec4(inColor, 1.0);
        }
    }
  )glsl";

    const std::string fs_full = std::string(kWaterGlsl) + fs_src;
    auto vs_result = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
    auto fs_result = compiler.compileGLSLToSPIRV(fs_full, "main", "fragment");
    if (!vs_result.success || !fs_result.success) {
      spdlog::error("GlobeRenderer shader compilation failed: vert='{}' frag='{}'",
                    vs_result.errorMessage,
                    fs_result.errorMessage);
      return false;
    }

    ShaderDesc vs_desc;
    vs_desc.stage = ShaderStageFlagBits::Vertex;
    vs_desc.spirvCode = vs_result.spirvCode;
    vertex_shader_ = device_->createShader(vs_desc);

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStageFlagBits::Fragment;
    fs_desc.spirvCode = fs_result.spirvCode;
    fragment_shader_ = device_->createShader(fs_desc);

    if (!vertex_shader_ || !fragment_shader_) {
      spdlog::error("GlobeRenderer: failed to create shader modules");
      return false;
    }

    // ------------------------------------------------------------------
    // 2. Descriptor set layout: two combined image samplers (west + east)
    // ------------------------------------------------------------------
    {
      DescriptorSetLayoutDesc dl;
      DescriptorSetLayoutBinding b0;
      b0.binding = 0u;
      b0.descriptorCount = 1u;
      b0.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
      b0.descriptorType = DescriptorType::CombinedImageSampler;
      dl.bindings.push_back(b0);

      DescriptorSetLayoutBinding b1;
      b1.binding = 1u;
      b1.descriptorCount = 1u;
      b1.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
      b1.descriptorType = DescriptorType::CombinedImageSampler;
      dl.bindings.push_back(b1);

      DescriptorSetLayoutBinding b2;
      b2.binding = 2u;
      b2.descriptorCount = 1u;
      b2.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
      b2.descriptorType = DescriptorType::CombinedImageSampler;
      dl.bindings.push_back(b2);

      earth_desc_layout_ = device_->createDescriptorSetLayout(dl);
      if (!earth_desc_layout_) {
        spdlog::error("GlobeRenderer: failed to create earth descriptor set layout");
        return false;
      }
    }

    // ------------------------------------------------------------------
    // 3. Pipeline layout: set 0 = earth textures, push constants = mvp + flag
    // ------------------------------------------------------------------
    {
      PipelineLayoutDesc ld;
      ld.setLayouts.push_back(earth_desc_layout_);

      PushConstantRange pcr;
      pcr.stageFlags = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;
      pcr.offset = 0;
      pcr.size = sizeof(GlobePushData);
      ld.pushConstantRanges.push_back(pcr);

      pipeline_layout_ = device_->createPipelineLayout(ld);
      if (!pipeline_layout_) {
        spdlog::error("GlobeRenderer: failed to create pipeline layout");
        return false;
      }
    }

    // ------------------------------------------------------------------
    // 4. Graphics pipeline
    // ------------------------------------------------------------------
    {
      auto bind_desc = GlobeVertex::getBindingDescription();
      auto attr_desc = GlobeVertex::getAttributeDescriptions();

      GraphicsPipelineDesc pd;
      pd.shaders = {vertex_shader_, fragment_shader_};
      pd.vertexInput.bindings = {bind_desc};
      pd.vertexInput.attributes = attr_desc;
      pd.primitiveTopology = PrimitiveTopology::TriangleList;
      pd.rasterization.cullMode = CullMode::Back;
      pd.rasterization.frontCounterClockwise = true; // matches GlobeCamera Y-flip
      pd.rasterization.fillMode = FillMode::Solid;
      pd.depthStencil.depthTestEnable = true;
      pd.depthStencil.depthWriteEnable = true;
      pd.depthStencil.depthCompareOp = CompareOp::Less;
      pd.renderPass = render_pass;
      pd.pipelineLayout = pipeline_layout_;
      pipeline_ = device_->createGraphicsPipeline(pd);
      if (!pipeline_) {
        spdlog::error("GlobeRenderer: failed to create graphics pipeline");
        return false;
      }
    }

    // ------------------------------------------------------------------
    // 4b. Tile pipeline: each tile samples its own OSM/imagery texture.
    // Vertex format: GlobeVertex (position/normal/uv) — normal is unused here.
    // ------------------------------------------------------------------
    {
      const char* tile_vs = R"glsl(
      layout(location = 0) in  vec3 inPos;
      layout(location = 1) in  vec3 inNormal;  // unused for tiles
      layout(location = 2) in  vec2 inUV;
      layout(location = 0) out vec2 outUV;
      layout(location = 1) out vec2 outParentUV;
      layout(location = 2) out float outEdgeWeight;
      layout(location = 3) out vec3 outWorldPos;  // RTC position, for water shading
      layout(push_constant) uniform PC {
          mat4 vp;
          vec4 uv_offset_scale;
          vec4 parent_uv_offset_scale;
          vec4 edge_fade;  // (L, R, T, B) — 1.0 = blend toward parent on this side
          vec4 sun_dir_time;  // xyz = sun dir (ECEF), w = time (s)
          vec4 cam_rtc;       // xyz = camera in RTC, w = altitude (m)
          vec4 rtc_origin;    // xyz = RTC origin in ECEF, w = tile LOD (0 = no parent)
      } pc;
      void main() {
          outUV = inUV * pc.uv_offset_scale.zw + pc.uv_offset_scale.xy;
          outParentUV = inUV * pc.parent_uv_offset_scale.zw + pc.parent_uv_offset_scale.xy;

          // Compute per-vertex blend weight: 1.0 = full self, 0.0 = full parent.
          // Edges flagged in edge_fade ramp from 0 at the edge toward the tile
          // interior. At close range keep the original narrow transition for
          // sharp local DOM; at regional altitude broaden it because satellite
          // mosaics can differ strongly from their parent-level exposure.
          float altitudeFade = smoothstep(250000.0, 750000.0, pc.cam_rtc.w);
          float fadeWidth = mix(0.3, 0.85, altitudeFade);
          float w = 1.0;
          if (pc.edge_fade.x > 0.5) w = min(w, smoothstep(0.0, fadeWidth, inUV.x));
          if (pc.edge_fade.y > 0.5) w = min(w, smoothstep(0.0, fadeWidth, 1.0 - inUV.x));
          if (pc.edge_fade.z > 0.5) w = min(w, smoothstep(0.0, fadeWidth, inUV.y));
          if (pc.edge_fade.w > 0.5) w = min(w, smoothstep(0.0, fadeWidth, 1.0 - inUV.y));
          outEdgeWeight = w;
          outWorldPos = inPos;

          gl_Position = pc.vp * vec4(inPos, 1.0);
      }
    )glsl";
      const char* tile_fs = R"glsl(
      layout(set = 0, binding = 0) uniform sampler2D tileTex;
      layout(set = 1, binding = 0) uniform sampler2D parentTex;
      layout(set = 2, binding = 0) uniform sampler2D waterMaskTex;  // global, equirect
      layout(location = 0) in  vec2 inUV;
      layout(location = 1) in  vec2 inParentUV;
      layout(location = 2) in  float inEdgeWeight;
      layout(location = 3) in  vec3 inWorldPos;
      layout(location = 0) out vec4 outColor;
      layout(push_constant) uniform PC {
          mat4 vp;
          vec4 uv_offset_scale;
          vec4 parent_uv_offset_scale;
          vec4 edge_fade;
          vec4 sun_dir_time;
          vec4 cam_rtc;
          vec4 rtc_origin;
      } pc;
      void main() {
          vec4 c = texture(tileTex, inUV);
          vec4 p = texture(parentTex, inParentUV);

          // If this tile's sample is no-data (literal RGB(0,0,0) Tianditu
          // marker for satellite-coverage gaps), fall back to the coarser
          // parent texture directly. No discard, no base-globe through —
          // the parent's wider footprint and resampling averaging usually
          // fills in.
          vec3 rgb;
          if (c.r + c.g + c.b < 0.02) {
              if (p.r + p.g + p.b < 0.02) discard;  // parent also empty → base globe
              rgb = p.rgb;
          } else {
              // Normal pixel: blend self → parent across edges that face a
              // coarser-LOD neighbor in the drawing set. Blending in TEXTURE
              // space (not via a separate geometric layer) avoids the ghost
              // double-surface that the previous alpha-blend attempt produced
              // at low altitude.
              // The complete mip chain removes high-altitude aliasing. At
              // regional/global altitude, continuously retain more of the
              // parent image across the whole child tile. Unlike per-tile
              // low-frequency correction this remains continuous at sibling
              // borders; close views automatically return to full detail.
              // Apply full strength to z8+ regional detail, reduced strength
              // to the z7 transition ring, and none to coarser context. Full
              // recursive blending amplified a distant z7 sibling seam;
              // excluding z7 entirely left the main regional rectangle
              // unchanged. The reduced ring balances both discontinuities.
              float transitionLodGate = step(6.5, pc.rtc_origin.w);
              float detailLodGate = mix(0.4, 1.0, step(7.5, pc.rtc_origin.w)) * transitionLodGate;
              float altitudeHarmonize = smoothstep(250000.0, 750000.0, pc.cam_rtc.w) * 0.38 * detailLodGate;
              float sampledLod = max(textureQueryLod(tileTex, inUV).x, 0.0);
              float parentHarmonize = max(altitudeHarmonize, smoothstep(0.75, 3.0, sampledLod) * 0.16);
              float selfWeight = inEdgeWeight * (1.0 - parentHarmonize);
              rgb = mix(p.rgb, c.rgb, selfWeight);
          }

          // Water shading. The mask combines two sources:
          //  * GLOBAL water mask (set 2) sampled by lat/lon — one consistent
          //    answer for "is this ocean", immune to per-tile imagery color
          //    differences (the source of blocky water boundaries).
          //  * Per-pixel color classification — catches lakes/rivers below
          //    the global mask's resolution (~5 km/texel).
          vec3 worldEcef = inWorldPos + pc.rtc_origin.xyz;
          vec3 up = normalize(worldEcef);
          vec3 viewDir = normalize(pc.cam_rtc.xyz - inWorldPos);
          // Geodetic latitude of the surface point (exact at alt 0; the
          // geocentric shortcut would shift coastlines by up to ~20 km).
          const float kE2 = 0.00669437999;  // WGS84 first eccentricity²
          float g_lat = atan(worldEcef.z, length(worldEcef.xy) * (1.0 - kE2));
          float g_lon = atan(worldEcef.y, worldEcef.x);
          vec2 wm_uv = vec2(g_lon / 6.28318530718 + 0.5,
                            0.5 - g_lat / 3.14159265359);
          float gmask = texture(waterMaskTex, wm_uv).r;
          float mask = max(gmask, waterMask(rgb));
          // Ocean albedo unification: pull open-ocean pixels toward one
          // canonical deep-sea color. Imagery providers tile the ocean from
          // different passes/sources (near-black vs bright cyan blocks);
          // no shading tweak can hide an albedo step, so normalize the
          // albedo itself. gmask is globally smooth → introduces no seams;
          // its soft coastal edge (and lakes below mask resolution) keep
          // the real imagery.
          rgb = mix(rgb, vec3(0.07, 0.17, 0.27), gmask * 0.6);
          rgb = applyWaterGlint(rgb, up, inWorldPos, viewDir,
                                pc.sun_dir_time.xyz, pc.sun_dir_time.w, mask);
          outColor = vec4(rgb, 1.0);
      }
    )glsl";

      const std::string tile_fs_full = std::string(kWaterGlsl) + tile_fs;
      auto vs_res = compiler.compileGLSLToSPIRV(tile_vs, "main", "vertex");
      auto fs_res = compiler.compileGLSLToSPIRV(tile_fs_full, "main", "fragment");
      if (!vs_res.success || !fs_res.success) {
        spdlog::error("GlobeRenderer: tile shader compile failed: '{}' '{}'", vs_res.errorMessage, fs_res.errorMessage);
        return false;
      }
      ShaderDesc vsd;
      vsd.stage = ShaderStageFlagBits::Vertex;
      vsd.spirvCode = vs_res.spirvCode;
      ShaderDesc fsd;
      fsd.stage = ShaderStageFlagBits::Fragment;
      fsd.spirvCode = fs_res.spirvCode;
      tile_vs_ = device_->createShader(vsd);
      tile_fs_ = device_->createShader(fsd);

      // Descriptor layout: single combined image sampler at binding 0
      DescriptorSetLayoutDesc dl;
      DescriptorSetLayoutBinding b0;
      b0.binding = 0u;
      b0.descriptorCount = 1u;
      b0.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
      b0.descriptorType = DescriptorType::CombinedImageSampler;
      dl.bindings.push_back(b0);
      tile_desc_layout_ = device_->createDescriptorSetLayout(dl);

      // Pipeline layout: TWO descriptor sets (each = one combined image sampler).
      // set 0 = the tile's own imagery — bound from GpuTile::desc_set.
      // set 1 = the coarser-LOD ancestor's imagery, sampled in the fragment
      //         shader for smooth LOD transitions. Reuses the SAME existing
      //         per-tile desc_set on the ancestor — no extra descriptor pool
      //         churn, just bind the ancestor's already-allocated set into
      //         slot 1 of this draw.
      PipelineLayoutDesc tl;
      tl.setLayouts.push_back(tile_desc_layout_);
      tl.setLayouts.push_back(tile_desc_layout_);
      // set 2 = global water mask (equirectangular, bound once per frame).
      // Reuses the same single-sampler layout as the tile/parent sets.
      tl.setLayouts.push_back(tile_desc_layout_);
      PushConstantRange pcr;
      pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                       static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
      pcr.offset = 0u;
      pcr.size = sizeof(TilePushData);
      tl.pushConstantRanges.push_back(pcr);
      tile_pipeline_layout_ = device_->createPipelineLayout(tl);

      GraphicsPipelineDesc pd;
      pd.shaders = {tile_vs_, tile_fs_};
      pd.vertexInput.bindings = {GlobeVertex::getBindingDescription()};
      pd.vertexInput.attributes = GlobeVertex::getAttributeDescriptions();
      pd.primitiveTopology = PrimitiveTopology::TriangleList;
      pd.rasterization.cullMode = CullMode::Back;
      pd.rasterization.frontCounterClockwise = true; // matches GlobeCamera Y-flip
      pd.rasterization.fillMode = FillMode::Solid;
      pd.depthStencil.depthTestEnable = true;
      pd.depthStencil.depthWriteEnable = true;
      pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
      pd.renderPass = render_pass;
      pd.pipelineLayout = tile_pipeline_layout_;
      tile_pipeline_ = device_->createGraphicsPipeline(pd);
      if (!tile_pipeline_) {
        spdlog::error("GlobeRenderer: failed to create tile pipeline");
        return false;
      }
      spdlog::info("GlobeRenderer: tile pipeline ready (per-tile texture sampling)");
    }

    // ------------------------------------------------------------------
    // 4c. Initialize shared index buffer table (16 stitching topologies)
    // ------------------------------------------------------------------
    if (!index_table_.initialize(device_)) {
      spdlog::error("GlobeRenderer: IndexBufferTable initialization failed");
      return false;
    }

    {
      // Shared sampler for all tiles. Complete CPU-generated mip chains plus
      // trilinear/anisotropic filtering keep regional DOM stable when viewed
      // obliquely or heavily minified from the global-to-local transition.
      SamplerDesc sd;
      sd.magFilter = SamplerFilter::Linear;
      sd.minFilter = SamplerFilter::Linear;
      sd.mipmapMode = SamplerMipmapMode::Linear;
      sd.addressModeU = SamplerAddressMode::ClampToEdge;
      sd.addressModeV = SamplerAddressMode::ClampToEdge;
      sd.addressModeW = SamplerAddressMode::ClampToEdge;
      sd.anisotropyEnable = true;
      sd.maxAnisotropy = 16.0f;
      sd.minLod = 0.0f;
      sd.maxLod = 12.0f;
      tile_sampler_ = device_->createSampler(sd);
    }

    // ------------------------------------------------------------------
    // 5. Load default earth textures
    // ------------------------------------------------------------------
    loadDefaultTiles();

    // ------------------------------------------------------------------
    // 6. Space skybox (procedural star field)
    // ------------------------------------------------------------------
    skybox_ = std::make_unique<SkyboxRenderer>();
    if (!skybox_->init(device_, render_pass)) {
      spdlog::warn("GlobeRenderer: skybox init failed, space background disabled");
      skybox_.reset();
    }

    // ------------------------------------------------------------------
    // 7. Atmospheric glow (Fresnel limb effect)
    // ------------------------------------------------------------------
    if (!atmosphere_.init(device_, render_pass)) {
      spdlog::warn("GlobeRenderer: atmosphere init failed, limb glow disabled");
    }

    if (!water_surfaces_.init(device_, render_pass)) {
      spdlog::warn("GlobeRenderer: water surface renderer init failed, local water disabled");
    }

    // ------------------------------------------------------------------
    // 8. Vector overlay renderer
    // ------------------------------------------------------------------
    if (!vector_layers_.init(device_, render_pass)) {
      spdlog::warn("GlobeRenderer: vector layer renderer init failed");
    }

    // ------------------------------------------------------------------
    // 9. Label renderer (billboard markers)
    // ------------------------------------------------------------------
    if (!label_renderer_.init(device_, render_pass)) {
      spdlog::warn("GlobeRenderer: label renderer init failed");
    }

    spdlog::info("GlobeRenderer initialized successfully (earth textures: {})",
                 earth_textures_ready_ ? "ready" : "missing - using procedural colors");
    return true;
  }

  // ============================================================================
  // Mesh upload helper
  // ============================================================================

  void GlobeRenderer::uploadMesh(const EllipsoidMesh::TileMeshData& mesh, BaseGlobeMesh& out_tile) {
    if (mesh.vertices.empty() || mesh.indices.empty())
      return;

    std::vector<GlobeVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
      GlobeVertex gv;
      gv.position = v.position;
      gv.normal = v.normal; // color already baked in
      gv.uv = v.uv;
      vertices.push_back(gv);
    }

    BufferDesc vb_desc;
    vb_desc.size = vertices.size() * sizeof(GlobeVertex);
    vb_desc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    vb_desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    out_tile.vertex_buffer = device_->createBuffer(vb_desc);
    if (out_tile.vertex_buffer) {
      void* ptr = out_tile.vertex_buffer->map();
      std::memcpy(ptr, vertices.data(), vb_desc.size);
      out_tile.vertex_buffer->unmap();
    }

    BufferDesc ib_desc;
    ib_desc.size = mesh.indices.size() * sizeof(u32);
    ib_desc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    ib_desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    out_tile.index_buffer = device_->createBuffer(ib_desc);
    if (out_tile.index_buffer) {
      void* ptr = out_tile.index_buffer->map();
      std::memcpy(ptr, mesh.indices.data(), ib_desc.size);
      out_tile.index_buffer->unmap();
    }

    out_tile.index_count = static_cast<u32>(mesh.indices.size());
  }

  // ============================================================================
  // Update
  // ============================================================================

  void GlobeRenderer::update(f32 dt, const GlobeCamera& camera) {
    // Bump frame counter at the start so every touch()/upload uses the new frame
    // and end-of-frame eviction can compute the correct "used this frame" count.
    ++frame_counter_;

    // 1. Regenerate base globe and atmosphere meshes if RTC origin changed.
    const auto& rtc_origin = camera.getRTCOrigin();
    if (cached_rtc_origin_ != rtc_origin.getOriginECEF()) {
      auto base_mesh = EllipsoidMesh::generateFullGlobe(coordinate::WGS84, rtc_origin);
      uploadMesh(base_mesh, base_globe_);
      cached_rtc_origin_ = rtc_origin.getOriginECEF();
      // Tile meshes have the old RTC origin baked into their float32 vertex
      // positions. Regenerate ONLY the vertex/index buffers (cheap) and keep
      // the GPU texture + descriptor set (expensive). This way camera motion
      // doesn't wipe out loaded imagery — the visible tiles stay on screen
      // through every RTC recenter.
      for (auto& tile : gpu_cache_.tiles()) {
        if (!tile.texture)
          continue; // partial upload, skip
        regenerateTileMesh(tile.coord, tile, camera);
      }
    }

    atmosphere_.update(camera);
    water_surfaces_.update(camera);
    vector_layers_.update(camera);
    label_renderer_.update(camera);

    // Animate sun direction: slow orbit around Earth's polar axis. `elapsed_time_`
    // keeps advancing even when the sun is pinned — the water ripple animation
    // reads it independently of the sun position.
    elapsed_time_ += dt;
    if (sun_animated_) {
      constexpr float kSunOrbitSpeed = 0.02f; // radians/second (one day ~314 sec)
      float sun_lon = elapsed_time_ * kSunOrbitSpeed;
      constexpr float kSunDeclination = 0.408f; // ~23.4° in radians
      sun_dir_ecef_ = glm::normalize(glm::vec3(std::cos(kSunDeclination) * std::cos(sun_lon),
                                               std::cos(kSunDeclination) * std::sin(sun_lon),
                                               std::sin(kSunDeclination)));
    }

    if (!tile_rendering_enabled_) {
      return;
    }

    // 2. Pump network I/O
    download_queue_.update();
    http_client_.update();

    // 3. Update quadtree to determine visible tiles
    glm::dvec3 camera_ecef = camera.getPositionECEF();
    glm::dmat4 view = camera.getViewMatrix();
    f32 aspect = screen_width_ / screen_height_;
    glm::mat4 proj = camera.getProjectionMatrix(aspect);

    // The selector calls this for every tile it considers — including BRAND
    // NEW fine tiles that have no geom_cache entry yet. We can't rely on
    // geom_cache's baked min/max here. Instead sample the deepest LOADED DEM
    // tile directly: walk up the DEM ancestor chain, then min/max over a
    // sparse grid of samples covering this leaf's footprint in DEM UV space.
    // Without this, near-camera tiles at oblique low-altitude views get a
    // sea-level OBB, the frustum cull rejects them, the selector never
    // subdivides, and the foreground renders as base globe only.
    gis::TileQuadTreeSelector::HeightQuery hq = [this](const gis::TileCoord& c, f64& min_h, f64& max_h) {
      min_h = 0.0;
      max_h = 0.0;

      // Walk up to find a loaded DEM tile.
      gis::TileCoord dc = c;
      const gis::DemTile* dem = nullptr;
      while (true) {
        auto it = dem_tiles_.find(dc);
        if (it != dem_tiles_.end() && !it->second.empty()) {
          dem = &it->second;
          break;
        }
        if (dc.z == 0)
          break;
        dc = dc.parent();
      }
      if (!dem)
        return;

      // UV mapping from this leaf into the ancestor DEM's UV space.
      const i32 dz = c.z - dc.z;
      const f32 div = static_cast<f32>(1 << dz);
      const f32 inv = 1.0f / div;
      const i32 ox = c.x - (dc.x << dz);
      const i32 oy = c.y - (dc.y << dz);
      const f32 ou = static_cast<f32>(ox) * inv;
      const f32 ov = static_cast<f32>(oy) * inv;

      // 8x8 sparse grid is fast and tight enough for an OBB bound.
      f32 hmin = std::numeric_limits<f32>::infinity();
      f32 hmax = -std::numeric_limits<f32>::infinity();
      constexpr i32 kN = 8;
      for (i32 row = 0; row <= kN; ++row) {
        const f32 leaf_v = static_cast<f32>(row) / static_cast<f32>(kN);
        for (i32 col = 0; col <= kN; ++col) {
          const f32 leaf_u = static_cast<f32>(col) / static_cast<f32>(kN);
          const f32 h = dem->sample(leaf_u * inv + ou, leaf_v * inv + ov);
          if (h < hmin)
            hmin = h;
          if (h > hmax)
            hmax = h;
        }
      }
      min_h = static_cast<f64>(hmin);
      max_h = static_cast<f64>(hmax);
    };
    auto selected = quad_tree_.select(camera_ecef, view, glm::dmat4(proj), screen_height_, &hq);

    visible_tiles_.clear();
    visible_tiles_.reserve(selected.size() + 64); // +64 for permanent z=3 base
    leaf_info_.clear();
    leaf_info_.reserve(selected.size());
    for (const auto& info : selected) {
      visible_tiles_.push_back(info.coord);
      leaf_info_[info.coord] = info;
    }

    // 3a. Pre-request intermediate-LOD "anchor" ancestors for every leaf.
    // The selector only emits leaves, so on a close-up view that picks z=13
    // for the whole frame, levels z=4..z=12 are never downloaded. When the
    // shader discards no-data fragments inside a z=13 tile, walk-up has
    // nothing between the leaf and z=3 to render — the gap shows as a blurry
    // hemispheric texture.
    // Adding anchors every 3 LOD levels gives the discard fallback a
    // medium-resolution image to reveal instead. The set is heavily deduped
    // (one z=10 ancestor covers 64 z=13 leaves), so the extra request count
    // is small (~10-30 anchors per frame).
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> anchor_seen;
    for (const auto& info : selected) {
      gis::TileCoord c = info.coord;
      while (c.z > 3) {
        c = c.parent();
        const bool source_available = !imagery_layers_.empty() && imagery_layers_[0]->supportsTile(c);
        if (c.z % 3 != 0 && !source_available)
          continue; // cadence anchors plus every explicitly available sparse source
        if (anchor_seen.insert(c).second) {
          visible_tiles_.push_back(c);
        }
      }
    }

    // 3b. ALWAYS ensure the z=3 base layer is in the visible set — these 64
    // tiles are permanent backdrop coverage so unloaded high-zoom areas show
    // a blurry low-detail tile instead of the procedural base globe with
    // visible cloud + Lambertian artifacts. ~16 MB of textures = negligible.
    for (i32 x = 0; x < 8; ++x) {
      for (i32 y = 0; y < 8; ++y) {
        gis::TileCoord c{3, x, y};
        // Skip if the selector already chose this z=3 tile as a leaf — pushing
        // again would double-execute buildDrawingSet for it every frame.
        if (leaf_info_.find(c) == leaf_info_.end()) {
          visible_tiles_.push_back(c);
        }
      }
    }

    // 4. Build set of currently visible coords for fast lookup
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> visible_set;
    visible_set.reserve(visible_tiles_.size());
    for (const auto& coord : visible_tiles_) {
      visible_set.insert(coord);
    }

    // 5. Touch geometry and GPU caches for every leaf and its ancestors so they
    // survive LRU eviction. The ancestor walk keeps parent height grids alive for
    // ancestor-fallback rendering and DEM bake reuse. Stamping gpu_cache_ here
    // also replaces the O(N²) ancestor sweep that was previously in Step 8.
    for (const auto& info : selected) {
      geom_cache_.touch(info.coord, frame_counter_);
      if (auto* gp = gpu_cache_.find(info.coord))
        gp->frame = frame_counter_;
      gis::TileCoord a = info.coord;
      while (a.z > 0) {
        a = a.parent();
        geom_cache_.touch(a, frame_counter_);
        if (auto* gp = gpu_cache_.find(a))
          gp->frame = frame_counter_;
      }
    }

    // 6. For each visible tile: ensure GPU data exists or begin loading.
    // Cap uploads per frame to avoid stalling the render thread — many
    // decoded tiles arriving at once cause GPU upload bursts visible as
    // flicker. ~8 tiles/frame at 60 fps = 480 tiles/sec, fast enough to keep
    // up with typical network throughput without bursting.
    constexpr int kMaxUploadsPerFrame = 8;
    int uploads_this_frame = 0;
    for (const auto& coord : visible_tiles_) {
      // If we already have it on GPU, drop any duplicate decode and move on —
      // re-uploading would destroy the existing texture/descriptor set for one
      // frame, which is visible as flicker.
      auto* gpu_it = gpu_cache_.find(coord);
      if (gpu_it && gpu_it->owns_real_texture) {
        decoded_tiles_.erase(coord);
        continue;
      }

      auto decoded = decoded_tiles_.find(coord);
      if (decoded != decoded_tiles_.end()) {
        if (uploads_this_frame < kMaxUploadsPerFrame) {
          uploadTile(coord, decoded->second, camera);
          decoded_tiles_.erase(decoded);
          ++uploads_this_frame;
        }
        continue; // either uploaded or deferred to next frame
      }

      // Check tile cache
      auto cached = tile_cache_.get(coord);
      if (cached.has_value()) {
        if (uploads_this_frame < kMaxUploadsPerFrame) {
          uploadTile(coord, cached.value(), camera);
          ++uploads_this_frame;
        }
      } else if (loading_tiles_.count(coord) == 0 && unavailable_tiles_.count(coord) == 0) {
        requestTile(coord);
      }
    }

    // 6. Fetch DEM tiles for visible imagery tiles
    if (dem_provider_) {
      for (const auto& coord : visible_tiles_) {
        // Clamp to DEM provider's max level (terrain tiles are coarser than imagery)
        gis::TileCoord dem_coord = coord;
        if (dem_coord.z > dem_provider_->getMaxLevel()) {
          // Walk up the quad tree to the max DEM level
          i32 diff = dem_coord.z - dem_provider_->getMaxLevel();
          for (i32 d = 0; d < diff; ++d) {
            dem_coord = dem_coord.parent();
          }
        }

        if (!dem_tiles_.count(dem_coord) && !dem_loading_.count(dem_coord)) {
          dem_loading_.insert(dem_coord);
          std::string url = dem_provider_->buildTileUrl(dem_coord);
          download_queue_.enqueue(url,
                                  core::net::DownloadQueue::Priority::Low,
                                  [this, dem_coord](core::net::HttpClient::Response response) {
                                    dem_loading_.erase(dem_coord);
                                    if (!response.success() || response.body.empty() || !dem_provider_)
                                      return;
                                    gis::DemTile tile = dem_provider_->decode(response.body);
                                    if (!tile.empty()) {
                                      dem_tiles_[dem_coord] = std::move(tile);
                                      // Invalidate any previously-baked geometry whose footprint
                                      // is covered by this new DEM. Without this, a leaf that
                                      // baked from a coarser ancestor DEM keeps its stale heights
                                      // forever — adjacent leaves that baked at different times
                                      // pick different DEM sources and the shared-edge vertex
                                      // elevations don't match, producing visible cracks.
                                      // Walk every geom entry and check whether dem_coord is on
                                      // its ancestor chain (or equals it); if yes, the new DEM
                                      // is at least as deep as whatever it baked from, so re-bake.
                                      for (auto& gt : geom_cache_.tiles()) {
                                        if (gt.coord.z < dem_coord.z)
                                          continue; // coarser, can't be a descendant
                                        gis::TileCoord a = gt.coord;
                                        while (a.z > dem_coord.z)
                                          a = a.parent();
                                        if (a == dem_coord)
                                          gt.dem_baked = false;
                                      }
                                    }
                                  });
        }
      }
    }

    // 6a. Bake DEM into geometry for every tile that has a geom_cache entry —
    // not just selector leaves. Ancestors picked up by buildDrawingSet's
    // walk-up are RENDERED but were never `selected`; if we skipped them
    // here their heights stayed at zero (ellipsoid surface), so they'd
    // render as a sea-level shell underneath the actual terrain.
    //
    // bakeDemIntoGeometry walks the tile's parent chain to find a loaded
    // DEM and bilinearly samples it; whenever a finer DEM later arrives
    // the download callback above clears dem_baked for any tile under it,
    // forcing a re-bake here. This is what keeps adjacent tiles
    // consistent at their shared edges: once the network has caught up,
    // every tile in the chain ends up baking from the same deepest DEM.
    auto dem_for = [this](const gis::TileCoord& c) -> const gis::DemTile* {
      auto it = dem_tiles_.find(c);
      return it == dem_tiles_.end() ? nullptr : &it->second;
    };

    // Compute the deepest DEM level that's loaded for EVERY selector leaf
    // in the visible area. Every tile then bakes from that single level —
    // adjacent tiles sample the same DEM source (or sibling DEM tiles with
    // edge-consistent values by Terrarium spec), so the rendered surface
    // joins without elevation cracks. Without this constraint, two
    // neighbors could pick different deepest DEMs depending on what was
    // loaded at bake time, producing the vertical "wall" gaps the user
    // reported at oblique low-altitude views.
    i32 uniform_dem_level = -1;
    if (dem_provider_ && !selected.empty()) {
      const i32 max_dem = dem_provider_->getMaxLevel();
      for (i32 L = max_dem; L >= 0; --L) {
        bool all_have = true;
        for (const auto& info : selected) {
          gis::TileCoord anc = info.coord;
          if (anc.z < L)
            continue; // leaf shallower than L — its own DEM (or coarser) suffices
          while (anc.z > L)
            anc = anc.parent();
          if (!dem_tiles_.count(anc)) {
            all_have = false;
            break;
          }
        }
        if (all_have) {
          uniform_dem_level = L;
          break;
        }
      }
    }
    // If the uniform level changed since last frame, invalidate every
    // existing bake so the next pass produces consistent heights.
    if (uniform_dem_level != last_uniform_dem_level_) {
      for (auto& gt : geom_cache_.tiles())
        gt.dem_baked = false;
      last_uniform_dem_level_ = uniform_dem_level;
    }

    for (auto& gt : geom_cache_.tiles()) {
      if (gt.dem_baked)
        continue;
      if (bakeDemIntoGeometry(gt, dem_for, uniform_dem_level)) {
        gt.dem_baked = true;
        // Force VBO rebuild — render() skips tiles with !vertex_buffer, and
        // regenerateTileMesh (called below) recreates from the latest heights.
        if (auto* gp = gpu_cache_.find(gt.coord)) {
          gp->vertex_buffer.reset();
        }
      }
    }

    // 6b. Rebuild any tile whose VBO was invalidated (DEM bake above, or any
    // future code path that resets vertex_buffer while preserving texture).
    // regenerateTileMesh reads the latest heights from geom_cache_ and clears
    // dirty_height — closing the bake→render loop within a single frame.
    for (auto& tile : gpu_cache_.tiles()) {
      if (!tile.texture)
        continue; // not a real-textured tile
      if (tile.vertex_buffer)
        continue; // already has geometry
      regenerateTileMesh(tile.coord, tile, camera);
    }

    // 7. Cancel pending downloads for tiles no longer in the visible set.
    // Without this, fast camera motion queued thousands of requests that
    // Tianditu drip-feeds for ~30 minutes long after the user moved on,
    // and the pend: counter climbed to 4000+ while download bandwidth was
    // wasted on tiles the user no longer cares about.
    if (!imagery_layers_.empty()) {
      for (auto it = loading_tiles_.begin(); it != loading_tiles_.end();) {
        if (visible_set.count(*it) == 0) {
          download_queue_.cancel(imagery_layers_[0]->buildTileUrl(*it));
          it = loading_tiles_.erase(it);
        } else {
          ++it;
        }
      }
    }

    // 8. Mark directly-visible tile slots as touched-this-frame for LRU
    // bookkeeping. This covers z=3 backdrop tiles inserted in step 3a that are
    // not in `selected` (and therefore not already stamped by step 5's ancestor
    // walk). The O(N²) ancestor sweep previously here is replaced by step 5.
    for (const auto& coord : visible_tiles_) {
      if (auto* gp = gpu_cache_.find(coord))
        gp->frame = frame_counter_;
    }

    // 9. Evict stale entries from tile cache
    tile_cache_.evict();

    // 10. Build the per-frame drawing set: walk each selector leaf up to its
    // deepest loaded ancestor and dedup. The z=3 base layer is intentionally
    // NOT passed in here — it's already in visible_tiles_ (for download/LRU
    // touch) but if we fed it to the walk-up, every z=3 tile that's loaded
    // would self-add to the drawing set on top of the deeper-LOD leaves'
    // ancestors, producing redundant overlap. Walk-up naturally bottoms out at
    // z=3 (or higher) when finer ancestors aren't loaded, so the base layer
    // appears in the drawing set ONLY where it's actually needed as backstop.
    std::vector<gis::TileCoord> leaves_only;
    leaves_only.reserve(selected.size());
    for (const auto& info : selected)
      leaves_only.push_back(info.coord);
    buildDrawingSet(leaves_only);

    // 11. End-of-frame: peak×ratio LRU eviction for both caches.
    geom_cache_.endFrameWithEviction(frame_counter_);
    gpu_cache_.endFrameWithEviction(frame_counter_);
  }

  // ============================================================================
  // Render
  // ============================================================================

  void GlobeRenderer::render(RHICommandBufferPtr cmd, const GlobeCamera& camera) {
    if (!pipeline_ || !pipeline_layout_)
      return;

    f32 aspect = screen_width_ / screen_height_;
    glm::mat4 proj = camera.getProjectionMatrix(aspect);
    glm::mat4 view = camera.getRTCViewMatrix();

    // 0. Space skybox — renders first, stays behind all geometry (depth 1.0)
    if (environment_rendering_enabled_ && skybox_) {
      // Remove translation from view matrix so skybox rotates with camera but doesn't translate
      glm::mat4 view_rot = glm::mat4(glm::mat3(view));
      skybox_->render(cmd, view_rot, proj, glm::vec3(camera.getPositionECEF()), sun_dir_ecef_);
    }

    // Cloud fade: full clouds from space, none when close to surface.
    // sun_dir.w carries the cloud opacity [0, 1] to the fragment shader.
    double cam_alt = camera.getAltitude();
    constexpr double kCloudFull = 3'000'000.0; // 3000 km — full clouds
    constexpr double kCloudNone = 300'000.0;   //  300 km — no clouds
    double ct = std::clamp((cam_alt - kCloudNone) / (kCloudFull - kCloudNone), 0.0, 1.0);
    float cloud_fade = static_cast<float>(ct * ct * (3.0 - 2.0 * ct)); // smoothstep

    GlobePushData push_data;
    push_data.mvp = proj * view;
    push_data.sun_dir = glm::vec4(sun_dir_ecef_, cloud_fade);
    // Camera in RTC + time, for the base-globe water shading (M2.5).
    push_data.cam_rtc =
        glm::vec4(glm::vec3(camera.getPositionECEF() - camera.getRTCOrigin().getOriginECEF()), elapsed_time_);
    push_data.use_texture = 0u;

    const ShaderStageFlags vs_fs = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;

    cmd->bindPipeline(pipeline_);

    // Bind earth descriptor set (stays bound; tile overlays ignore it via use_texture=0)
    if (earth_desc_set_) {
      cmd->bindDescriptorSet(0u, earth_desc_set_, pipeline_layout_, PipelineBindPoint::Graphics);
    }

    // 1. Base globe – sample from GPU earth textures when available.
    //    REQUIRES earth_desc_set_ to be bound: the base-globe pipeline layout
    //    declares set=0 (west/east/cloud samplers). Issuing the draw with an
    //    unbound descriptor set is undefined and GPU-faults on MoltenVK — which
    //    happened whenever the default `texture/earth` assets were absent. Guard
    //    the draw on the descriptor set so a missing-asset scenario renders empty
    //    space instead of crashing. (earth_demo/editor always have it → unchanged.)
    if (earth_desc_set_ && base_globe_.vertex_buffer && base_globe_.index_buffer && base_globe_.index_count > 0) {
      push_data.use_texture = earth_textures_ready_ ? 1u : 0u;
      cmd->pushConstants(pipeline_layout_, vs_fs, 0, sizeof(push_data), &push_data);
      cmd->bindVertexBuffer(0, base_globe_.vertex_buffer);
      cmd->bindIndexBuffer(base_globe_.index_buffer);
      cmd->drawIndexed(base_globe_.index_count);
    }

    if (!tile_rendering_enabled_) {
      // 2. Local water bodies — alpha-blend over the base globe
      water_surfaces_.render(cmd, camera, aspect, sun_dir_ecef_, elapsed_time_);
      // 3. Vector overlays — on top of globe, under atmosphere
      vector_layers_.render(cmd, camera, aspect);
      // 4. Labels — billboard markers
      label_renderer_.render(cmd, camera, aspect);
      // 5. Atmospheric glow — renders after globe, additive blend
      if (environment_rendering_enabled_)
        atmosphere_.render(cmd, camera, aspect);
      return;
    }

    // 2. Tile overlays — single opaque pass with TEXTURE-LEVEL parent blend.
    //
    // For each fine tile we pick a "parent" — the deepest loaded ancestor
    // OTHER than self — and bind its texture to descriptor set 1. The
    // fragment shader samples both at the matching world position (parent
    // UV computed via a per-tile offset+scale push constant) and blends
    // from self to parent on edges that face a coarser-LOD neighbor in
    // the drawing set. Blending in TEXTURE space (not via a separate
    // geometric layer) avoids the ghost double-surface that the previous
    // alpha-blend attempt produced at low altitude.
    if (tile_pipeline_) {
      cmd->bindPipeline(tile_pipeline_);
      glm::mat4 tile_mvp = proj * view;

      // Global water mask — set 2, same for every tile; bind once.
      if (water_mask_desc_set_) {
        cmd->bindDescriptorSet(2u, water_mask_desc_set_, tile_pipeline_layout_, PipelineBindPoint::Graphics);
      }

      // Frame-constant water-shading inputs (same for every tile this frame).
      const glm::dvec3 rtc_origin_ecef = camera.getRTCOrigin().getOriginECEF();
      const glm::vec3 tile_cam_rtc = glm::vec3(camera.getPositionECEF() - rtc_origin_ecef);
      const glm::vec4 tile_sun_dir_time = glm::vec4(sun_dir_ecef_, elapsed_time_);
      const glm::vec4 tile_rtc_origin = glm::vec4(glm::vec3(rtc_origin_ecef), 0.0f);

      std::vector<gis::TileCoord> ordered = drawing_set_;
      std::sort(
          ordered.begin(), ordered.end(), [](const gis::TileCoord& a, const gis::TileCoord& b) { return a.z < b.z; });

      // Find a coarser-LOD parent texture for `coord`. Walks up the chain
      // skipping low-quality and missing tiles. Returns nullptr (no parent
      // available) when the chain bottoms out, in which case the shader's
      // parent sampler will be aliased to the tile's own texture and the
      // blend collapses to a no-op.
      auto findParentTile = [this](const gis::TileCoord& coord) -> const GpuTile* {
        gis::TileCoord c = coord;
        while (c.z > 0) {
          c = c.parent();
          const GpuTile* p = gpu_cache_.find(c);
          if (p && p->owns_real_texture && p->desc_set)
            return p;
        }
        return nullptr;
      };

      for (const auto& coord : ordered) {
        const GpuTile* tile = gpu_cache_.find(coord);
        if (!tile)
          continue;
        if (!tile->vertex_buffer || !tile->index_buffer || tile->index_count == 0)
          continue;
        if (!tile->desc_set)
          continue;

        const GpuTile* parent = findParentTile(coord);
        // No parent available → use self in slot 1, blend collapses to a no-op.
        const auto& parent_desc = (parent ? parent->desc_set : tile->desc_set);

        cmd->bindDescriptorSet(0u, tile->desc_set, tile_pipeline_layout_, PipelineBindPoint::Graphics);
        cmd->bindDescriptorSet(1u, parent_desc, tile_pipeline_layout_, PipelineBindPoint::Graphics);

        TilePushData pd;
        pd.vp = tile_mvp;
        pd.sun_dir_time = tile_sun_dir_time;
        pd.cam_rtc = glm::vec4(tile_cam_rtc, static_cast<f32>(camera.getAltitude()));
        pd.rtc_origin = tile_rtc_origin;
        pd.rtc_origin.w = parent ? static_cast<f32>(coord.z) : 0.0f;
        pd.uv_offset_scale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

        // Parent UV remap: this tile's coord at z=N → parent coord at z=P
        // (where P <= N-1). Within parent, this tile occupies a sub-rectangle
        // of size (1/2^(N-P)) at offset ((x mod 2^(N-P)) / 2^(N-P), ...).
        glm::vec4 parent_uv(0.0f, 0.0f, 1.0f, 1.0f);
        if (parent) {
          const i32 dz = coord.z - parent->coord.z;
          const f32 div = static_cast<f32>(1 << dz);
          const f32 inv = 1.0f / div;
          const i32 ox = coord.x - (parent->coord.x << dz);
          const i32 oy = coord.y - (parent->coord.y << dz);
          parent_uv = glm::vec4(static_cast<f32>(ox) * inv, static_cast<f32>(oy) * inv, inv, inv);
        }
        pd.parent_uv_offset_scale = parent_uv;

        // Edge fade: 1.0 where the in-set neighbor is one LOD coarser OR
        // where the imagery provider changes across a same-LOD edge. Routed
        // source boundaries then feather through the already-bound parent
        // texture on both sides instead of exposing a rectangular color seam.
        pd.edge_fade = glm::vec4(0.0f);
        if (auto it = drawing_set_info_.find(coord); it != drawing_set_info_.end()) {
          const auto& s = it->second;
          const u8 source_edges = tile->source_transition_edges;
          pd.edge_fade =
              glm::vec4(s.left_scale == 2 || (source_edges & gis::TileSourceTransitionLeft) ? 1.0f : 0.0f,
                        s.right_scale == 2 || (source_edges & gis::TileSourceTransitionRight) ? 1.0f : 0.0f,
                        s.top_scale == 2 || (source_edges & gis::TileSourceTransitionTop) ? 1.0f : 0.0f,
                        s.bottom_scale == 2 || (source_edges & gis::TileSourceTransitionBottom) ? 1.0f : 0.0f);
        }

        cmd->pushConstants(tile_pipeline_layout_,
                           static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                               static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
                           0,
                           sizeof(TilePushData),
                           &pd);
        cmd->bindVertexBuffer(0, tile->vertex_buffer);
        cmd->bindIndexBuffer(tile->index_buffer);
        cmd->drawIndexed(tile->index_count);
      }
    }

    // 3. Local water bodies — alpha-blend over the opaque tiles, depth-tested
    water_surfaces_.render(cmd, camera, aspect, sun_dir_ecef_, elapsed_time_);

    // 4. Vector overlays
    vector_layers_.render(cmd, camera, aspect);

    // 5. Labels — billboard markers
    label_renderer_.render(cmd, camera, aspect);

    // 6. Atmospheric glow — renders last, additive blend
    if (environment_rendering_enabled_)
      atmosphere_.render(cmd, camera, aspect);
  }

  // ============================================================================
  // Tile request / upload
  // ============================================================================

  void GlobeRenderer::requestTile(const gis::TileCoord& coord, i32 attempt) {
    if (imagery_layers_.empty()) {
      return;
    }

    std::string url = imagery_layers_[0]->buildTileUrl(coord);
    if (url.empty()) {
      spdlog::debug("GlobeRenderer: imagery provider '{}' has no tile {}",
                    imagery_layers_[0]->getName(),
                    coord.toString());
      unavailable_tiles_.insert(coord);
      return;
    }
    loading_tiles_.insert(coord);
    // No fallback upload — the base globe shows through until real OSM data
    // arrives, avoiding distracting procedural-color dots over a satellite map.

    download_queue_.enqueue(
        url,
        core::net::DownloadQueue::Priority::Normal,
        [this, coord, attempt](core::net::HttpClient::Response response) {
          loading_tiles_.erase(coord);

          if (!response.success() || response.body.empty()) {
            if (attempt < 2) {
              spdlog::debug("GlobeRenderer: retrying tile {} (attempt={}, http={}, error='{}', size={})",
                            coord.toString(),
                            attempt + 1,
                            response.status_code,
                            response.error,
                            response.body.size());
              requestTile(coord, attempt + 1);
              return;
            }

            spdlog::warn("GlobeRenderer: failed to download tile {} after {} attempts "
                         "(http={}, error='{}', size={})",
                         coord.toString(),
                         attempt + 1,
                         response.status_code,
                         response.error,
                         response.body.size());
            // Mark as permanently loading to prevent infinite re-request loop.
            loading_tiles_.insert(coord);
            return;
          }

          // Decode image with stb_image
          int w = 0, h = 0, channels = 0;
          unsigned char* pixels = stbi_load_from_memory(
              response.body.data(), static_cast<int>(response.body.size()), &w, &h, &channels, 4); // force RGBA
          if (!pixels) {
            spdlog::warn("GlobeRenderer: failed to decode tile image {}", coord.toString());
            loading_tiles_.insert(coord); // keep marked as loading; do not retry
            return;
          }

          gis::TileData tile_data;
          tile_data.width = w;
          tile_data.height = h;
          tile_data.image_data.assign(pixels, pixels + w * h * 4);
          stbi_image_free(pixels);
          // No-data handling is now done in the fragment shader (`discard`
          // when sample is near-zero) — see tile_fs in initialize(). Dropping
          // mostly-black tiles here was tempting but causes a worse failure
          // when no intermediate-LOD ancestor is loaded: walk-up cliffs all
          // the way to z=3 base and leaves uniform blurry coverage.

          // Stash the decoded image in decoded_tiles_ but DO NOT touch
          // gpu_cache_. Erasing here creates a one-frame gap before the next
          // update() loop re-uploads, which the user sees as a black flash on
          // every download arrival. The update loop already skips re-upload
          // when gpu_cache_ already has the coord (see step 4), so a duplicate
          // download just gets discarded — no flicker.
          decoded_tiles_[coord] = tile_data;
          tile_cache_.put(coord, std::move(tile_data));
        });
  }

  void GlobeRenderer::buildDrawingSet(const std::vector<gis::TileCoord>& leaves) {
    // Deferred tile replacement with restricted-quadtree balancing.
    //
    // Step 1 — walk-up: each leaf is replaced with its deepest LOADED ancestor.
    //          If no ancestor is loaded down to z=0, the leaf is dropped entirely
    //          (the base globe renders behind everything; better to show that
    //          than to leave an empty drawing-set slot which masks the base).
    // Step 2 — balance: any drawing-set member whose neighbor is two-or-more LOD
    //          levels coarser is demoted (replaced with its own coarser ancestor)
    //          until no neighbor pair differs by more than 1. The 16-topology
    //          IBO table can only stitch a 1-level jump, so without this the
    //          rendered mesh leaks vertical/horizontal hairline cracks along
    //          every >=2-level boundary.
    // Step 3 — edge scales + per-tile bookkeeping for the rendered set.

    drawing_set_.clear();
    drawing_set_.reserve(leaves.size());
    drawing_set_info_.clear();
    drawing_set_info_.reserve(leaves.size());

    auto isLoaded = [this](const gis::TileCoord& c) -> bool {
      const GpuTile* s = gpu_cache_.find(c);
      return s && s->owns_real_texture;
    };
    auto isHighQuality = [this](const gis::TileCoord& c) -> bool {
      const GpuTile* s = gpu_cache_.find(c);
      return s && s->owns_real_texture && !s->low_quality;
    };

    // Step 1 — walk-up to pick the rendered LOD per leaf.
    // First pass prefers a HIGH-QUALITY ancestor (loaded AND not flagged
    // as Tianditu no-data); if every ancestor in the chain is low-quality
    // or unloaded, fall back to any loaded ancestor (still better than a
    // gap). If even that fails, drop the leaf so base globe shows through.
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> drawing_set_lookup;
    drawing_set_lookup.reserve(leaves.size());
    for (const auto& leaf : leaves) {
      gis::TileCoord c = leaf;
      while (!isHighQuality(c)) {
        if (c.z == 0)
          break;
        c = c.parent();
      }
      if (!isHighQuality(c)) {
        // No high-quality ancestor found — accept the deepest loaded one
        // (any quality). Walk fresh from the leaf to keep finer detail.
        c = leaf;
        while (!isLoaded(c)) {
          if (c.z == 0)
            break;
          c = c.parent();
        }
        if (!isLoaded(c))
          continue; // dead chain — let base globe through
      }
      if (drawing_set_lookup.insert(c).second) {
        drawing_set_.push_back(c);
      }
    }

    // neighborLOD walks the same-z neighbor coord upward through the drawing set
    // to find the LOD on that side. If no ancestor is in the set, the neighbor
    // is either finer or absent — return self.z to indicate "no demotion needed".
    auto neighborLOD = [&drawing_set_lookup](const gis::TileCoord& self, i32 dx, i32 dy) -> i32 {
      const i32 max_xy = 1 << self.z;
      const i32 nx = self.x + dx;
      const i32 ny = self.y + dy;
      if (nx < 0 || nx >= max_xy || ny < 0 || ny >= max_xy)
        return self.z;
      gis::TileCoord cand{self.z, nx, ny};
      while (true) {
        if (drawing_set_lookup.count(cand))
          return cand.z;
        if (cand.z == 0)
          break;
        cand = cand.parent();
      }
      return self.z;
    };

    // Step 2 — restricted-quadtree balancing. Demote any member whose coarsest
    // neighbor is two-or-more levels below it. Demoted member must replace with
    // a LOADED ancestor; if no loaded ancestor exists in the demotion window,
    // walk up to the deepest loaded coord we can reach (worst case z=0 fallback,
    // which gets dropped above — but that branch is taken before step 2).
    bool changed = true;
    int safety = 0;
    while (changed && safety++ < 16) {
      changed = false;
      for (size_t i = 0; i < drawing_set_.size(); ++i) {
        gis::TileCoord T = drawing_set_[i];
        i32 min_nlod = T.z;
        for (auto dxdy : std::array<std::pair<i32, i32>, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}}) {
          const i32 nlod = neighborLOD(T, dxdy.first, dxdy.second);
          if (nlod < min_nlod)
            min_nlod = nlod;
        }
        if (T.z - min_nlod < 2)
          continue; // within 1-level tolerance — keep

        // Demote T to a loaded ancestor at z <= min_nlod + 1. Walk up until we
        // hit a loaded coord at the demotion target or coarser; if we exhaust
        // the chain, fall back to whatever's loaded along the way.
        const i32 target_z = min_nlod + 1;
        gis::TileCoord cand = T;
        while (cand.z > target_z)
          cand = cand.parent();
        // cand is now at target_z. If not loaded, walk further up to deepest
        // loaded ancestor (the original walk-up guarantee).
        while (!isLoaded(cand) && cand.z > 0)
          cand = cand.parent();
        if (!isLoaded(cand)) {
          // Demotion exhausted — drop T from the set.
          drawing_set_lookup.erase(T);
          drawing_set_.erase(drawing_set_.begin() + i);
          --i;
          changed = true;
          continue;
        }
        if (cand == T)
          continue; // already at target
        // Replace T → cand. Dedup against the existing lookup.
        drawing_set_lookup.erase(T);
        if (drawing_set_lookup.insert(cand).second) {
          drawing_set_[i] = cand;
        } else {
          drawing_set_.erase(drawing_set_.begin() + i);
          --i;
        }
        changed = true;
      }
    }

    // Step 3 — per-frame bookkeeping for the rendered set: edge_scales, identity
    // UV, this-frame LRU stamp, and IBO topology refresh.
    for (const auto& c : drawing_set_) {
      gis::SelectedTile s;
      s.coord = c;
      drawing_set_info_[c] = s;
    }
    for (const auto& c : drawing_set_) {
      gis::SelectedTile& s = drawing_set_info_[c];
      s.left_scale = neighborLOD(c, -1, 0) < c.z ? 2 : 1;
      s.right_scale = neighborLOD(c, +1, 0) < c.z ? 2 : 1;
      s.top_scale = neighborLOD(c, 0, -1) < c.z ? 2 : 1;
      s.bottom_scale = neighborLOD(c, 0, +1) < c.z ? 2 : 1;
    }

    for (const auto& c : drawing_set_) {
      GpuTile* slot = gpu_cache_.find(c);
      if (!slot)
        continue;
      slot->render_texture_source = c;
      slot->uv_offset_scale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
      slot->frame = frame_counter_;
      refreshTileIBO(*slot);
    }
  }

  void GlobeRenderer::regenerateTileMesh(const gis::TileCoord& coord, GpuTile& tile, const GlobeCamera& camera) {
    const auto& rtc_origin = camera.getRTCOrigin();

    i32 ls = 1, rs = 1, ts = 1, bs = 1;
    {
      auto it = leaf_info_.find(coord);
      if (it != leaf_info_.end()) {
        ls = it->second.left_scale;
        rs = it->second.right_scale;
        ts = it->second.top_scale;
        bs = it->second.bottom_scale;
      }
    }

    // Pull DEM-baked heights from the geometry cache when available. The two
    // constants `gis::kTileGeometryVertexCount` and `renderer::kTileVertexCount`
    // are both 1089 (33×33) — sizes match so the array reinterpret is sound.
    static_assert(gis::kTileGeometryVertexCount == kTileVertexCount, "tile vertex grid sizes must match");
    const std::array<f32, kTileVertexCount>* heights = nullptr;
    gis::GeometryTile* geom = geom_cache_.find(coord);
    if (geom && geom->dirty_height) {
      heights = reinterpret_cast<const std::array<f32, kTileVertexCount>*>(&geom->heights);
    }
    std::array<EllipsoidMesh::Vertex, kTileVertexCount> verts;
    buildTileVertices(coord, coordinate::WGS84, rtc_origin, heights, verts);
    if (geom && heights)
      geom->dirty_height = false;

    const size_t vb_size = verts.size() * sizeof(GlobeVertex);
    if (!tile.vertex_buffer) {
      BufferDesc vb_desc;
      vb_desc.size = vb_size;
      vb_desc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
      vb_desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      tile.vertex_buffer = device_->createBuffer(vb_desc);
      if (!tile.vertex_buffer)
        return;
    }
    if (void* p = tile.vertex_buffer->map()) {
      std::vector<GlobeVertex> gpu_verts;
      gpu_verts.reserve(verts.size());
      for (const auto& v : verts) {
        GlobeVertex gv;
        gv.position = v.position;
        gv.normal = v.normal;
        gv.uv = v.uv;
        gpu_verts.push_back(gv);
      }
      std::memcpy(p, gpu_verts.data(), vb_size);
      tile.vertex_buffer->unmap();
    } else {
      // map() failed — leave the tile with no renderable geometry so render()
      // skips it (the existing guard tests index_count > 0).
      tile.index_count = 0;
      return;
    }

    // Index buffer always shared from table; update if edge scales changed.
    tile.index_buffer = index_table_.getBuffer(ls, rs, ts, bs);
    tile.index_count = index_table_.getIndexCount(ls, rs, ts, bs);
    tile.last_edge_scales = IndexBufferTable::tableIndex(ls, rs, ts, bs);
    // texture, view, desc_set preserved
  }

  void GlobeRenderer::refreshTileIBO(GpuTile& tile) {
    // Edge scales come from the drawing set's per-member layout (computed each
    // frame in buildDrawingSet), not leaf_info_ — because the rendered tile may
    // be an ancestor that the selector never picked as a leaf, and its
    // neighbors are *other drawing-set members*, not selector leaves.
    i32 ls = 1, rs = 1, ts = 1, bs = 1;
    if (auto it = drawing_set_info_.find(tile.coord); it != drawing_set_info_.end()) {
      ls = it->second.left_scale;
      rs = it->second.right_scale;
      ts = it->second.top_scale;
      bs = it->second.bottom_scale;
    }
    const i32 new_packed = IndexBufferTable::tableIndex(ls, rs, ts, bs);
    if (new_packed == tile.last_edge_scales)
      return; // no change

    tile.index_buffer = index_table_.getBuffer(ls, rs, ts, bs);
    tile.index_count = index_table_.getIndexCount(ls, rs, ts, bs);
    tile.last_edge_scales = new_packed;
  }

  void GlobeRenderer::uploadTile(const gis::TileCoord& coord, const gis::TileData& data, const GlobeCamera& camera) {
    if (data.width <= 0 || data.height <= 0 || data.image_data.empty()) {
      return; // No imagery — skip; base globe shows through
    }

    // Generate ellipsoid mesh for this tile
    const auto& rtc_origin = camera.getRTCOrigin();

    // Look up edge scales for this leaf (set by Phase 3 in leaf_info_).
    i32 ls = 1, rs = 1, ts = 1, bs = 1;
    {
      auto it = leaf_info_.find(coord);
      if (it != leaf_info_.end()) {
        ls = it->second.left_scale;
        rs = it->second.right_scale;
        ts = it->second.top_scale;
        bs = it->second.bottom_scale;
      }
    }

    // Pull DEM-baked heights from the geometry cache when available.
    static_assert(gis::kTileGeometryVertexCount == kTileVertexCount, "tile vertex grid sizes must match");
    const std::array<f32, kTileVertexCount>* heights = nullptr;
    gis::GeometryTile* geom = geom_cache_.find(coord);
    if (geom && geom->dirty_height) {
      heights = reinterpret_cast<const std::array<f32, kTileVertexCount>*>(&geom->heights);
    }

    std::array<EllipsoidMesh::Vertex, kTileVertexCount> verts;
    buildTileVertices(coord, coordinate::WGS84, rtc_origin, heights, verts);
    if (geom && heights)
      geom->dirty_height = false;

    // Vertex buffer
    BufferDesc vb_desc;
    vb_desc.size = verts.size() * sizeof(GlobeVertex);
    vb_desc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    vb_desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    auto vb = device_->createBuffer(vb_desc);
    bool vb_mapped = false;
    if (vb) {
      std::vector<GlobeVertex> gpu_verts;
      gpu_verts.reserve(verts.size());
      for (const auto& v : verts) {
        GlobeVertex gv;
        gv.position = v.position;
        gv.normal = v.normal;
        gv.uv = v.uv;
        gpu_verts.push_back(gv);
      }
      if (void* p = vb->map()) {
        std::memcpy(p, gpu_verts.data(), vb_desc.size);
        vb->unmap();
        vb_mapped = true;
      }
    }

    // Index buffer comes from the shared table — leaf's index buffer is a
    // shared pointer to one of 16 pre-baked GPU buffers.
    RHIBufferPtr ib = index_table_.getBuffer(ls, rs, ts, bs);
    u32 ib_count = index_table_.getIndexCount(ls, rs, ts, bs);

    // Create per-tile imagery texture. A complete chain is deliberately built
    // on the CPU because uploadTextureData already accepts all mip payloads and
    // this keeps behavior deterministic across Vulkan drivers.
    RHITexturePtr tex;
    RHITextureViewPtr view;
    const auto mip_chain =
        buildRgba8MipChain(static_cast<u32>(data.width), static_cast<u32>(data.height), data.image_data);
    {
      TextureDesc td;
      td.type = TextureType::Texture2D;
      td.extent = {static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height), 1u};
      td.mipLevels = static_cast<u32>(mip_chain.size());
      td.arrayLayers = 1u;
      td.format = Format::R8G8B8A8_UNORM;
      td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
      td.sampleCount = 1u;
      if (!mip_chain.empty())
        tex = device_->createTexture(td);
      if (tex) {
        std::vector<RHIDevice::MipData> mip_data;
        mip_data.reserve(mip_chain.size());
        for (const auto& level : mip_chain) {
          RHIDevice::MipData mip;
          mip.width = level.width;
          mip.height = level.height;
          mip.depth = 1u;
          mip.data = level.pixels.data();
          mip.dataSize = level.pixels.size();
          mip_data.push_back(mip);
        }
        device_->uploadTextureData(tex, mip_data, 0u);
        view = tex->createView();
      }
    }

    // Descriptor set binding tile texture to set=0 binding=0
    RHIDescriptorSetPtr desc_set;
    if (view && tile_desc_layout_ && tile_sampler_) {
      DescriptorSetDesc dsd;
      DescriptorBinding db;
      db.binding = 0u;
      db.type = DescriptorType::CombinedImageSampler;
      db.textureView = view;
      db.sampler = tile_sampler_;
      dsd.bindings.push_back(db);
      desc_set = device_->createDescriptorSet(tile_desc_layout_, dsd);
    }

    // Bail if any GPU resource failed to allocate — MoltenVK silently returns
    // null on memory pressure, and a partial entry (e.g. valid desc_set bound
    // to uninitialized texture memory) would render as a black rectangle.
    // Leaving the tile out of gpu_cache_ means the base layer shows through,
    // and the next visible-tile pass will re-request it cleanly.
    if (!vb || !vb_mapped || !ib || !tex || !view || !desc_set) {
      spdlog::debug("GlobeRenderer: tile {} upload failed (vb={} vb_mapped={} ib={} tex={} view={} desc={})",
                    coord.toString(),
                    !!vb,
                    vb_mapped,
                    !!ib,
                    !!tex,
                    !!view,
                    !!desc_set);
      return;
    }

    // Mark this tile "low-quality" if >50% of sampled pixels are literal
    // RGB(0,0,0) — Tianditu's no-data marker. buildDrawingSet's walk-up
    // will then skip past it to a coarser ancestor (which we pre-request as
    // an anchor in update() step 3a), so the rendered LOD is one that has
    // actual data. The tile still uploads — shader discard handles the
    // black pixels even when it does end up in the drawing set (e.g. when
    // every ancestor is also low-quality).
    bool low_quality = false;
    {
      size_t n_samples = 0;
      size_t n_black = 0;
      const size_t total_pixels = static_cast<size_t>(data.width) * static_cast<size_t>(data.height);
      for (size_t pix = 0; pix < total_pixels; pix += 64) {
        const uint8_t* p = &data.image_data[pix * 4];
        if (p[0] < 5 && p[1] < 5 && p[2] < 5)
          ++n_black;
        ++n_samples;
      }
      low_quality = (n_samples > 0 && n_black * 2 > n_samples);
    }

    GpuTile* slot = gpu_cache_.touch(coord, frame_counter_);
    slot->vertex_buffer = vb;
    slot->index_buffer = ib; // shared; not owned by this tile
    slot->index_count = ib_count;
    slot->last_edge_scales = IndexBufferTable::tableIndex(ls, rs, ts, bs);
    slot->texture = tex;
    slot->view = view;
    slot->desc_set = desc_set;
    slot->owns_real_texture = true;
    slot->low_quality = low_quality;
    slot->mip_levels = static_cast<u32>(mip_chain.size());
    slot->source_transition_edges = imagery_layers_.empty() ? gis::TileSourceTransitionNone
                                                            : imagery_layers_.front()->getSourceTransitionEdges(coord);
    slot->render_texture_source = coord;
    slot->uv_offset_scale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
  }

  // ============================================================================
  // Default tiles
  // ============================================================================

  // Upload one RGBA tile to the GPU; returns the created texture and view.
  static bool uploadEarthTile(RHIDevice* device,
                              const std::string& stem,
                              const std::vector<uint8_t>& pixels,
                              int w,
                              int h,
                              RHITexturePtr& out_tex,
                              RHITextureViewPtr& out_view) {
    TextureDesc td;
    td.type = TextureType::Texture2D;
    td.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
    td.mipLevels = 1u;
    td.arrayLayers = 1u;
    td.format = Format::R8G8B8A8_UNORM;
    td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
    td.sampleCount = 1u;

    out_tex = device->createTexture(td);
    if (!out_tex) {
      spdlog::error("GlobeRenderer: createTexture failed for '{}'", stem);
      return false;
    }

    RHIDevice::MipData mip;
    mip.width = static_cast<uint32_t>(w);
    mip.height = static_cast<uint32_t>(h);
    mip.data = pixels.data();
    mip.dataSize = pixels.size();
    device->uploadTextureData(out_tex, {mip}, 0u);

    out_view = out_tex->createView();
    if (!out_view) {
      spdlog::error("GlobeRenderer: createView failed for '{}'", stem);
      return false;
    }

    spdlog::info("GlobeRenderer: uploaded earth tile '{}' ({}x{})", stem, w, h);
    return true;
  }

  void GlobeRenderer::loadDefaultTiles() {
    namespace fs = std::filesystem;
    const fs::path root = asset_root_.empty() ? fs::path{} : fs::path(asset_root_);
    const fs::path dir = root / "texture" / "earth";
    if (!fs::exists(dir)) {
      spdlog::warn("GlobeRenderer::loadDefaultTiles: directory '{}' not found", dir.string());
      return;
    }

    // west tile: lon[-180°, 0°]  → globe u=[0.0, 0.5]
    // east tile: lon[  0°,+180°] → globe u=[0.5, 1.0]
    struct TileSpec {
      std::string stem;
      RHITexturePtr& tex;
      RHITextureViewPtr& view;
    };
    std::array<TileSpec, 2> specs = {
        {{"0_0_0", earth_west_texture_, earth_west_view_}, {"0_1_0", earth_east_texture_, earth_east_view_}}};

    for (auto& spec : specs) {
      EarthTextureImage image;

      for (const char* ext : {".jpg", ".dds"}) {
        const std::string path = (dir / (spec.stem + ext)).string();
        image = loadEarthTextureRgba8(path);
        if (image.valid())
          break;
      }

      if (!image.valid()) {
        spdlog::warn("GlobeRenderer: earth tile '{}' not found or failed to decode", spec.stem);
        continue;
      }

      uploadEarthTile(device_, spec.stem, image.rgba, image.width, image.height, spec.tex, spec.view);
    }

    if (!earth_west_view_ || !earth_east_view_) {
      spdlog::warn("GlobeRenderer: one or both earth tiles missing – using procedural colors");
      return;
    }

    // Sampler (linear, clamp)
    SamplerDesc sd;
    sd.magFilter = SamplerFilter::Linear;
    sd.minFilter = SamplerFilter::Linear;
    sd.mipmapMode = SamplerMipmapMode::Linear;
    sd.addressModeU = SamplerAddressMode::ClampToEdge;
    sd.addressModeV = SamplerAddressMode::ClampToEdge;
    sd.addressModeW = SamplerAddressMode::ClampToEdge;
    sd.anisotropyEnable = false;
    sd.minLod = 0.0f;
    sd.maxLod = 1.0f;
    earth_sampler_ = device_->createSampler(sd);
    if (!earth_sampler_) {
      spdlog::error("GlobeRenderer: failed to create earth sampler");
      return;
    }

    earth_textures_ready_ = true;
    spdlog::info("GlobeRenderer: earth GPU textures ready (west + east hemisphere)");

    // Load cloud overlay texture (optional — globe works without it)
    {
      int w = 0, h = 0, channels = 0;
      const std::string cloud_path = (root / "texture" / "sky" / "Globe_Cloud.png").string();
      unsigned char* pix = stbi_load(cloud_path.c_str(), &w, &h, &channels, 4);
      if (pix) {
        TextureDesc td;
        td.type = TextureType::Texture2D;
        td.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
        td.mipLevels = 1u;
        td.arrayLayers = 1u;
        td.format = Format::R8G8B8A8_UNORM;
        td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
        td.sampleCount = 1u;
        cloud_texture_ = device_->createTexture(td);
        if (cloud_texture_) {
          RHIDevice::MipData mip;
          mip.width = static_cast<uint32_t>(w);
          mip.height = static_cast<uint32_t>(h);
          mip.data = pix;
          mip.dataSize = static_cast<size_t>(w * h * 4);
          device_->uploadTextureData(cloud_texture_, {mip}, 0u);
          cloud_view_ = cloud_texture_->createView();
          cloud_ready_ = cloud_view_ != nullptr;
          spdlog::info("GlobeRenderer: loaded cloud texture {}x{}", w, h);
        }
        stbi_image_free(pix);
      } else {
        spdlog::warn("GlobeRenderer: cloud texture '{}' not found, clouds disabled", cloud_path);
      }
    }

    // Recreate descriptor set with 3 bindings: west, east, cloud
    // Use a 1x1 white fallback texture for cloud binding if not loaded
    RHITexturePtr fallback_tex;
    RHITextureViewPtr fallback_view;
    if (!cloud_ready_) {
      // 1×1 white pixel as fallback for binding 2
      uint8_t white[4] = {255, 255, 255, 255};
      TextureDesc td;
      td.type = TextureType::Texture2D;
      td.extent = {1u, 1u, 1u};
      td.mipLevels = 1u;
      td.arrayLayers = 1u;
      td.format = Format::R8G8B8A8_UNORM;
      td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
      td.sampleCount = 1u;
      fallback_tex = device_->createTexture(td);
      if (fallback_tex) {
        RHIDevice::MipData mip;
        mip.width = mip.height = 1u;
        mip.data = white;
        mip.dataSize = 4u;
        device_->uploadTextureData(fallback_tex, {mip}, 0u);
        fallback_view = fallback_tex->createView();
      }
    }

    auto& cloud_view_bind = cloud_ready_ ? cloud_view_ : fallback_view;

    // Rebuild descriptor set with cloud binding
    earth_desc_set_.reset();
    DescriptorSetDesc ds3;
    DescriptorBinding db0;
    db0.binding = 0u;
    db0.type = DescriptorType::CombinedImageSampler;
    db0.textureView = earth_west_view_;
    db0.sampler = earth_sampler_;
    DescriptorBinding db1;
    db1.binding = 1u;
    db1.type = DescriptorType::CombinedImageSampler;
    db1.textureView = earth_east_view_;
    db1.sampler = earth_sampler_;
    DescriptorBinding db2;
    db2.binding = 2u;
    db2.type = DescriptorType::CombinedImageSampler;
    db2.textureView = cloud_view_bind;
    db2.sampler = earth_sampler_;
    ds3.bindings = {db0, db1, db2};
    earth_desc_set_ = device_->createDescriptorSet(earth_desc_layout_, ds3);
    if (!earth_desc_set_) {
      spdlog::error("GlobeRenderer: failed to recreate descriptor set with cloud");
    }

    // ------------------------------------------------------------------
    // Global water mask (tools/generate_water_mask.py). Sampled by lat/lon in
    // the tile shader so the ocean water effect is gated by one globally
    // consistent mask, not per-tile imagery color — removes the blocky
    // water-boundary artifacts that patchy imagery produces. Missing file →
    // 1×1 black fallback (max(0, colorMask) = colorMask, i.e. old behavior).
    // ------------------------------------------------------------------
    {
      const std::string mask_path = (dir / "water_mask.png").string();
      int w = 0, h = 0, channels = 0;
      unsigned char* pix = stbi_load(mask_path.c_str(), &w, &h, &channels, 4);
      if (pix) {
        std::vector<u8> pixels(pix, pix + static_cast<size_t>(w * h * 4));
        stbi_image_free(pix);
        uploadEarthTile(device_, "water_mask", pixels, w, h, water_mask_texture_, water_mask_view_);
        if (water_mask_view_) {
          spdlog::info("GlobeRenderer: global water mask loaded ({}x{})", w, h);
        }
      } else {
        spdlog::warn("GlobeRenderer: '{}' not found — ocean mask falls back to "
                     "per-pixel imagery color (blocky on patchy imagery)",
                     mask_path);
      }
      if (!water_mask_view_) {
        // 1×1 black: gmask = 0 everywhere → color classification alone.
        uint8_t black[4] = {0, 0, 0, 255};
        TextureDesc td;
        td.type = TextureType::Texture2D;
        td.extent = {1u, 1u, 1u};
        td.mipLevels = 1u;
        td.arrayLayers = 1u;
        td.format = Format::R8G8B8A8_UNORM;
        td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
        td.sampleCount = 1u;
        water_mask_texture_ = device_->createTexture(td);
        if (water_mask_texture_) {
          RHIDevice::MipData mip;
          mip.width = mip.height = 1u;
          mip.data = black;
          mip.dataSize = 4u;
          device_->uploadTextureData(water_mask_texture_, {mip}, 0u);
          water_mask_view_ = water_mask_texture_->createView();
        }
      }
      if (water_mask_view_ && tile_desc_layout_ && tile_sampler_) {
        DescriptorSetDesc dsd;
        DescriptorBinding db;
        db.binding = 0u;
        db.type = DescriptorType::CombinedImageSampler;
        db.textureView = water_mask_view_;
        db.sampler = tile_sampler_;
        dsd.bindings.push_back(db);
        water_mask_desc_set_ = device_->createDescriptorSet(tile_desc_layout_, dsd);
      }
    }
  }

  // ============================================================================
  // Queries
  // ============================================================================

  RHIPipelineLayoutPtr GlobeRenderer::getPipelineLayout() const {
    return pipeline_layout_;
  }

  i32 GlobeRenderer::getVisibleTileCount() const {
    return static_cast<i32>(visible_tiles_.size());
  }

  i32 GlobeRenderer::getLoadedTileCount() const {
    return static_cast<i32>(gpu_cache_.size());
  }

  i32 GlobeRenderer::getPendingTileCount() const {
    return static_cast<i32>(loading_tiles_.size());
  }

  GlobeStreamingStats GlobeRenderer::getStreamingStats() const {
    GlobeStreamingStats stats;
    if (!imagery_layers_.empty()) {
      stats.imagery_provider = imagery_layers_.front()->getName();
    }
    stats.cache_namespace = tile_cache_.getNamespace();
    stats.visible_tiles = static_cast<i32>(visible_tiles_.size());
    stats.drawing_tiles = static_cast<i32>(drawing_set_.size());
    stats.loaded_tiles = static_cast<i32>(gpu_cache_.size());
    stats.pending_tiles = static_cast<i32>(loading_tiles_.size());
    stats.decoded_tiles = static_cast<i32>(decoded_tiles_.size());
    stats.queued_downloads = download_queue_.getPendingCount();
    stats.active_downloads = download_queue_.getActiveCount();
    stats.cache_hits = tile_cache_.getCacheHitCount();
    stats.cache_misses = tile_cache_.getCacheMissCount();
    stats.cache_memory_usage = tile_cache_.getMemoryUsage();
    stats.cache_memory_budget = tile_cache_.getMemoryBudget();

    auto level_range = [](const auto& tiles, i32& min_level, i32& max_level) {
      if (tiles.empty()) {
        return;
      }
      min_level = std::numeric_limits<i32>::max();
      max_level = std::numeric_limits<i32>::min();
      for (const auto& tile : tiles) {
        min_level = std::min(min_level, tile.z);
        max_level = std::max(max_level, tile.z);
      }
    };
    level_range(visible_tiles_, stats.visible_min_level, stats.visible_max_level);
    level_range(drawing_set_, stats.drawing_min_level, stats.drawing_max_level);

    for (const auto& tile : gpu_cache_.tiles()) {
      if (!tile.owns_real_texture || tile.mip_levels == 0)
        continue;
      if (tile.mip_levels > 1)
        ++stats.mipped_tiles;
      const i32 levels = static_cast<i32>(tile.mip_levels);
      stats.tile_mip_levels_min = stats.tile_mip_levels_min == 0 ? levels : std::min(stats.tile_mip_levels_min, levels);
      stats.tile_mip_levels_max = std::max(stats.tile_mip_levels_max, levels);
    }

    for (const auto& coord : drawing_set_) {
      const GpuTile* tile = gpu_cache_.find(coord);
      if (auto info = drawing_set_info_.find(coord); info != drawing_set_info_.end()) {
        const auto& transition = info->second;
        const i32 edges = (transition.left_scale == 2 ? 1 : 0) + (transition.right_scale == 2 ? 1 : 0) +
                          (transition.top_scale == 2 ? 1 : 0) + (transition.bottom_scale == 2 ? 1 : 0);
        if (edges > 0) {
          ++stats.lod_transition_tiles;
          stats.lod_transition_edges += edges;
        }
      }
      if (!tile || tile->source_transition_edges == gis::TileSourceTransitionNone) {
        continue;
      }
      ++stats.source_transition_tiles;
      for (u8 edges = tile->source_transition_edges; edges != 0; edges >>= 1u) {
        stats.source_transition_edges += edges & 1u;
      }
    }
    return stats;
  }

  void GlobeRenderer::dumpDrawingSet() const {
    spdlog::info("=== drawing_set_ dump: {} members ===", drawing_set_.size());
    // Per-LOD histogram first for quick overview.
    std::map<i32, i32> lod_hist;
    for (const auto& c : drawing_set_)
      lod_hist[c.z]++;
    for (const auto& [z, n] : lod_hist)
      spdlog::info("  z={}: {} tiles", z, n);

    // Then a per-tile line: coord, geometry/texture status, edge_scales.
    for (const auto& c : drawing_set_) {
      const auto* slot = gpu_cache_.find(c);
      auto info_it = drawing_set_info_.find(c);
      const bool has_vb = slot && slot->vertex_buffer;
      const bool has_ib = slot && slot->index_buffer;
      const bool has_tex = slot && slot->texture;
      const bool has_desc = slot && slot->desc_set;
      const bool owns_real = slot && slot->owns_real_texture;
      i32 ls = 1, rs = 1, ts = 1, bs = 1;
      if (info_it != drawing_set_info_.end()) {
        ls = info_it->second.left_scale;
        rs = info_it->second.right_scale;
        ts = info_it->second.top_scale;
        bs = info_it->second.bottom_scale;
      }
      spdlog::info("  {} vb={} ib={} tex={} desc={} owns_real={} mips={} scales=L{}R{}T{}B{} source_edges=0x{:x} "
                   "idx_cnt={}",
                   c.toString(),
                   has_vb,
                   has_ib,
                   has_tex,
                   has_desc,
                   owns_real,
                   slot ? slot->mip_levels : 0u,
                   ls,
                   rs,
                   ts,
                   bs,
                   slot ? slot->source_transition_edges : 0u,
                   slot ? slot->index_count : 0u);
    }
  }

} // namespace hongtu::renderer
