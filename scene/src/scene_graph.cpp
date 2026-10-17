#include <hongtu/scene/scene_graph.hpp>

namespace hongtu::scene {

bool SceneGraph::remove(EntityId id) {
  auto it = entities_.find(id);
  if (it == entities_.end()) return false;
  if (!it->second.name.empty()) by_name_.erase(it->second.name);
  entities_.erase(it);
  return true;
}

bool SceneGraph::removeByName(const std::string& name) {
  auto it = by_name_.find(name);
  if (it == by_name_.end()) return false;
  return remove(it->second);
}

bool SceneGraph::contains(EntityId id) const {
  return entities_.find(id) != entities_.end();
}

SceneGraph::EntityId SceneGraph::idOfName(const std::string& name) const {
  auto it = by_name_.find(name);
  return it == by_name_.end() ? kInvalidId : it->second;
}

std::string SceneGraph::nameOfId(EntityId id) const {
  auto it = entities_.find(id);
  return it == entities_.end() ? std::string{} : it->second.name;
}

void SceneGraph::clear() {
  entities_.clear();
  by_name_.clear();
  next_id_ = 1;
  annotations_.clear();
  timeline_.clearDrivers();
}

}  // namespace hongtu::scene
