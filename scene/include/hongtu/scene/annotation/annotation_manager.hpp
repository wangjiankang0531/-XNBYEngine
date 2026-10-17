#pragma once

#include <hongtu/scene/annotation/annotation.hpp>

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace hongtu::scene {

/// Owns a collection of `Annotation` instances and serializes them all to
/// one JSON document (see spec §5 schema).
///
/// Mixed Geo + Local annotations are allowed in the same manager — scenario
/// JSON files typically pick one mode for consistency, but at runtime the
/// editor / DrawingTool can produce either.
///
/// IDs are auto-assigned by `add()`. Calling `add()` with an annotation that
/// already has an id != 0 overrides the auto-assign and uses that id (the
/// `next_id_` cursor advances past it).
class AnnotationManager {
 public:
  AnnotationManager() = default;

  AnnotationManager(const AnnotationManager&)            = delete;
  AnnotationManager& operator=(const AnnotationManager&) = delete;
  AnnotationManager(AnnotationManager&&) noexcept            = default;
  AnnotationManager& operator=(AnnotationManager&&) noexcept = default;

  /// Add an annotation, returning its assigned id.
  uint64_t add(std::unique_ptr<Annotation> a);

  /// Add ONE annotation from a single element JSON ({kind, mode, vertices,
  /// style, ...} — same per-element schema as toJson's array entries).
  /// Unlike fromJson this does NOT clear existing. Returns the assigned id,
  /// or 0 on schema error. Used by the cloud render server's dynamic-scene
  /// command bus for incremental adds.
  uint64_t addFromJson(const nlohmann::json& element);

  /// Remove by id. Returns true if found.
  bool remove(uint64_t id);

  /// Lookup. Returns nullptr if not found.
  Annotation*       get(uint64_t id);
  const Annotation* get(uint64_t id) const;

  /// List all annotations (stable order: insertion order is not guaranteed).
  std::vector<Annotation*> all();
  std::vector<const Annotation*> all() const;

  void   clear();
  size_t size() const { return items_.size(); }
  bool   empty() const { return items_.empty(); }

  /// Serialize the entire collection to JSON. Schema:
  ///   { "version": 1, "annotations": [ ... ] }
  nlohmann::json toJson() const;

  /// Replace contents from JSON. Returns true on success, false on schema
  /// error (unknown kind / mode, malformed shape). On failure the manager
  /// is left in a partially-populated state — caller should clear first if
  /// atomic semantics are needed.
  bool fromJson(const nlohmann::json& j);

  /// Build render payloads for every visible annotation against `origin`.
  /// Demo / renderer side then dispatches by `kind` to the appropriate
  /// renderer(PolylineRenderer / PolygonRenderer / BillboardRenderer).
  std::vector<AnnotationRenderPayload>
      buildRenderPayloads(const coordinate::PrecisionOrigin& origin) const;

 private:
  std::unordered_map<uint64_t, std::unique_ptr<Annotation>> items_;
  uint64_t next_id_ = 1;
};

}  // namespace hongtu::scene
