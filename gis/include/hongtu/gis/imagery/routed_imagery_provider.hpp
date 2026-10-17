#pragma once

#include <hongtu/gis/imagery/imagery_provider.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace hongtu::gis {

  /// Composes sparse regional/offline imagery with an optional fallback source.
  ///
  /// Routes are selected by zoom, geographic coverage and priority. A route is
  /// skipped when its provider reports that the tile is unavailable, which lets
  /// sparse LocalImageryProvider directories fall through cleanly to a remote
  /// service. This keeps GlobeRenderer's downloader, LRU and quadtree unchanged:
  /// from its point of view the composition is one ordinary imagery provider.
  class RoutedImageryProvider : public ImageryProvider {
  public:
    struct Route {
      Route() = default;
      Route(std::string route_name,
            GeoBounds route_coverage,
            i32 route_min_level,
            i32 route_max_level,
            i32 route_priority,
            std::shared_ptr<ImageryProvider> route_provider)
          : name(std::move(route_name)), coverage(route_coverage), min_level(route_min_level),
            max_level(route_max_level), priority(route_priority), provider(std::move(route_provider)) {
      }

      std::string name;
      GeoBounds coverage;
      i32 min_level = 0;
      i32 max_level = 0;
      i32 priority = 0;
      std::shared_ptr<ImageryProvider> provider;

    private:
      friend class RoutedImageryProvider;
      std::shared_ptr<std::atomic_size_t> request_count = std::make_shared<std::atomic_size_t>(0);
    };

    explicit RoutedImageryProvider(std::string name, std::shared_ptr<ImageryProvider> fallback = nullptr);

    /// Convenience helper for non-dateline-crossing bounds.
    static GeoBounds boundsFromDegrees(f64 west, f64 south, f64 east, f64 north);

    /// Add a route. Its effective level range is intersected with the provider's
    /// own advertised range. Invalid routes are rejected and return false.
    bool addRoute(Route route);

    std::string buildTileUrl(const TileCoord& coord) const override;
    i32 getMinLevel() const override;
    i32 getMaxLevel() const override;
    i32 getTileSize() const override;
    std::string getName() const override {
      return name_;
    }
    std::string getCacheNamespace() const override {
      return name_;
    }
    bool supportsTile(const TileCoord& coord) const override;
    u8 getSourceTransitionEdges(const TileCoord& coord) const override;

    /// Resolve without incrementing request telemetry. Returns "fallback" or an
    /// empty string when no source can provide the coordinate.
    std::string resolveRouteName(const TileCoord& coord) const;
    std::size_t getRouteRequestCount(const std::string& route_name) const;
    std::size_t getFallbackRequestCount() const {
      return fallback_request_count_.load();
    }
    std::size_t getRouteCount() const {
      return routes_.size();
    }

  private:
    struct Selection {
      const Route* route = nullptr;
      std::shared_ptr<ImageryProvider> provider;
    };

    Selection select(const TileCoord& coord) const;

    std::string name_;
    std::shared_ptr<ImageryProvider> fallback_;
    std::vector<Route> routes_;
    mutable std::atomic_size_t fallback_request_count_{0};
  };

} // namespace hongtu::gis
