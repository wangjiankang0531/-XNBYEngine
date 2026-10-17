#include <hongtu/scene/annotation/annotation_manager.hpp>

#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>

#include "annotation_json.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace hongtu::scene {

namespace {

/// Build an empty annotation of (kind, mode) — the .cpp side knows all 10
/// concrete classes so the manager doesn't need to expose them.
std::unique_ptr<Annotation> makeEmpty(AnnotationKind k, AnnotationMode m) {
  switch (k) {
    case AnnotationKind::Polyline:
      return m == AnnotationMode::Geo
          ? std::unique_ptr<Annotation>(std::make_unique<GeoPolylineAnnotation>())
          : std::unique_ptr<Annotation>(std::make_unique<LocalPolylineAnnotation>());
    case AnnotationKind::Polygon:
      return m == AnnotationMode::Geo
          ? std::unique_ptr<Annotation>(std::make_unique<GeoPolygonAnnotation>())
          : std::unique_ptr<Annotation>(std::make_unique<LocalPolygonAnnotation>());
    case AnnotationKind::Symbol:
      return m == AnnotationMode::Geo
          ? std::unique_ptr<Annotation>(std::make_unique<GeoSymbolAnnotation>())
          : std::unique_ptr<Annotation>(std::make_unique<LocalSymbolAnnotation>());
    case AnnotationKind::Text:
      return m == AnnotationMode::Geo
          ? std::unique_ptr<Annotation>(std::make_unique<GeoTextAnnotation>())
          : std::unique_ptr<Annotation>(std::make_unique<LocalTextAnnotation>());
    case AnnotationKind::Arrow:
      return m == AnnotationMode::Geo
          ? std::unique_ptr<Annotation>(std::make_unique<GeoArrowAnnotation>())
          : std::unique_ptr<Annotation>(std::make_unique<LocalArrowAnnotation>());
  }
  return nullptr;
}

bool parseKind(const std::string& s, AnnotationKind& out) {
  if (s == "polyline") { out = AnnotationKind::Polyline; return true; }
  if (s == "polygon")  { out = AnnotationKind::Polygon;  return true; }
  if (s == "symbol")   { out = AnnotationKind::Symbol;   return true; }
  if (s == "text")     { out = AnnotationKind::Text;     return true; }
  if (s == "arrow")    { out = AnnotationKind::Arrow;    return true; }
  return false;
}

bool parseMode(const std::string& s, AnnotationMode& out) {
  if (s == "geo")   { out = AnnotationMode::Geo;   return true; }
  if (s == "local") { out = AnnotationMode::Local; return true; }
  return false;
}

}  // namespace

uint64_t AnnotationManager::add(std::unique_ptr<Annotation> a) {
  if (!a) return 0;
  uint64_t id = a->id();
  if (id == 0) {
    id = next_id_++;
    a->setId(id);
  } else if (id >= next_id_) {
    next_id_ = id + 1;
  }
  items_[id] = std::move(a);
  return id;
}

uint64_t AnnotationManager::addFromJson(const nlohmann::json& element) {
  if (!element.is_object() ||
      !element.contains("kind") || !element.contains("mode")) {
    return 0;
  }
  AnnotationKind k;
  AnnotationMode m;
  if (!parseKind(element["kind"].get<std::string>(), k) ||
      !parseMode(element["mode"].get<std::string>(), m)) {
    return 0;
  }
  auto a = makeEmpty(k, m);
  if (!a) return 0;
  a->fromJson(element);
  return add(std::move(a));
}

bool AnnotationManager::remove(uint64_t id) {
  return items_.erase(id) > 0;
}

Annotation* AnnotationManager::get(uint64_t id) {
  auto it = items_.find(id);
  return it == items_.end() ? nullptr : it->second.get();
}

const Annotation* AnnotationManager::get(uint64_t id) const {
  auto it = items_.find(id);
  return it == items_.end() ? nullptr : it->second.get();
}

std::vector<Annotation*> AnnotationManager::all() {
  std::vector<Annotation*> out;
  out.reserve(items_.size());
  for (auto& [_, a] : items_) out.push_back(a.get());
  return out;
}

std::vector<const Annotation*> AnnotationManager::all() const {
  std::vector<const Annotation*> out;
  out.reserve(items_.size());
  for (const auto& [_, a] : items_) out.push_back(a.get());
  return out;
}

void AnnotationManager::clear() {
  items_.clear();
  next_id_ = 1;
}

nlohmann::json AnnotationManager::toJson() const {
  nlohmann::json j;
  j["version"] = 1;
  auto& arr = j["annotations"] = nlohmann::json::array();
  // Stable insertion-order-ish: sort by id so file diffs are predictable.
  std::vector<uint64_t> ids;
  ids.reserve(items_.size());
  for (const auto& [id, _] : items_) ids.push_back(id);
  std::sort(ids.begin(), ids.end());
  for (uint64_t id : ids) {
    arr.push_back(items_.at(id)->toJson());
  }
  return j;
}

bool AnnotationManager::fromJson(const nlohmann::json& j) {
  clear();
  if (!j.is_object() || !j.contains("annotations")) {
    spdlog::warn("AnnotationManager::fromJson: missing 'annotations' array");
    return false;
  }
  const auto& arr = j["annotations"];
  if (!arr.is_array()) return false;

  bool all_ok = true;
  for (const auto& item : arr) {
    if (!item.is_object()) { all_ok = false; continue; }
    if (!item.contains("kind") || !item.contains("mode")) { all_ok = false; continue; }

    AnnotationKind k;
    AnnotationMode m;
    if (!parseKind(item["kind"].get<std::string>(), k) ||
        !parseMode(item["mode"].get<std::string>(), m)) {
      spdlog::warn("AnnotationManager::fromJson: unknown kind/mode '{}/{}'",
                   item.value("kind", ""), item.value("mode", ""));
      all_ok = false;
      continue;
    }
    auto a = makeEmpty(k, m);
    if (!a) { all_ok = false; continue; }
    a->fromJson(item);
    add(std::move(a));
  }
  return all_ok;
}

std::vector<AnnotationRenderPayload>
AnnotationManager::buildRenderPayloads(const coordinate::PrecisionOrigin& origin) const {
  std::vector<AnnotationRenderPayload> out;
  out.reserve(items_.size());
  for (const auto& [_, a] : items_) {
    if (!a->visible()) continue;
    out.push_back(a->toRenderPayload(origin));
  }
  return out;
}

}  // namespace hongtu::scene
