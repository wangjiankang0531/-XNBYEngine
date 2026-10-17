#pragma once

#include <hongtu/scene/scene_graph.hpp>

#include <memory>
#include <optional>
#include <string>

namespace hongtu::scene {

/// Loads / saves a `.scene.json` describing all dynamic scene primitives
/// (emitters / annotations / etc) plus a timeline that drives them.
///
/// Schema is documented in `docs/scene-schema.md`. P0 supports:
///   - `version`(must == 1)
///   - `mode`(\"geo\" | \"local\")
///   - `annotations`: delegated to `AnnotationManager::fromJson` (covers
///     all 5 kinds × 2 modes, with id + style + visibility)
///   - `emitters`: per-entry preset + position + emit_rate; populates
///     `SceneGraph` with `LocalEmitter` or `GeoEmitter` named by JSON `id`
///   - `timeline.duration_seconds` + `timeline.tracks`:
///     * `"<emitter_name>.enabled"` bool → wires CallbackDriver to flip
///       the emitter's enabled flag (other tracks are warned-and-skipped)
///
/// **Not in P0** (note + TODO in spec §8):
///   - static_meshes (.glb references)
///   - shockwaves / volumes / agents
///   - generic field reflection beyond `.enabled`
///
/// Caller pattern:
///   SceneLoader l;
///   auto sg = l.load("data/scenes/foo.scene.json");
///   if (!sg) { std::cerr << l.lastError(); return; }
///   for (auto* e : (*sg)->all<LocalEmitter>()) { ... }
///   (*sg)->timeline().update(dt);
class SceneLoader {
 public:
  /// Parse + build a SceneGraph from JSON file at `json_path`. Returns
  /// `nullopt` on schema error (`lastError()` populated).
  std::optional<std::unique_ptr<SceneGraph>> load(const std::string& json_path);

  /// Inverse of `load` — serialize the SceneGraph back to JSON. Returns
  /// false on I/O failure.
  bool save(const SceneGraph& sg, const std::string& json_path) const;

  const std::string& lastError() const { return last_error_; }

 private:
  mutable std::string last_error_;
};

}  // namespace hongtu::scene
