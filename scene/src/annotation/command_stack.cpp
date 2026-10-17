#include <hongtu/scene/annotation/command_stack.hpp>

#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>

#include <nlohmann/json.hpp>

namespace hongtu::scene {

namespace {

/// Rebuild a fresh annotation from a JSON blob (kind/mode dispatch shared with
/// AnnotationManager::fromJson — duplicated here to keep CommandStack self-
/// contained without pulling in the manager's private parseKind/parseMode).
std::unique_ptr<Annotation> rebuild(const nlohmann::json& j) {
  if (!j.is_object() || !j.contains("kind") || !j.contains("mode")) return nullptr;
  const auto k = j["kind"].get<std::string>();
  const auto m = j["mode"].get<std::string>();
  AnnotationMode mode = (m == "geo") ? AnnotationMode::Geo : AnnotationMode::Local;

  std::unique_ptr<Annotation> a;
  if      (k == "polyline") a = mode == AnnotationMode::Geo
                                  ? std::unique_ptr<Annotation>(std::make_unique<GeoPolylineAnnotation>())
                                  : std::unique_ptr<Annotation>(std::make_unique<LocalPolylineAnnotation>());
  else if (k == "polygon")  a = mode == AnnotationMode::Geo
                                  ? std::unique_ptr<Annotation>(std::make_unique<GeoPolygonAnnotation>())
                                  : std::unique_ptr<Annotation>(std::make_unique<LocalPolygonAnnotation>());
  else if (k == "symbol")   a = mode == AnnotationMode::Geo
                                  ? std::unique_ptr<Annotation>(std::make_unique<GeoSymbolAnnotation>())
                                  : std::unique_ptr<Annotation>(std::make_unique<LocalSymbolAnnotation>());
  else if (k == "text")     a = mode == AnnotationMode::Geo
                                  ? std::unique_ptr<Annotation>(std::make_unique<GeoTextAnnotation>())
                                  : std::unique_ptr<Annotation>(std::make_unique<LocalTextAnnotation>());
  else if (k == "arrow")    a = mode == AnnotationMode::Geo
                                  ? std::unique_ptr<Annotation>(std::make_unique<GeoArrowAnnotation>())
                                  : std::unique_ptr<Annotation>(std::make_unique<LocalArrowAnnotation>());
  if (a) a->fromJson(j);
  return a;
}

}  // namespace

// =============================================================================
// AddAnnotationCommand
// =============================================================================

AddAnnotationCommand::AddAnnotationCommand(std::unique_ptr<Annotation> a)
    : pending_(std::move(a)) {}

void AddAnnotationCommand::apply(AnnotationManager& mgr) {
  if (!pending_) return;
  id_ = mgr.add(std::move(pending_));
}

void AddAnnotationCommand::revert(AnnotationManager& mgr) {
  // Snapshot back into pending_ for redo
  if (auto* a = mgr.get(id_)) {
    pending_ = rebuild(a->toJson());
    if (pending_) pending_->setId(id_);
  }
  mgr.remove(id_);
}

// =============================================================================
// RemoveAnnotationCommand
// =============================================================================

void RemoveAnnotationCommand::apply(AnnotationManager& mgr) {
  if (auto* a = mgr.get(id_)) {
    snapshot_ = rebuild(a->toJson());
    if (snapshot_) snapshot_->setId(id_);
  }
  mgr.remove(id_);
}

void RemoveAnnotationCommand::revert(AnnotationManager& mgr) {
  if (snapshot_) mgr.add(std::move(snapshot_));
}

// =============================================================================
// ModifyAnnotationCommand
// =============================================================================

ModifyAnnotationCommand::ModifyAnnotationCommand(
    uint64_t id, nlohmann::json before, nlohmann::json after)
    : id_(id),
      before_(std::make_unique<nlohmann::json>(std::move(before))),
      after_ (std::make_unique<nlohmann::json>(std::move(after))) {}

ModifyAnnotationCommand::~ModifyAnnotationCommand() = default;

void ModifyAnnotationCommand::apply(AnnotationManager& mgr) {
  mgr.remove(id_);
  auto a = rebuild(*after_);
  if (a) { a->setId(id_); mgr.add(std::move(a)); }
}

void ModifyAnnotationCommand::revert(AnnotationManager& mgr) {
  mgr.remove(id_);
  auto a = rebuild(*before_);
  if (a) { a->setId(id_); mgr.add(std::move(a)); }
}

// =============================================================================
// CommandStack
// =============================================================================

void CommandStack::doCommand(std::unique_ptr<Command> cmd, AnnotationManager& mgr) {
  if (!cmd) return;
  cmd->apply(mgr);
  redo_stack_.clear();              // new branch invalidates redo
  undo_stack_.push_back(std::move(cmd));
  while (undo_stack_.size() > max_depth_) {
    undo_stack_.erase(undo_stack_.begin());
  }
}

bool CommandStack::undo(AnnotationManager& mgr) {
  if (undo_stack_.empty()) return false;
  auto cmd = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  cmd->revert(mgr);
  redo_stack_.push_back(std::move(cmd));
  return true;
}

bool CommandStack::redo(AnnotationManager& mgr) {
  if (redo_stack_.empty()) return false;
  auto cmd = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  cmd->apply(mgr);
  undo_stack_.push_back(std::move(cmd));
  return true;
}

void CommandStack::clear() {
  undo_stack_.clear();
  redo_stack_.clear();
}

}  // namespace hongtu::scene
