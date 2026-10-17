#pragma once

#include <hongtu/scene/annotation/annotation_manager.hpp>
#include <hongtu/scene/timeline/timeline.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hongtu::scene {

/// Per-scene container with stable id-based addressing for any scene
/// primitive — not just `scene::Entity` subclasses.
///
/// The scene has multiple parallel base hierarchies that don't share a
/// common ancestor (Entity / EmitterEntity / VolumetricEntity /
/// ShockwaveEntity / PolylineEntity / BillboardEntity / Annotation). Rather
/// than force them into a single base, SceneGraph stores everything
/// type-erased(`shared_ptr<void>` + `std::type_index`)so any movable C++
/// type can be registered. Type safety is preserved at the API boundary
/// via `getAs<T>` and `all<T>` — both validate `type_index` at lookup.
///
/// Observers reference primitives via `EntityId` (a stable uint64_t handle)
/// rather than raw pointers. This way streaming load/unload can move
/// primitives in/out without invalidating long-lived references — the id
/// is the contract. An optional `name` alias lets the editor / scene JSON /
/// runtime control API address primitives by string when convenient.
///
/// Co-resident: `AnnotationManager` (it has its own id scheme + JSON
/// serializer already) and `Timeline` (CallbackDriver-based, references
/// primitives by closures the demo/loader supplies).
class SceneGraph {
 public:
  using EntityId = uint64_t;
  static constexpr EntityId kInvalidId = 0;

  SceneGraph() = default;
  SceneGraph(const SceneGraph&)            = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;
  SceneGraph(SceneGraph&&) noexcept            = default;
  SceneGraph& operator=(SceneGraph&&) noexcept = default;

  // ---------------------------------------------------------------------------
  // Creation
  // ---------------------------------------------------------------------------

  /// Construct a new primitive of type T with a fresh auto-incremented id +
  /// optional display name, return a non-owning observer pointer.
  template <typename T, typename... Args>
  T* create(const std::string& name, Args&&... args) {
    EntityId id = next_id_++;
    return registerPrimitive<T>(id, name, std::forward<Args>(args)...);
  }

  /// Same as `create` but with a caller-specified id. Used by SceneLoader
  /// when JSON already names the id. Returns nullptr if `id == kInvalidId`
  /// or if the id is already taken; otherwise advances `next_id_` past `id`
  /// to keep auto-assignment monotonic.
  template <typename T, typename... Args>
  T* createWithId(EntityId id, const std::string& name, Args&&... args) {
    if (id == kInvalidId) return nullptr;
    if (entities_.find(id) != entities_.end()) return nullptr;
    if (id >= next_id_) next_id_ = id + 1;
    return registerPrimitive<T>(id, name, std::forward<Args>(args)...);
  }

  // ---------------------------------------------------------------------------
  // Removal
  // ---------------------------------------------------------------------------

  bool remove(EntityId id);
  bool removeByName(const std::string& name);
  void clear();

  // ---------------------------------------------------------------------------
  // Lookup
  // ---------------------------------------------------------------------------

  /// Lookup membership + name without recovering a typed pointer. For typed
  /// access, callers should use `getAs<T>` (type safety guard).
  bool          contains(EntityId id) const;
  EntityId      idOfName(const std::string& name) const;
  std::string   nameOfId(EntityId id) const;

  /// Type-safe accessor. Returns nullptr if id not present or stored
  /// primitive has a different runtime type.
  template <typename T>
  T* getAs(EntityId id) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    if (it->second.type != std::type_index(typeid(T))) return nullptr;
    return static_cast<T*>(it->second.entity.get());
  }

  /// Type-safe access by name.
  template <typename T>
  T* getAsByName(const std::string& name) {
    auto id = idOfName(name);
    return id == kInvalidId ? nullptr : getAs<T>(id);
  }

  /// All primitives of type T (linear scan; O(N) — caller can cache if hot).
  template <typename T>
  std::vector<T*> all() {
    std::vector<T*> out;
    out.reserve(entities_.size());
    for (auto& [id, e] : entities_) {
      if (e.type == std::type_index(typeid(T))) {
        out.push_back(static_cast<T*>(e.entity.get()));
      }
    }
    return out;
  }

  /// Like `all<T>` but also returns the EntityId + name for each primitive.
  /// Used by serializers / editors that need to round-trip ids and names.
  template <typename T>
  struct EntryInfo {
    EntityId    id;
    std::string name;
    T*          ptr;
  };
  template <typename T>
  std::vector<EntryInfo<T>> allWithMeta() {
    std::vector<EntryInfo<T>> out;
    out.reserve(entities_.size());
    for (auto& [id, e] : entities_) {
      if (e.type == std::type_index(typeid(T))) {
        out.push_back({id, e.name, static_cast<T*>(e.entity.get())});
      }
    }
    return out;
  }

  std::size_t size() const  { return entities_.size(); }
  bool        empty() const { return entities_.empty(); }

  // ---------------------------------------------------------------------------
  // Co-resident systems
  // ---------------------------------------------------------------------------

  AnnotationManager&       annotations()       { return annotations_; }
  const AnnotationManager& annotations() const { return annotations_; }
  Timeline&                timeline()       { return timeline_; }
  const Timeline&          timeline() const { return timeline_; }

 private:
  struct Entry {
    /// Type-erased ownership — shared_ptr<void> preserves the original T's
    /// destructor via its type-erased deleter, even though we drop the
    /// static type here.
    std::shared_ptr<void> entity;
    std::string           name;
    std::type_index       type;
  };

  template <typename T, typename... Args>
  T* registerPrimitive(EntityId id, const std::string& name, Args&&... args) {
    auto owned = std::make_shared<T>(std::forward<Args>(args)...);
    T* raw = owned.get();
    entities_.emplace(id, Entry{std::shared_ptr<void>(owned), name,
                                  std::type_index(typeid(T))});
    if (!name.empty()) by_name_[name] = id;
    return raw;
  }

  std::unordered_map<EntityId, Entry>       entities_;
  std::unordered_map<std::string, EntityId> by_name_;
  EntityId next_id_ = 1;
  AnnotationManager annotations_;
  Timeline          timeline_;
};

}  // namespace hongtu::scene
