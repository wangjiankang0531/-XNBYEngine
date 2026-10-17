#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/core/net/download_queue.hpp>
#include <hongtu/core/net/http_client.hpp>
#include <hongtu/gis/imagery/imagery_provider.hpp>
#include <hongtu/gis/terrain/dem_provider.hpp>
#include <hongtu/gis/tile/tile_cache.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <hongtu/gis/tile/tile_geometry_cache.hpp>
#include <hongtu/gis/tile/tile_quad_tree_selector.hpp>
#include <hongtu/renderer/globe/atmosphere_renderer.hpp>
#include <hongtu/renderer/globe/ellipsoid_mesh.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/globe/index_buffer_table.hpp>
#include <hongtu/renderer/globe/label_renderer.hpp>
#include <hongtu/renderer/globe/skybox_renderer.hpp>
#include <hongtu/renderer/globe/tile_gpu_cache.hpp>
#include <hongtu/renderer/globe/vector_layer_renderer.hpp>
#include <hongtu/renderer/globe/water_surface_renderer.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hongtu::renderer {

  using namespace hongtu;
  using namespace HongTu::RHI;

  /// Push constant data for the tile vertex shader.
  /// Phase 1: uv_offset_scale is identity (0,0,1,1) for every tile.
  /// Phase 2-3: ancestor fallback populates non-identity values.
  struct TilePushData {
    // Tile vertices are pre-baked into RTC world space, so this is the
    // view-projection matrix only — no per-tile model matrix.
    glm::mat4 vp;
    glm::vec4 uv_offset_scale; // (off.x, off.y, scale.x, scale.y)
    // Parent (coarser-LOD) ancestor texture UV remap so the shader can
    // sample the parent at the same world position as this fragment.
    // (off.x, off.y, scale.x, scale.y) — same convention as above.
    // When the tile has no loaded coarser ancestor, this is identity (the
    // shader then samples its own texture as parent → blend is a no-op).
    glm::vec4 parent_uv_offset_scale;
    // Per-edge fade flag (L,R,T,B) — 1.0 on edges that face a coarser-LOD
    // neighbor in the drawing set. The fragment shader fades from self to
    // parent on those edges, hiding the LOD seam.
    glm::vec4 edge_fade;
    // --- Water shading (frame-constant; same value for every tile this frame).
    // These mirror the existing per-tile re-push of `vp`: they're identical
    // across tiles but ride the push for simplicity. The fragment shader uses
    // them to add a sun glint to imagery pixels that classify as water.
    glm::vec4 sun_dir_time; // xyz = sun direction (ECEF), w = elapsed time (s)
    glm::vec4 cam_rtc;      // xyz = camera position in RTC space, w = altitude (m)
    glm::vec4 rtc_origin;   // xyz = RTC origin in ECEF, w = tile LOD (0 = no parent)
  };
  // 160 bytes. Above Vulkan's 128 B guaranteed minimum, but the engine targets
  // MoltenVK (maxPushConstantsSize = 4096) and validation layers (ON) reject any
  // device that can't honor the range at pipeline creation. If a portability
  // target with a 128 B cap is ever added, migrate these three frame-constant
  // vec4s to a per-frame uniform buffer.
  static_assert(sizeof(TilePushData) == 160, "TilePushData layout changed — update shaders + push range");

  /// Read-only snapshot used by demos, diagnostics and regression tooling.
  struct GlobeStreamingStats {
    std::string imagery_provider;
    std::string cache_namespace;
    i32 visible_tiles = 0;
    i32 drawing_tiles = 0;
    i32 loaded_tiles = 0;
    i32 pending_tiles = 0;
    i32 decoded_tiles = 0;
    i32 visible_min_level = -1;
    i32 visible_max_level = -1;
    i32 drawing_min_level = -1;
    i32 drawing_max_level = -1;
    i32 mipped_tiles = 0;
    i32 tile_mip_levels_min = 0;
    i32 tile_mip_levels_max = 0;
    i32 lod_transition_tiles = 0;
    i32 lod_transition_edges = 0;
    i32 source_transition_tiles = 0;
    i32 source_transition_edges = 0;
    std::size_t queued_downloads = 0;
    std::size_t active_downloads = 0;
    std::size_t cache_hits = 0;
    std::size_t cache_misses = 0;
    std::size_t cache_memory_usage = 0;
    std::size_t cache_memory_budget = 0;
  };

  /// Main globe rendering controller.
  ///
  /// Orchestrates tile loading, caching, and rendering for a digital earth.
  /// The base globe uses GPU textures (west/east hemispheres) for high-quality
  /// satellite imagery. Network tile overlays are baked into vertex colors and
  /// rendered on top.
  class GlobeRenderer {
  public:
    explicit GlobeRenderer(RHIDevice* device);
    ~GlobeRenderer();

    GlobeRenderer(const GlobeRenderer&) = delete;
    GlobeRenderer& operator=(const GlobeRenderer&) = delete;

    void addImageryLayer(std::shared_ptr<gis::ImageryProvider> provider);

    /// Drop all imagery providers and discard cached/in-flight tiles. The next
    /// `addImageryLayer()` + frame will refetch from scratch. Used by the editor
    /// to live-switch between providers (OSM ↔ Esri ↔ Tianditu) without tearing
    /// down the GlobeRenderer.
    void clearImageryLayers();

    /// Add a GeoJSON/Shapefile feature collection as a vector overlay. Returns layer ID.
    int addVectorLayer(const gis::FeatureCollection& fc, const VectorStyle& style = {});

    /// Remove a previously added vector layer.
    void removeVectorLayer(int layer_id);

    /// Add a local water body (lake / river / flood zone): an explicit geodetic
    /// boundary polygon whose per-vertex alt is the water level. Rendered as an
    /// alpha-blended water surface (Fresnel + sun glint, close-range ripples).
    /// Returns the body id, or -1 for a degenerate boundary.
    int addWaterBody(std::vector<coordinate::Geodetic> boundary, const WaterStyle& style = {});
    void removeWaterBody(int id);
    void clearWaterBodies();

    void setTileCachePath(const std::string& path);
    /// Resolve built-in globe imagery, cloud, and water-mask assets from this
    /// root instead of relying on the process working directory. Call before
    /// initialize() when a demo can be launched from an isolated directory.
    void setAssetRoot(const std::string& path);
    void setMemoryBudget(size_t bytes);
    void setMaxConcurrentDownloads(i32 n);
    void setScreenSize(f32 width, f32 height);
    void setTileRenderingEnabled(bool enabled);
    /// Enable the globe's space skybox and atmospheric shell. Composite demos
    /// can disable these during a local-scene handoff while continuing to draw
    /// the ellipsoid and streamed imagery below their own sky dome.
    void setEnvironmentRenderingEnabled(bool enabled) {
      environment_rendering_enabled_ = enabled;
    }
    bool isTileRenderingEnabled() const {
      return tile_rendering_enabled_;
    }

    void setDemProvider(std::shared_ptr<gis::DemProvider> provider);

    /// Pin the sun to a fixed ECEF direction and stop the automatic day-orbit
    /// animation. Used for time-of-day control in the editor and for
    /// deterministic water-glint verification. `dir` need not be normalized.
    void setSunDirection(const glm::vec3& dir);
    /// Re-enable the automatic sun orbit (undoes setSunDirection).
    void setSunAnimated(bool animated) {
      sun_animated_ = animated;
    }
    glm::vec3 getSunDirection() const {
      return sun_dir_ecef_;
    }

    /// Add a labeled marker at a geodetic position.
    void addLabel(const Label& label);
    /// Remove all labels.
    void clearLabels();

    /// Initialize shaders, pipeline, and default earth textures.
    bool initialize(RHIRenderPassPtr render_pass);

    void update(f32 dt, const GlobeCamera& camera);
    void render(RHICommandBufferPtr cmd, const GlobeCamera& camera);

    RHIPipelineLayoutPtr getPipelineLayout() const;
    i32 getVisibleTileCount() const;
    i32 getLoadedTileCount() const;
    i32 getPendingTileCount() const;
    GlobeStreamingStats getStreamingStats() const;

    /// Dump the per-frame drawing set to stdout (coord, edge_scales, whether
    /// the tile has texture/desc_set/vbo). For debugging visual artifacts —
    /// run autotest with --dump-drawing-set to flush this on the last frame.
    void dumpDrawingSet() const;

  private:
    /// Lightweight buffer-only handle for the base globe mesh. The per-tile
    /// caches use `GpuTile` from `tile_gpu_cache.hpp`; this struct just holds
    /// the base-mesh vertex/index buffers so it can share `uploadMesh`.
    struct BaseGlobeMesh {
      RHIBufferPtr vertex_buffer;
      RHIBufferPtr index_buffer;
      u32 index_count = 0;
    };

    u64 frame_counter_ = 0;

    void requestTile(const gis::TileCoord& coord, i32 attempt = 0);
    void uploadTile(const gis::TileCoord& coord, const gis::TileData& data, const GlobeCamera& camera);
    // Resolves each visible leaf's render texture source (own or ancestor)
    // and writes uv_offset_scale to the corresponding GpuTile.
    void buildDrawingSet(const std::vector<gis::TileCoord>& leaves);
    void uploadMesh(const EllipsoidMesh::TileMeshData& mesh, BaseGlobeMesh& out_tile);
    /// Recreate only the vertex/index buffers of an already-loaded tile so the
    /// new RTC origin is reflected in the float32 positions. Texture/descriptor
    /// set are preserved.
    void regenerateTileMesh(const gis::TileCoord& coord, GpuTile& tile, const GlobeCamera& camera);

    // Refresh the tile's index_buffer / index_count if its edge_scales have
    // changed since the last frame. Cheap — pointer swap into the shared
    // IndexBufferTable, no allocation.
    void refreshTileIBO(GpuTile& tile);

    // Upload west/east earth tiles from texture/earth/ to GPU textures.
    void loadDefaultTiles();

    RHIDevice* device_ = nullptr;
    std::string asset_root_;

    // Space background and atmosphere
    std::unique_ptr<SkyboxRenderer> skybox_;
    AtmosphereRenderer atmosphere_;

    // Local water bodies (M3 geo water)
    WaterSurfaceRenderer water_surfaces_;

    // Vector overlay layers (GeoJSON, Shapefile features on the globe)
    VectorLayerRenderer vector_layers_;

    // Billboard labels at geodetic positions
    LabelRenderer label_renderer_;

    // Base globe pipeline (hemispheric earth_west/east textures)
    RHIShaderPtr vertex_shader_;
    RHIShaderPtr fragment_shader_;
    RHIDescriptorSetLayoutPtr earth_desc_layout_;
    RHIPipelineLayoutPtr pipeline_layout_;
    RHIPipelinePtr pipeline_;

    // Per-tile pipeline (samples each tile's own OSM/imagery texture)
    RHIShaderPtr tile_vs_;
    RHIShaderPtr tile_fs_;
    RHIDescriptorSetLayoutPtr tile_desc_layout_;
    RHIPipelineLayoutPtr tile_pipeline_layout_;
    RHIPipelinePtr tile_pipeline_;
    RHISamplerPtr tile_sampler_;

    // Default earth textures: west = lon[-180°,0°], east = lon[0°,+180°]
    RHITexturePtr earth_west_texture_;
    RHITexturePtr earth_east_texture_;
    RHITextureViewPtr earth_west_view_;
    RHITextureViewPtr earth_east_view_;
    RHISamplerPtr earth_sampler_;
    RHIDescriptorSetPtr earth_desc_set_;
    bool earth_textures_ready_ = false;

    // Cloud overlay texture (equirectangular, full globe)
    RHITexturePtr cloud_texture_;
    RHITextureViewPtr cloud_view_;
    bool cloud_ready_ = false;

    // Global water mask (equirectangular, 255 = water). Generated offline from
    // the blue-marble base textures (tools/generate_water_mask.py). The tile
    // shader samples it by lat/lon so the ocean water effect is gated by a
    // globally CONSISTENT mask instead of per-tile imagery color — that's what
    // kills the blocky water-boundary artifacts from patchy imagery. Falls back
    // to a 1×1 black texture (mask contributes nothing) when the PNG is absent.
    RHITexturePtr water_mask_texture_;
    RHITextureViewPtr water_mask_view_;
    RHIDescriptorSetPtr water_mask_desc_set_;

    // Sun direction in ECEF space (animated unless pinned via setSunDirection)
    glm::vec3 sun_dir_ecef_{0.866f, 0.0f, 0.5f};
    bool sun_animated_ = true;
    float elapsed_time_ = 0.0f;

    // Tile management
    core::net::HttpClient http_client_;
    core::net::DownloadQueue download_queue_;
    gis::TileCache tile_cache_;
    gis::TileQuadTreeSelector quad_tree_;
    // Edge scales for LOD seam stitching, populated by the selector.
    // Z=3 base-layer tiles that weren't chosen by the selector intentionally
    // have no entry here — consumers must treat a missing entry as
    // edge_scales = {1, 1, 1, 1} (all neighbors same-or-finer).
    std::unordered_map<gis::TileCoord, gis::SelectedTile, gis::TileCoord::Hash> leaf_info_;

    std::vector<std::shared_ptr<gis::ImageryProvider>> imagery_layers_;

    // DEM terrain
    std::shared_ptr<gis::DemProvider> dem_provider_;
    std::unordered_map<gis::TileCoord, gis::DemTile, gis::TileCoord::Hash> dem_tiles_;
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> dem_loading_;
    // The deepest DEM zoom level that was loaded across every visible
    // tile's footprint on the previous frame. When this changes (a new
    // finer DEM completes for the missing area), every cached geometry
    // bake is invalidated so the next frame re-bakes uniformly at the
    // new level — keeping all adjacent tiles' shared-edge heights in
    // agreement. -1 means "no DEM constraint" (selector empty or
    // provider missing).
    i32 last_uniform_dem_level_ = -2;

    BaseGlobeMesh base_globe_;
    gis::TileGeometryCache geom_cache_;
    TileGpuCache gpu_cache_;
    std::unordered_map<gis::TileCoord, gis::TileData, gis::TileCoord::Hash> decoded_tiles_;
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> loading_tiles_;
    // Sparse routed providers intentionally omit zoom levels and geographic
    // regions. Remember those misses separately from real in-flight requests
    // so they neither retry every frame nor inflate pending-download telemetry.
    std::unordered_set<gis::TileCoord, gis::TileCoord::Hash> unavailable_tiles_;
    std::vector<gis::TileCoord> visible_tiles_;

    // The tiles actually drawn this frame, after walking each visible leaf up
    // to its deepest loaded ancestor. Each entry is rendered with its OWN
    // geometry + OWN texture + identity UV — no ancestor-texture borrowing,
    // no UV sub-sampling. Auto-deduplicated: sibling leaves that share an
    // unloaded parent produce a single parent draw, not N broken pieces.
    std::vector<gis::TileCoord> drawing_set_;
    // Per-frame edge_scales for drawing_set_ members, computed from the
    // drawing set's own LOD distribution (not from selector leaves), so
    // T-junction stitching is correct when neighbors render at different
    // levels due to incomplete streaming.
    std::unordered_map<gis::TileCoord, gis::SelectedTile, gis::TileCoord::Hash> drawing_set_info_;

    bool tile_rendering_enabled_ = true;
    bool environment_rendering_enabled_ = true;

    f32 screen_width_ = 1280.0f;
    f32 screen_height_ = 720.0f;

    glm::dvec3 cached_rtc_origin_{0.0};

    IndexBufferTable index_table_;
  };

} // namespace hongtu::renderer
