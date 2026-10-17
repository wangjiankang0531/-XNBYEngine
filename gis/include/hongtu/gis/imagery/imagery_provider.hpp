#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>

#include <string>

namespace hongtu::gis {

  using namespace hongtu;

  /// Tile-edge bit mask used to request source-boundary feathering.
  ///
  /// L/R/T/B follow the same order as renderer tile edge scales. Providers
  /// that compose more than one backing source can mark an edge when the
  /// same-LOD neighbor resolves to a different source. The renderer then
  /// fades both sides through a common loaded ancestor instead of exposing a
  /// hard rectangular provider boundary.
  enum TileSourceTransitionEdge : u8 {
    TileSourceTransitionNone = 0,
    TileSourceTransitionLeft = 1u << 0u,
    TileSourceTransitionRight = 1u << 1u,
    TileSourceTransitionTop = 1u << 2u,
    TileSourceTransitionBottom = 1u << 3u,
  };

  /// Abstract interface for tile imagery providers.
  ///
  /// Implementations build URLs for fetching map tile images at given coordinates.
  class ImageryProvider {
  public:
    virtual ~ImageryProvider() = default;

    /// Build the URL for fetching the tile image at the given coordinate.
    virtual std::string buildTileUrl(const TileCoord& coord) const = 0;

    /// Get the minimum supported zoom level.
    virtual i32 getMinLevel() const = 0;

    /// Get the maximum supported zoom level.
    virtual i32 getMaxLevel() const = 0;

    /// Get the tile size in pixels (typically 256).
    virtual i32 getTileSize() const = 0;

    /// Get the human-readable name of this provider.
    virtual std::string getName() const = 0;

    /// Stable identity used to isolate persistent tile-cache entries.
    ///
    /// Providers that can point at more than one backing store (for example a
    /// local directory provider) should override this and include that backing
    /// store in the identity.  The default is intentionally human-readable and
    /// keeps existing remote providers source-compatible.
    virtual std::string getCacheNamespace() const {
      return getName();
    }

    /// Return whether this provider can supply the requested tile.
    ///
    /// Remote providers normally cover every tile between their advertised
    /// levels.  Sparse/local providers override this so a routed provider can
    /// fall through to another source instead of issuing a known-bad URL.
    virtual bool supportsTile(const TileCoord& coord) const {
      return coord.isValid() && coord.z >= getMinLevel() && coord.z <= getMaxLevel();
    }

    /// Return same-LOD edges whose neighbor resolves to another backing
    /// imagery source. Ordinary single-source providers have no transitions.
    virtual u8 getSourceTransitionEdges(const TileCoord& coord) const {
      (void)coord;
      return TileSourceTransitionNone;
    }
  };

} // namespace hongtu::gis
