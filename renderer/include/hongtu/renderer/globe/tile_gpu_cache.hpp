#pragma once

#include <glm/vec4.hpp>
#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <hongtu/rhi/RHIResources.h>

#include <vector>

namespace hongtu::renderer {

  struct GpuTile {
    gis::TileCoord coord;
    HongTu::RHI::RHIBufferPtr vertex_buffer;
    HongTu::RHI::RHIBufferPtr index_buffer; // shared from IndexBufferTable
    u32 index_count = 0;
    HongTu::RHI::RHITexturePtr texture;
    HongTu::RHI::RHITextureViewPtr view;
    HongTu::RHI::RHIDescriptorSetPtr desc_set;
    bool owns_real_texture = false;
    // True if the uploaded image is mostly literal RGB(0,0,0) — Tianditu's
    // no-data marker for satellite-coverage gaps. The tile still uploads and
    // can render (a few real pixels along the edge are useful), but the
    // walk-up in buildDrawingSet skips past it so a coarser ancestor takes
    // its place if one is available.
    bool low_quality = false;
    // Complete imagery mip levels resident in `texture`. A normal 256x256
    // routed-imagery tile has 9 levels (256..1); zero means no real texture.
    u32 mip_levels = 0;
    // Bit mask of same-LOD edges whose adjacent tile comes from another
    // imagery route/provider. Populated once at upload time and consumed by the
    // existing parent-texture edge feather in GlobeRenderer.
    u8 source_transition_edges = 0;
    u64 frame = 0;

    // Current edge_scales bit-packed (L=bit0, R=bit1, T=bit2, B=bit3).
    // -1 = "uninitialized — first frame will set this". Matches
    // IndexBufferTable::tableIndex output range [0, 15].
    i32 last_edge_scales = -1;

    // Per-frame, written by buildDrawingSet:
    gis::TileCoord render_texture_source{};
    glm::vec4 uv_offset_scale{0.0f, 0.0f, 1.0f, 1.0f};
  };

  class TileGpuCache {
  public:
    GpuTile* touch(const gis::TileCoord& coord, u64 frame);
    GpuTile* find(const gis::TileCoord& coord);
    const GpuTile* find(const gis::TileCoord& coord) const;

    /// Iterate all entries (read-only). Returns const reference to internal
    /// storage; pointer stability holds only between mutating calls.
    const std::vector<GpuTile>& tiles() const {
      return tiles_;
    }
    std::vector<GpuTile>& tiles() {
      return tiles_;
    }

    void endFrameWithEviction(u64 frame);

    std::size_t size() const {
      return tiles_.size();
    }
    void setRatio(f32 r) {
      ratio_ = r;
    }
    void clear() {
      tiles_.clear();
      peak_ = 0;
    }

  private:
    std::vector<GpuTile> tiles_;
    u32 peak_ = 0;
    f32 ratio_ = 2.0f;
  };

} // namespace hongtu::renderer
