#include <hongtu/gis/tiles3d_reader.hpp>
#include <nlohmann/json.hpp>

namespace hongtu::gis {

BoundingVolume Tileset3DReader::parseBoundingVolume(const nlohmann::json& j) {
  BoundingVolume bv;
  if (j.contains("region")) {
    bv.type = BoundingVolume::Type::Region;
    auto& r = j["region"];
    bv.region = {r[0], r[1], r[2], r[3], r[4], r[5]};
  } else if (j.contains("box")) {
    bv.type = BoundingVolume::Type::Box;
    auto& b = j["box"];
    bv.box.center = {b[0], b[1], b[2]};
    bv.box.half_axes[0] = {b[3], b[4], b[5]};
    bv.box.half_axes[1] = {b[6], b[7], b[8]};
    bv.box.half_axes[2] = {b[9], b[10], b[11]};
  } else if (j.contains("sphere")) {
    bv.type = BoundingVolume::Type::Sphere;
    auto& s = j["sphere"];
    bv.sphere.center = {s[0], s[1], s[2]};
    bv.sphere.radius = s[3];
  }
  return bv;
}

std::unique_ptr<Tile> Tileset3DReader::parseTile(const nlohmann::json& j) {
  auto tile = std::make_unique<Tile>();

  if (j.contains("boundingVolume")) {
    tile->bounding_volume = parseBoundingVolume(j["boundingVolume"]);
  }

  tile->geometric_error = j.value("geometricError", 0.0);

  if (j.contains("refine")) {
    std::string r = j["refine"].get<std::string>();
    tile->refine = (r == "ADD") ? Refinement::Add : Refinement::Replace;
  }

  if (j.contains("content")) {
    if (j["content"].contains("uri")) {
      tile->content_uri = j["content"]["uri"].get<std::string>();
    } else if (j["content"].contains("url")) {
      tile->content_uri = j["content"]["url"].get<std::string>();
    }
  }

  if (j.contains("transform")) {
    auto& t = j["transform"];
    if (t.size() == 16) {
      for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
          tile->transform[col][row] = t[col * 4 + row].get<f64>();
    }
  }

  if (j.contains("children")) {
    for (const auto& child : j["children"]) {
      tile->children.push_back(parseTile(child));
    }
  }

  return tile;
}

void Tileset3DReader::countTiles(const Tile* tile, u32 depth) {
  if (!tile) return;
  info_.tile_count++;
  info_.max_depth = std::max(info_.max_depth, depth);
  for (const auto& child : tile->children) {
    countTiles(child.get(), depth + 1);
  }
}

bool Tileset3DReader::parse(const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    return parse(j);
  } catch (const std::exception& e) {
    error_ = std::string("JSON parse error: ") + e.what();
    return false;
  }
}

bool Tileset3DReader::parse(const nlohmann::json& j) {
  info_ = {};
  root_.reset();
  error_.clear();

  try {
    // Asset info
    if (j.contains("asset")) {
      info_.version = j["asset"].value("version", "1.0");
    }

    info_.geometric_error = j.value("geometricError", 0.0);

    if (!j.contains("root")) {
      error_ = "No 'root' tile found";
      return false;
    }

    root_ = parseTile(j["root"]);
    info_.root_bounding_volume = root_->bounding_volume;

    // Count tiles
    info_.tile_count = 0;
    info_.max_depth = 0;
    countTiles(root_.get(), 0);

    return true;
  } catch (const std::exception& e) {
    error_ = std::string("3D Tiles parse error: ") + e.what();
    return false;
  }
}

std::vector<std::string> Tileset3DReader::getAllContentURIs() const {
  std::vector<std::string> uris;
  std::function<void(const Tile*)> collect = [&](const Tile* tile) {
    if (!tile) return;
    if (!tile->content_uri.empty()) {
      uris.push_back(tile->content_uri);
    }
    for (const auto& child : tile->children) {
      collect(child.get());
    }
  };
  collect(root_.get());
  return uris;
}

} // namespace hongtu::gis
