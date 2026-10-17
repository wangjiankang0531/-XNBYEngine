#pragma once

#include <hongtu/core/common.hpp>

#include <vector>

namespace hongtu::renderer {

  /// One tightly packed RGBA8 mip level.
  struct Rgba8MipLevel {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels;
  };

  /// Build a complete, box-filtered RGBA8 mip chain down to 1x1.
  ///
  /// Returns an empty vector for zero dimensions, overflow, or a byte count
  /// that does not exactly match width * height * 4. Odd dimensions are
  /// handled by mapping every source pixel into the smaller Vulkan mip size.
  std::vector<Rgba8MipLevel> buildRgba8MipChain(u32 width, u32 height, const std::vector<u8>& rgba);

} // namespace hongtu::renderer
