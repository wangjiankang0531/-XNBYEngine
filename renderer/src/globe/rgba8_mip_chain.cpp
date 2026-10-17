#include <hongtu/renderer/globe/rgba8_mip_chain.hpp>

#include <algorithm>
#include <limits>

namespace hongtu::renderer {

  std::vector<Rgba8MipLevel> buildRgba8MipChain(u32 width, u32 height, const std::vector<u8>& rgba) {
    std::vector<Rgba8MipLevel> result;
    constexpr std::size_t kChannels = 4;
    if (width == 0 || height == 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / height ||
        static_cast<std::size_t>(width) * height > std::numeric_limits<std::size_t>::max() / kChannels ||
        rgba.size() != static_cast<std::size_t>(width) * height * kChannels) {
      return result;
    }

    result.push_back({width, height, rgba});
    while (result.back().width > 1 || result.back().height > 1) {
      const Rgba8MipLevel& source = result.back();
      const u32 target_width = std::max(1u, source.width / 2u);
      const u32 target_height = std::max(1u, source.height / 2u);
      Rgba8MipLevel target;
      target.width = target_width;
      target.height = target_height;
      target.pixels.resize(static_cast<std::size_t>(target_width) * target_height * kChannels);

      for (u32 y = 0; y < target_height; ++y) {
        const u32 source_y_begin = y * source.height / target_height;
        const u32 source_y_end = std::max(source_y_begin + 1u, (y + 1u) * source.height / target_height);
        for (u32 x = 0; x < target_width; ++x) {
          const u32 source_x_begin = x * source.width / target_width;
          const u32 source_x_end = std::max(source_x_begin + 1u, (x + 1u) * source.width / target_width);
          const u32 sample_count = (source_x_end - source_x_begin) * (source_y_end - source_y_begin);
          const std::size_t destination = (static_cast<std::size_t>(y) * target_width + x) * kChannels;
          for (u32 channel = 0; channel < kChannels; ++channel) {
            u32 sum = 0;
            for (u32 source_y = source_y_begin; source_y < source_y_end; ++source_y) {
              for (u32 source_x = source_x_begin; source_x < source_x_end; ++source_x) {
                const std::size_t source_offset =
                    (static_cast<std::size_t>(source_y) * source.width + source_x) * kChannels;
                sum += source.pixels[source_offset + channel];
              }
            }
            target.pixels[destination + channel] = static_cast<u8>((sum + sample_count / 2u) / sample_count);
          }
        }
      }
      result.push_back(std::move(target));
    }
    return result;
  }

} // namespace hongtu::renderer
