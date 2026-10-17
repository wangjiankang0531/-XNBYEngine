#include <gtest/gtest.h>
#include <hongtu/renderer/globe/rgba8_mip_chain.hpp>

using namespace hongtu;
using namespace hongtu::renderer;

TEST(Rgba8MipChain, RejectsInvalidInput) {
  EXPECT_TRUE(buildRgba8MipChain(0, 1, {}).empty());
  EXPECT_TRUE(buildRgba8MipChain(1, 1, {1, 2, 3}).empty());
}

TEST(Rgba8MipChain, BuildsCompletePowerOfTwoChain) {
  std::vector<u8> pixels(4u * 4u * 4u);
  for (u32 y = 0; y < 4; ++y) {
    for (u32 x = 0; x < 4; ++x) {
      const std::size_t offset = (y * 4u + x) * 4u;
      const u8 value = static_cast<u8>(y * 40u + x * 4u);
      pixels[offset] = value;
      pixels[offset + 1] = static_cast<u8>(value + 1u);
      pixels[offset + 2] = static_cast<u8>(value + 2u);
      pixels[offset + 3] = 255u;
    }
  }

  const auto chain = buildRgba8MipChain(4, 4, pixels);
  ASSERT_EQ(chain.size(), 3u);
  EXPECT_EQ(chain[0].width, 4u);
  EXPECT_EQ(chain[1].width, 2u);
  EXPECT_EQ(chain[1].height, 2u);
  EXPECT_EQ(chain[2].width, 1u);
  EXPECT_EQ(chain[2].height, 1u);
  EXPECT_EQ(chain[1].pixels[0], 22u);
  EXPECT_EQ(chain[2].pixels[0], 66u);
  EXPECT_EQ(chain[2].pixels[1], 67u);
  EXPECT_EQ(chain[2].pixels[2], 68u);
  EXPECT_EQ(chain[2].pixels[3], 255u);
}

TEST(Rgba8MipChain, IncludesEveryPixelForOddDimensions) {
  std::vector<u8> pixels(3u * 3u * 4u, 0u);
  for (std::size_t pixel = 0; pixel < 9u; ++pixel) {
    pixels[pixel * 4u] = static_cast<u8>(pixel);
    pixels[pixel * 4u + 3u] = 255u;
  }

  const auto chain = buildRgba8MipChain(3, 3, pixels);
  ASSERT_EQ(chain.size(), 2u);
  EXPECT_EQ(chain[1].width, 1u);
  EXPECT_EQ(chain[1].height, 1u);
  EXPECT_EQ(chain[1].pixels[0], 4u);
  EXPECT_EQ(chain[1].pixels[3], 255u);
}
