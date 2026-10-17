#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <array>
#include <vector>

namespace hongtu::renderer {

inline constexpr i32 kTileGridSize     = 33;            // verts per side
inline constexpr i32 kTileCellsPerSide = kTileGridSize - 1;
inline constexpr i32 kTileVertexCount  = kTileGridSize * kTileGridSize;  // 1089

/// Generate triangle indices for a 33×33 vertex grid whose 4 edges may stitch
/// to coarser neighbors. Each scale ∈ {1, 2}. 1 = same-or-finer neighbor
/// (full edge resolution). 2 = neighbor is one level coarser (every-other
/// vertex on this edge collapses to its coarser equivalent).
///
/// Returns a CPU vector of u32 indices; size always divisible by 3.
std::vector<u32> generateStitchingIndices(i32 left_scale, i32 right_scale,
                                           i32 top_scale, i32 bottom_scale);

/// Pre-built table of 16 GPU index buffers indexed by
/// `tableIndex(left, right, top, bottom)`. Built once in initialize().
class IndexBufferTable {
 public:
  /// Bit-pack 4 edge scales into an array slot. Order: L|R|T|B, each 1 bit
  /// (0 = scale 1, 1 = scale 2). Range 0..15.
  static i32 tableIndex(i32 left_scale, i32 right_scale,
                        i32 top_scale, i32 bottom_scale) {
    auto bit = [](i32 s) { return s >= 2 ? 1 : 0; };
    return (bit(left_scale)   << 0)
         | (bit(right_scale)  << 1)
         | (bit(top_scale)    << 2)
         | (bit(bottom_scale) << 3);
  }

  /// Build all 16 GPU index buffers on `device`. Returns false if any
  /// allocation fails.
  bool initialize(HongTu::RHI::RHIDevice* device);

  HongTu::RHI::RHIBufferPtr getBuffer(i32 left_scale, i32 right_scale,
                                       i32 top_scale, i32 bottom_scale) const {
    return buffers_[static_cast<u32>(tableIndex(left_scale, right_scale, top_scale, bottom_scale))];
  }

  u32 getIndexCount(i32 left_scale, i32 right_scale,
                    i32 top_scale, i32 bottom_scale) const {
    return index_counts_[static_cast<u32>(tableIndex(left_scale, right_scale, top_scale, bottom_scale))];
  }

 private:
  std::array<HongTu::RHI::RHIBufferPtr, 16> buffers_{};
  std::array<u32, 16> index_counts_{};
};

}  // namespace hongtu::renderer
