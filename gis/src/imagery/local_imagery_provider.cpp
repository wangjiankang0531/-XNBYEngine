#include <hongtu/gis/imagery/local_imagery_provider.hpp>

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace hongtu::gis {

  namespace fs = std::filesystem;

  LocalImageryProvider::LocalImageryProvider(const std::string& base_dir,
                                             const std::string& ext,
                                             i32 min_level,
                                             i32 max_level)
      : ext_(ext), min_level_(min_level), max_level_(max_level) {
    // Resolve to absolute so file:// URLs are valid regardless of cwd
    std::error_code ec;
    fs::path p = fs::absolute(base_dir, ec);
    base_dir_ = ec ? base_dir : p.lexically_normal().string();
  }

  std::string LocalImageryProvider::buildTileUrl(const TileCoord& coord) const {
    std::ostringstream url;
    // curl accepts both file:/// (three slashes, absolute path) and
    // bare file:// for non-host-prefixed paths.
    url << "file://" << base_dir_ << '/' << coord.z << '/' << coord.x << '/' << coord.y << '.' << ext_;
    return url.str();
  }

  bool LocalImageryProvider::tileExists(const TileCoord& coord) const {
    fs::path p = fs::path(base_dir_) / std::to_string(coord.z) / std::to_string(coord.x) /
                 (std::to_string(coord.y) + '.' + ext_);
    std::error_code ec;
    return fs::exists(p, ec) && fs::file_size(p, ec) > 0;
  }

  std::string LocalImageryProvider::getCacheNamespace() const {
    // std::hash is not required to be stable across processes or standard
    // library versions. FNV-1a gives the on-disk cache a deterministic identity
    // while avoiding a machine-specific absolute path as a directory name.
    const std::string identity =
        base_dir_ + '|' + ext_ + '|' + std::to_string(min_level_) + '|' + std::to_string(max_level_);
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : identity) {
      hash ^= c;
      hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << "LocalTiles-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
  }

  bool LocalImageryProvider::supportsTile(const TileCoord& coord) const {
    return ImageryProvider::supportsTile(coord) && tileExists(coord);
  }

} // namespace hongtu::gis
