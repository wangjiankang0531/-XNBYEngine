#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hongtu::renderer {
  struct NativeSourceNode {
    std::string name;
    std::vector<std::string> children;
  };

  // Logical visibility commits only after the complete, audited child family is
  // uploaded. The renderer retains old physical pages until their last draw fence.
  class NativeSourceFrontier {
  public:
    using Cut = std::set<std::string>;
    struct Ticket {
      uint64_t value;
    };

    NativeSourceFrontier(std::vector<NativeSourceNode> nodes,
                         std::string root,
                         Cut visible,
                         uint64_t residentBytes,
                         uint64_t budgetBytes)
        : root_(std::move(root)), visible_(std::move(visible)), residentBytes_(residentBytes),
          budgetBytes_(budgetBytes) {
      for (auto& node : nodes) {
        if (!nodes_.emplace(node.name, node.children).second)
          throw std::invalid_argument("Duplicate source node");
      }
      std::set<std::string> seen;
      std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (!nodes_.contains(name) || !seen.insert(name).second)
          throw std::invalid_argument("Invalid source hierarchy: missing, cyclic, or shared child");
        reachable_.insert(name);
        for (const auto& child : nodes_.at(name))
          visit(child);
      };
      visit(root_);
      validateCut(visible_);
      if (residentBytes_ > budgetBytes_)
        throw std::invalid_argument("Source residency exceeds budget");
    }

    const Cut& visible() const {
      return visible_;
    }
    uint64_t residentBytes() const {
      return residentBytes_;
    }
    uint64_t concurrentBytes() const {
      return residentBytes_ + (pending_ ? pending_->bytes : 0);
    }

    std::optional<Ticket> prepare(const std::string& parent,
                                  const std::vector<std::string>& auditedChildren,
                                  uint64_t candidateBytes,
                                  size_t uploadParts) {
      if (pending_ || !visible_.contains(parent) || !candidateBytes || !uploadParts || uploadParts > 64 ||
          candidateBytes > budgetBytes_ - residentBytes_)
        return {};
      const auto& expected = nodes_.at(parent);
      Cut children(auditedChildren.begin(), auditedChildren.end());
      if (expected.empty() || children.size() != auditedChildren.size() ||
          children != Cut(expected.begin(), expected.end()))
        return {};
      Cut replacement = visible_;
      replacement.erase(parent);
      replacement.insert(children.begin(), children.end());
      validateCut(replacement);
      Ticket ticket{++nextTicket_};
      pending_ = Pending{ticket, std::move(replacement), candidateBytes, std::vector<bool>(uploadParts, false)};
      return ticket;
    }

    // A spatial index may jump directly to a complete mixed-depth cut of this
    // branch. Validate it against the original tree, not a synthetic hierarchy.
    std::optional<Ticket> prepareDescendants(const std::string& parent,
                                             const std::vector<std::string>& auditedCut,
                                             uint64_t candidateBytes,
                                             size_t uploadParts) {
      if (pending_ || !visible_.contains(parent) || !candidateBytes || !uploadParts || uploadParts > 64 ||
          candidateBytes > budgetBytes_ - residentBytes_)
        return {};
      Cut selected(auditedCut.begin(), auditedCut.end());
      if (selected.empty() || selected.size() != auditedCut.size() || selected.contains(parent))
        return {};
      Cut subtree;
      std::function<void(const std::string&)> collect = [&](const auto& name) {
        subtree.insert(name);
        for (const auto& child : nodes_.at(name))
          collect(child);
      };
      collect(parent);
      for (const auto& name : selected)
        if (!subtree.contains(name))
          return {};
      Cut replacement = visible_;
      replacement.erase(parent);
      replacement.insert(selected.begin(), selected.end());
      try {
        validateCut(replacement);
      } catch (const std::invalid_argument&) {
        return {};
      }
      Ticket ticket{++nextTicket_};
      pending_ = Pending{ticket, std::move(replacement), candidateBytes, std::vector<bool>(uploadParts, false)};
      return ticket;
    }

    bool markPartReady(Ticket ticket, size_t part) {
      if (!pending_ || ticket.value != pending_->ticket.value || part >= pending_->ready.size())
        return false;
      pending_->ready[part] = true;
      return true;
    }

    bool commit(Ticket ticket) {
      if (!pending_ || ticket.value != pending_->ticket.value)
        return false;
      for (bool ready : pending_->ready)
        if (!ready)
          return false;
      visible_ = std::move(pending_->replacement);
      // Shared parent pages may still contain other visible nodes. Count them
      // until the renderer reports their retirement; do not assume parent bytes free.
      residentBytes_ += pending_->bytes;
      pending_.reset();
      return true;
    }

    bool cancel(Ticket ticket) {
      if (!pending_ || ticket.value != pending_->ticket.value)
        return false;
      pending_.reset();
      return true;
    }

    // Called after a complete coarsening transaction and actual GPU page retirement.
    void reset(Cut visible, uint64_t residentBytes) {
      validateCut(visible);
      if (residentBytes > budgetBytes_)
        throw std::invalid_argument("Source residency exceeds budget");
      visible_ = std::move(visible);
      residentBytes_ = residentBytes;
      pending_.reset();
    }

  private:
    void validateCut(const Cut& selected) const {
      if (selected.empty())
        throw std::invalid_argument("Empty source cut");
      for (const auto& name : selected)
        if (!reachable_.contains(name))
          throw std::invalid_argument("Unknown source cut node");
      Cut consumed;
      std::function<void(const std::string&)> cover = [&](const std::string& name) {
        if (selected.contains(name)) {
          consumed.insert(name);
          return;
        }
        const auto& children = nodes_.at(name);
        if (children.empty())
          throw std::invalid_argument("Source cut leaves a coverage hole");
        for (const auto& child : children)
          cover(child);
      };
      cover(root_);
      if (consumed != selected)
        throw std::invalid_argument("Source cut overlaps an ancestor");
    }
    struct Pending {
      Ticket ticket;
      Cut replacement;
      uint64_t bytes;
      std::vector<bool> ready;
    };
    std::unordered_map<std::string, std::vector<std::string>> nodes_;
    Cut reachable_;
    std::string root_;
    Cut visible_;
    uint64_t residentBytes_ = 0, budgetBytes_ = 0, nextTicket_ = 0;
    std::optional<Pending> pending_;
  };
} // namespace hongtu::renderer
