#include <gtest/gtest.h>

#include "hongtu/rhi/TextureUploadLayout.h"

using namespace HongTu::RHI;
namespace {
  uint8_t byte = 0;
  RHIDevice::MipData mip(uint32_t w, uint32_t h, size_t bytes, uint32_t d = 1) {
    return {w, h, d, &byte, bytes};
  }
  TEST(TextureUploadLayout, Bc7PartialBlocksAndTailAreRealBlocks) {
    const auto p = planTextureMipUpload(
        Format::BC7_UNORM_BLOCK, {5, 7, 1}, 3, 1, 0, {mip(5, 7, 64), mip(2, 3, 16), mip(1, 1, 16)});
    ASSERT_EQ(p.slices.size(), 3);
    EXPECT_EQ(p.bytes, 96);
    EXPECT_EQ(p.slices[1].offset, 64);
    EXPECT_EQ(p.slices[2].offset, 80);
  }
  TEST(TextureUploadLayout, Bc1BlocksAreEightBytesIncludingOnePixelMip) {
    const auto p = planTextureMipUpload(
        Format::BC1_RGB_UNORM_BLOCK, {5, 7, 1}, 3, 1, 0, {mip(5, 7, 32), mip(2, 3, 8), mip(1, 1, 8)});
    EXPECT_EQ(p.bytes, 48);
  }
  TEST(TextureUploadLayout, RealNonSquareMipChainHasAuditedSize) {
    std::vector<RHIDevice::MipData> mips;
    for (uint32_t w = 1040, h = 1560;; w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
      mips.push_back(mip(w, h, ((w + 3) / 4) * ((h + 3) / 4) * 16));
      if (w == 1 && h == 1)
        break;
    }
    const auto p = planTextureMipUpload(Format::BC7_SRGB_BLOCK, {1040, 1560, 1}, 11, 1, 0, mips);
    EXPECT_EQ(p.bytes, 2164672);
    EXPECT_EQ(p.slices.size(), 11);
  }
  TEST(TextureUploadLayout, SmallUncompressedMipOffsetsRequirePadding) {
    auto p = planTextureMipUpload(Format::R8_UNORM, {3, 1, 1}, 2, 1, 0, {mip(3, 1, 3), mip(1, 1, 1)});
    EXPECT_EQ(p.slices[1].offset, 4);
    EXPECT_EQ(p.bytes, 5);
    p = planTextureMipUpload(Format::R8G8B8_UNORM, {3, 1, 1}, 2, 1, 0, {mip(3, 1, 9), mip(1, 1, 3)});
    EXPECT_EQ(p.slices[1].offset, 12);
    EXPECT_EQ(p.bytes, 15);
  }
  TEST(TextureUploadLayout, VolumeMipsReduceDepth) {
    const auto p = planTextureMipUpload(Format::R32_SFLOAT, {2, 2, 2}, 2, 1, 0, {mip(2, 2, 32, 2), mip(1, 1, 4)});
    EXPECT_EQ(p.bytes, 36);
  }
  TEST(TextureUploadLayout, RejectsWrongMipDimensionsOrByteCount) {
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {5, 7, 1}, 3, 1, 0, {mip(5, 7, 64), mip(3, 3, 16)}),
                 std::invalid_argument);
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {1, 1, 1}, 1, 1, 0, {mip(1, 1, 4)}),
                 std::invalid_argument);
  }
  TEST(TextureUploadLayout, RejectsLayerRangeAndExcessMipCount) {
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {1, 1, 1}, 1, 1, 1, {mip(1, 1, 16)}),
                 std::invalid_argument);
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {1, 1, 1}, 2, 1, 0, {mip(1, 1, 16), mip(1, 1, 16)}),
                 std::invalid_argument);
  }
  TEST(TextureUploadLayout, RejectsNullDataAndUnknownFormat) {
    auto empty = mip(1, 1, 16);
    empty.data = nullptr;
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {1, 1, 1}, 1, 1, 0, {empty}), std::invalid_argument);
    EXPECT_THROW(planTextureMipUpload(Format::Count, {1, 1, 1}, 1, 1, 0, {mip(1, 1, 4)}), std::invalid_argument);
  }
  TEST(TextureUploadLayout, RejectsOverflowBeforeStagingAllocation) {
    EXPECT_THROW(checkedTextureMultiply(UINT64_MAX, 2), std::overflow_error);
    EXPECT_THROW(planTextureMipUpload(Format::R32G32B32A32_SFLOAT,
                                      {UINT32_MAX, UINT32_MAX, UINT32_MAX},
                                      1,
                                      1,
                                      0,
                                      {mip(UINT32_MAX, UINT32_MAX, 16, UINT32_MAX)}),
                 std::overflow_error);
  }
  TEST(TextureUploadLayout, RejectsPackedStencilAndCompressedVolume) {
    EXPECT_THROW(planTextureMipUpload(Format::D24_UNORM_S8_UINT, {1, 1, 1}, 1, 1, 0, {mip(1, 1, 4)}),
                 std::invalid_argument);
    EXPECT_THROW(planTextureMipUpload(Format::BC7_UNORM_BLOCK, {4, 4, 2}, 1, 1, 0, {mip(4, 4, 32, 2)}),
                 std::invalid_argument);
  }
} // namespace
