#pragma once
#include <hongtu/gis/imagery/imagery_provider.hpp>

#include <string>

namespace hongtu::gis {

  /// Loads imagery tiles from local files via file:// URLs.
  ///
  /// Expected layout: {base_dir}/{z}/{x}/{y}.{ext}
  /// Default extension is "jpg" (matches the scripts/download_tianditu.sh layout).
  ///
  /// The base directory may be relative — it is resolved to an absolute path
  /// at construction time so the URL works from any working directory.
  class LocalImageryProvider : public ImageryProvider {
  public:
    /// @param base_dir  Path to the tile directory (relative or absolute).
    /// @param ext       File extension without the dot (e.g. "jpg", "png").
    /// @param min_level Minimum zoom level present on disk.
    /// @param max_level Maximum zoom level present on disk.
    explicit LocalImageryProvider(const std::string& base_dir,
                                  const std::string& ext = "jpg",
                                  i32 min_level = 1,
                                  i32 max_level = 18);

    std::string buildTileUrl(const TileCoord& coord) const override;
    i32 getMinLevel() const override {
      return min_level_;
    }
    i32 getMaxLevel() const override {
      return max_level_;
    }
    i32 getTileSize() const override {
      return 256;
    }
    std::string getName() const override {
      return "LocalTiles";
    }
    std::string getCacheNamespace() const override;
    bool supportsTile(const TileCoord& coord) const override;

    /// Check whether the tile file exists on disk.
    bool tileExists(const TileCoord& coord) const;

  private:
    std::string base_dir_; ///< Absolute path
    std::string ext_;      ///< Extension without dot
    i32 min_level_;
    i32 max_level_;
  };

} // namespace hongtu::gis
