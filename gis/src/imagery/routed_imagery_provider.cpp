#include <hongtu/gis/imagery/routed_imagery_provider.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hongtu::gis {
  namespace {

    constexpr f64 kDegreesToRadians = 3.14159265358979323846 / 180.0;

  } // namespace

  RoutedImageryProvider::RoutedImageryProvider(std::string name, std::shared_ptr<ImageryProvider> fallback)
      : name_(std::move(name)), fallback_(std::move(fallback)) {
    if (name_.empty()) {
      name_ = "RoutedImagery";
    }
  }

  GeoBounds RoutedImageryProvider::boundsFromDegrees(f64 west, f64 south, f64 east, f64 north) {
    return {west * kDegreesToRadians, south * kDegreesToRadians, east * kDegreesToRadians, north * kDegreesToRadians};
  }

  bool RoutedImageryProvider::addRoute(Route route) {
    if (!route.provider || route.name.empty() || route.coverage.west > route.coverage.east ||
        route.coverage.south > route.coverage.north) {
      return false;
    }

    route.min_level = std::max(route.min_level, route.provider->getMinLevel());
    route.max_level = std::min(route.max_level, route.provider->getMaxLevel());
    if (route.min_level > route.max_level) {
      return false;
    }

    routes_.push_back(std::move(route));
    return true;
  }

  RoutedImageryProvider::Selection RoutedImageryProvider::select(const TileCoord& coord) const {
    if (!coord.isValid()) {
      return {};
    }

    const GeoBounds tile_bounds = coord.toBounds();
    f64 lat = 0.0;
    f64 lon = 0.0;
    tile_bounds.center(lat, lon);

    const Route* best = nullptr;
    for (const auto& route : routes_) {
      if (coord.z < route.min_level || coord.z > route.max_level || !route.coverage.contains(lat, lon) ||
          !route.provider->supportsTile(coord)) {
        continue;
      }
      if (!best || route.priority > best->priority ||
          (route.priority == best->priority && route.min_level > best->min_level)) {
        best = &route;
      }
    }

    if (best) {
      return {best, best->provider};
    }
    if (fallback_ && fallback_->supportsTile(coord)) {
      return {nullptr, fallback_};
    }
    return {};
  }

  std::string RoutedImageryProvider::buildTileUrl(const TileCoord& coord) const {
    const Selection selection = select(coord);
    if (!selection.provider) {
      return {};
    }
    if (selection.route) {
      selection.route->request_count->fetch_add(1, std::memory_order_relaxed);
    } else {
      fallback_request_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return selection.provider->buildTileUrl(coord);
  }

  i32 RoutedImageryProvider::getMinLevel() const {
    i32 level = fallback_ ? fallback_->getMinLevel() : std::numeric_limits<i32>::max();
    for (const auto& route : routes_) {
      level = std::min(level, route.min_level);
    }
    return level == std::numeric_limits<i32>::max() ? 0 : level;
  }

  i32 RoutedImageryProvider::getMaxLevel() const {
    i32 level = fallback_ ? fallback_->getMaxLevel() : 0;
    for (const auto& route : routes_) {
      level = std::max(level, route.max_level);
    }
    return level;
  }

  i32 RoutedImageryProvider::getTileSize() const {
    if (fallback_) {
      return fallback_->getTileSize();
    }
    return routes_.empty() ? 256 : routes_.front().provider->getTileSize();
  }

  bool RoutedImageryProvider::supportsTile(const TileCoord& coord) const {
    return select(coord).provider != nullptr;
  }

  u8 RoutedImageryProvider::getSourceTransitionEdges(const TileCoord& coord) const {
    const Selection current = select(coord);
    if (!current.provider) {
      return TileSourceTransitionNone;
    }

    const i32 axis_tiles = TileCoord::maxTilesAtLevel(coord.z);
    if (axis_tiles <= 0) {
      return TileSourceTransitionNone;
    }

    u8 edges = TileSourceTransitionNone;
    auto sourceDiffers = [&current, this](const TileCoord& neighbor) {
      const Selection adjacent = select(neighbor);
      return adjacent.provider &&
             (adjacent.route != current.route || adjacent.provider.get() != current.provider.get());
    };

    TileCoord neighbor = coord;
    neighbor.x = coord.x == 0 ? axis_tiles - 1 : coord.x - 1;
    if (sourceDiffers(neighbor)) {
      edges |= TileSourceTransitionLeft;
    }

    neighbor = coord;
    neighbor.x = coord.x + 1 == axis_tiles ? 0 : coord.x + 1;
    if (sourceDiffers(neighbor)) {
      edges |= TileSourceTransitionRight;
    }

    if (coord.y > 0) {
      neighbor = coord;
      --neighbor.y;
      if (sourceDiffers(neighbor)) {
        edges |= TileSourceTransitionTop;
      }
    }
    if (coord.y + 1 < axis_tiles) {
      neighbor = coord;
      ++neighbor.y;
      if (sourceDiffers(neighbor)) {
        edges |= TileSourceTransitionBottom;
      }
    }
    return edges;
  }

  std::string RoutedImageryProvider::resolveRouteName(const TileCoord& coord) const {
    const Selection selection = select(coord);
    if (!selection.provider) {
      return {};
    }
    return selection.route ? selection.route->name : "fallback";
  }

  std::size_t RoutedImageryProvider::getRouteRequestCount(const std::string& route_name) const {
    const auto route = std::find_if(
        routes_.begin(), routes_.end(), [&](const Route& candidate) { return candidate.name == route_name; });
    return route == routes_.end() ? 0 : route->request_count->load(std::memory_order_relaxed);
  }

} // namespace hongtu::gis
