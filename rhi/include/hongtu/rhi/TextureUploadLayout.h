#pragma once

#include "hongtu/rhi/RHIDevice.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace HongTu::RHI {

  struct TextureUploadSlice {
    uint64_t offset = 0;
    uint64_t size = 0;
  };
  struct TextureMipUploadLayout {
    std::vector<TextureUploadSlice> slices;
    uint64_t bytes = 0;
  };

  inline bool isBlockCompressedFormat(Format format) {
    return format == Format::BC1_RGB_UNORM_BLOCK || format == Format::BC2_UNORM_BLOCK ||
           format == Format::BC7_UNORM_BLOCK || format == Format::BC7_SRGB_BLOCK;
  }

  // Format sizes describe tightly packed source bytes, not driver allocation sizes.
  inline uint32_t textureUploadBlockBytes(Format format) {
    switch (format) {
      case Format::BC1_RGB_UNORM_BLOCK:
        return 8;
      case Format::BC2_UNORM_BLOCK:
      case Format::BC7_UNORM_BLOCK:
      case Format::BC7_SRGB_BLOCK:
        return 16;
      default:
        break;
    }
    const auto f = static_cast<uint32_t>(format);
    const auto between = [f](Format first, Format last) {
      return f >= static_cast<uint32_t>(first) && f <= static_cast<uint32_t>(last);
    };
    if (between(Format::R8_UNORM, Format::R8_SINT))
      return 1;
    if (between(Format::R8G8_UNORM, Format::R8G8_SINT))
      return 2;
    if (between(Format::R8G8B8_UNORM, Format::R8G8B8_SINT))
      return 3;
    if (between(Format::R8G8B8A8_UNORM, Format::R8G8B8A8_SRGB))
      return 4;
    if (between(Format::R16_UNORM, Format::R16_SFLOAT))
      return 2;
    if (between(Format::R16G16_UNORM, Format::R16G16_SFLOAT))
      return 4;
    if (between(Format::R16G16B16A16_UNORM, Format::R16G16B16A16_SFLOAT))
      return 8;
    if (between(Format::R32_UINT, Format::R32_SFLOAT) || format == Format::D32_SFLOAT)
      return 4;
    if (between(Format::R32G32_UINT, Format::R32G32_SFLOAT))
      return 8;
    if (between(Format::R32G32B32_UINT, Format::R32G32B32_SFLOAT))
      return 12;
    if (between(Format::R32G32B32A32_UINT, Format::R32G32B32A32_SFLOAT))
      return 16;
    throw std::invalid_argument("Unsupported texture upload format or packed depth/stencil");
  }

  inline uint64_t checkedTextureMultiply(uint64_t left, uint64_t right) {
    if (right && left > std::numeric_limits<uint64_t>::max() / right)
      throw std::overflow_error("Texture upload byte count overflow");
    return left * right;
  }

  // Validate before allocating a staging buffer or recording any GPU commands.
  // Upload starts at mip zero; a prefix is allowed by the existing RHI API.
  inline TextureMipUploadLayout planTextureMipUpload(Format format,
                                                     Extent3D extent,
                                                     uint32_t textureMips,
                                                     uint32_t layers,
                                                     uint32_t layer,
                                                     const std::vector<RHIDevice::MipData>& mips) {
    if (!extent.width || !extent.height || !extent.depth || !textureMips || !layers || layer >= layers ||
        mips.empty() || mips.size() > textureMips)
      throw std::invalid_argument("Texture upload subresource range invalid");
    uint32_t maxMips = 1;
    for (auto n = std::max({extent.width, extent.height, extent.depth}); n > 1; n /= 2)
      ++maxMips;
    if (textureMips > maxMips)
      throw std::invalid_argument("Texture mip count exceeds extent");
    const uint32_t block = textureUploadBlockBytes(format);
    const uint32_t alignment = std::lcm(4u, block);
    const bool compressed = isBlockCompressedFormat(format);
    if (compressed && extent.depth != 1)
      throw std::invalid_argument("Compressed upload requires depth one");
    TextureMipUploadLayout result;
    for (const auto& mip : mips) {
      const uint64_t columns = compressed ? (uint64_t(extent.width) + 3) / 4 : extent.width;
      const uint64_t rows = compressed ? (uint64_t(extent.height) + 3) / 4 : extent.height;
      const auto size =
          checkedTextureMultiply(checkedTextureMultiply(checkedTextureMultiply(columns, rows), extent.depth), block);
      if (!mip.data || mip.width != extent.width || mip.height != extent.height || mip.depth != extent.depth ||
          mip.dataSize != size)
        throw std::invalid_argument("Texture mip dimensions, bytes or data pointer invalid");
      const uint64_t padding = (alignment - result.bytes % alignment) % alignment;
      if (result.bytes > std::numeric_limits<uint64_t>::max() - padding ||
          size > std::numeric_limits<uint64_t>::max() - result.bytes - padding)
        throw std::overflow_error("Texture upload offset overflow");
      result.bytes += padding;
      result.slices.push_back({result.bytes, size});
      result.bytes += size;
      extent.width = std::max(1u, extent.width / 2);
      extent.height = std::max(1u, extent.height / 2);
      extent.depth = std::max(1u, extent.depth / 2);
    }
    return result;
  }
} // namespace HongTu::RHI
