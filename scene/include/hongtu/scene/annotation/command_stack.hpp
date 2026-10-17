#pragma once

#include <hongtu/scene/annotation/annotation_manager.hpp>

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <vector>

namespace hongtu::scene {

/// Abstract reversible operation on an AnnotationManager.
class Command {
 public:
  virtual ~Command() = default;
  virtual void apply (AnnotationManager& mgr) = 0;
  virtual void revert(AnnotationManager& mgr) = 0;
};

/// Adds an annotation. On apply the manager takes ownership; on revert the
/// command takes ownership back so a future redo can re-add it. The id used
/// is whatever the annotation carried at construction (or 0 → auto-assign
/// on first apply).
class AddAnnotationCommand : public Command {
 public:
  explicit AddAnnotationCommand(std::unique_ptr<Annotation> a);
  void apply (AnnotationManager& mgr) override;
  void revert(AnnotationManager& mgr) override;
 private:
  std::unique_ptr<Annotation> pending_;   ///< non-null when not currently in mgr
  uint64_t                    id_ = 0;     ///< assigned on first apply
};

/// Removes an annotation. Snapshots the annotation's JSON on apply so revert
/// can rebuild it.
class RemoveAnnotationCommand : public Command {
 public:
  explicit RemoveAnnotationCommand(uint64_t id) : id_(id) {}
  void apply (AnnotationManager& mgr) override;
  void revert(AnnotationManager& mgr) override;
 private:
  uint64_t                                id_;
  std::unique_ptr<Annotation>             snapshot_;  ///< populated on apply
};

/// Modifies an annotation by JSON diff (before / after blobs). Simple — no
/// field-level granularity, the whole annotation is replaced. Sufficient for
/// editor + drag-to-move use cases.
class ModifyAnnotationCommand : public Command {
 public:
  ModifyAnnotationCommand(uint64_t id, nlohmann::json before, nlohmann::json after);
  ~ModifyAnnotationCommand();
  void apply (AnnotationManager& mgr) override;
  void revert(AnnotationManager& mgr) override;
 private:
  uint64_t                       id_;
  std::unique_ptr<nlohmann::json> before_;  ///< heap-allocated to keep header
  std::unique_ptr<nlohmann::json> after_;   ///< free of <nlohmann/json.hpp>
};

/// Undo/redo stack. doCommand() applies and pushes onto undo; undo() pops one
/// off undo, reverts it, and pushes onto redo. redo() does the reverse.
/// Bound to `max_depth` items — overflow drops the oldest undo entry.
class CommandStack {
 public:
  void doCommand(std::unique_ptr<Command> cmd, AnnotationManager& mgr);
  bool undo(AnnotationManager& mgr);
  bool redo(AnnotationManager& mgr);
  void clear();

  size_t undoDepth() const { return undo_stack_.size(); }
  size_t redoDepth() const { return redo_stack_.size(); }
  size_t maxDepth() const { return max_depth_; }
  void   setMaxDepth(size_t d) { max_depth_ = d; }

 private:
  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
  size_t max_depth_ = 128;
};

}  // namespace hongtu::scene
