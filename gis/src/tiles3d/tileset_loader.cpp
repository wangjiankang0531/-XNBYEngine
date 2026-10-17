#include <hongtu/gis/tiles3d/tileset_loader.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace hongtu::gis {

using json = nlohmann::json;

namespace {

glm::dmat4 parseTransform(const json& j) {
  glm::dmat4 m(1.0);
  if (!j.is_array() || j.size() != 16) return m;
  // tileset.json uses column-major order
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      m[col][row] = j[col * 4 + row].get<double>();
  return m;
}

BoundingVolume parseBoundingVolume(const json& bv) {
  BoundingVolume vol;
  if (bv.contains("box") && bv["box"].is_array() && bv["box"].size() == 12) {
    const auto& b = bv["box"];
    vol.center      = {b[0].get<double>(), b[1].get<double>(), b[2].get<double>()};
    // half-axes: take the max component of each column as a conservative AABB
    glm::dvec3 hx = {b[3].get<double>(), b[4].get<double>(), b[5].get<double>()};
    glm::dvec3 hy = {b[6].get<double>(), b[7].get<double>(), b[8].get<double>()};
    glm::dvec3 hz = {b[9].get<double>(), b[10].get<double>(), b[11].get<double>()};
    vol.half_extents = {glm::length(hx), glm::length(hy), glm::length(hz)};
    vol.is_sphere = false;
  } else if (bv.contains("sphere") && bv["sphere"].is_array() && bv["sphere"].size() == 4) {
    const auto& s = bv["sphere"];
    vol.center        = {s[0].get<double>(), s[1].get<double>(), s[2].get<double>()};
    vol.sphere_radius = s[3].get<double>();
    vol.is_sphere     = true;
  } else if (bv.contains("region") && bv["region"].is_array() && bv["region"].size() == 6) {
    // region: [west, south, east, north, minH, maxH] in radians/meters
    // Approximate as sphere for LOD purposes
    const auto& r = bv["region"];
    double cx = (r[0].get<double>() + r[2].get<double>()) * 0.5;
    double cy = (r[1].get<double>() + r[3].get<double>()) * 0.5;
    double cz = (r[4].get<double>() + r[5].get<double>()) * 0.5;
    vol.center = {cx, cy, cz};
    double dx = (r[2].get<double>() - r[0].get<double>()) * 0.5;
    double dy = (r[3].get<double>() - r[1].get<double>()) * 0.5;
    vol.sphere_radius = glm::length(glm::dvec2(dx, dy));
    vol.is_sphere = true;
  }
  return vol;
}

std::unique_ptr<TilesetNode> parseTile(const json& j) {
  auto node = std::make_unique<TilesetNode>();

  if (j.contains("boundingVolume") && j["boundingVolume"].is_object())
    node->bounding_volume = parseBoundingVolume(j["boundingVolume"]);

  if (j.contains("geometricError") && j["geometricError"].is_number())
    node->geometric_error = j["geometricError"].get<double>();

  if (j.contains("refine") && j["refine"].is_string())
    node->refine = j["refine"].get<std::string>();

  if (j.contains("transform") && j["transform"].is_array())
    node->transform = parseTransform(j["transform"]);

  if (j.contains("content") && j["content"].is_object()) {
    const auto& c = j["content"];
    if (c.contains("uri") && c["uri"].is_string())
      node->content_uri = c["uri"].get<std::string>();
    else if (c.contains("url") && c["url"].is_string())  // legacy field name
      node->content_uri = c["url"].get<std::string>();
  }

  if (j.contains("children") && j["children"].is_array())
    for (const auto& child : j["children"])
      if (child.is_object())
        node->children.push_back(parseTile(child));

  return node;
}

} // namespace

std::unique_ptr<TilesetNode> TilesetLoader::parse(const std::string& json_text) {
  try {
    auto j = json::parse(json_text);
    if (!j.is_object() || !j.contains("root")) {
      spdlog::error("TilesetLoader: no 'root' in tileset.json");
      return nullptr;
    }
    return parseTile(j["root"]);
  } catch (const json::exception& e) {
    spdlog::error("TilesetLoader: JSON parse error: {}", e.what());
    return nullptr;
  }
}

} // namespace hongtu::gis
