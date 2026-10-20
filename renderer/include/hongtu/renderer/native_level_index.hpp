#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

namespace hongtu::renderer {

  struct NativeLevelBounds {
    uint32_t id = 0;
    int level = 0;
    glm::dvec3 low{}, high{};
  };

  struct NativeLevelView {
    glm::dvec3 camera{}, target{};
    double fovDegrees = 50, aspect = 1.6, nearMeters = 0.1, farMeters = 60000;

    std::array<glm::dvec4, 6> planes() const {
      const auto finite = [](glm::dvec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
      };
      if (!finite(camera) || !finite(target) || glm::length(target - camera) < 1e-12 || !std::isfinite(fovDegrees) ||
          !(fovDegrees > 0 && fovDegrees < 179) || !std::isfinite(aspect) || !(aspect > 0) ||
          !std::isfinite(nearMeters) || !std::isfinite(farMeters) || !(nearMeters > 0 && farMeters > nearMeters))
        throw std::invalid_argument("Invalid indexed landscape view");
      const glm::dvec3 forward = glm::normalize(target - camera);
      auto right = glm::cross(forward, glm::dvec3(0, 0, 1));
      right = glm::length(right) < 1e-12 ? glm::dvec3(1, 0, 0) : glm::normalize(right);
      const auto up = glm::normalize(glm::cross(right, forward));
      const double tangent = std::tan(glm::radians(fovDegrees * .5));
      const auto plane = [&](glm::dvec3 n, double offset = 0.0) {
        return glm::dvec4(n, -glm::dot(n, camera) + offset);
      };
      return {plane(forward * tangent * aspect + right),
              plane(forward * tangent * aspect - right),
              plane(forward * tangent + up),
              plane(forward * tangent - up),
              plane(forward, -nearMeters),
              plane(-forward, farMeters)};
    }
  };

  class NativeLevelIndex {
  public:
    struct Query {
      std::vector<uint32_t> ids;
      uint64_t bvhNodesVisited = 0, entryBoundsTested = 0;
    };

    void build(std::vector<NativeLevelBounds> entries) {
      std::set<uint32_t> ids;
      for (const auto& entry : entries) {
        if (!ids.insert(entry.id).second || entry.level < 0)
          throw std::invalid_argument("Duplicate source ID or invalid level");
        for (int a = 0; a < 3; ++a)
          if (!std::isfinite(entry.low[a]) || !std::isfinite(entry.high[a]) || entry.low[a] > entry.high[a])
            throw std::invalid_argument("Invalid exact source bounds");
      }
      entries_ = std::move(entries);
      order_.resize(entries_.size());
      std::iota(order_.begin(), order_.end(), 0u);
      nodes_.clear();
      if (!entries_.empty())
        buildNode(0, static_cast<uint32_t>(entries_.size()));
    }

    Query query(const NativeLevelView& view) const {
      const auto planes = view.planes();
      Query result;
      std::vector<uint32_t> pending;
      if (!nodes_.empty())
        pending.push_back(0);
      while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        const auto& node = nodes_[index];
        ++result.bvhNodesVisited;
        if (!intersects(node.low, node.high, planes))
          continue;
        if (node.count) {
          for (uint32_t i = node.first; i < node.first + node.count; ++i) {
            const auto& entry = entries_[order_[i]];
            ++result.entryBoundsTested;
            if (intersects(entry.low, entry.high, planes))
              result.ids.push_back(entry.id);
          }
        } else {
          pending.push_back(node.left);
          pending.push_back(node.right);
        }
      }
      std::sort(result.ids.begin(), result.ids.end());
      return result;
    }

    size_t size() const {
      return entries_.size();
    }

  private:
    struct Node {
      glm::dvec3 low{}, high{};
      uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    static bool intersects(glm::dvec3 low, glm::dvec3 high, const std::array<glm::dvec4, 6>& planes) {
      for (const auto& plane : planes) {
        const glm::dvec3 p(plane.x >= 0 ? high.x : low.x, plane.y >= 0 ? high.y : low.y, plane.z >= 0 ? high.z : low.z);
        if (glm::dot(glm::dvec3(plane), p) + plane.w < -1e-7)
          return false;
      }
      return true;
    }
    uint32_t buildNode(uint32_t first, uint32_t count) {
      Node node;
      node.low = entries_[order_[first]].low;
      node.high = entries_[order_[first]].high;
      for (uint32_t i = first + 1; i < first + count; ++i) {
        node.low = glm::min(node.low, entries_[order_[i]].low);
        node.high = glm::max(node.high, entries_[order_[i]].high);
      }
      const auto index = static_cast<uint32_t>(nodes_.size());
      nodes_.push_back(node);
      if (count <= 8) {
        nodes_[index].first = first;
        nodes_[index].count = count;
      } else {
        const auto extent = node.high - node.low;
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > extent[axis])
          axis = 2;
        const uint32_t half = count / 2;
        std::nth_element(order_.begin() + first,
                         order_.begin() + first + half,
                         order_.begin() + first + count,
                         [&](uint32_t a, uint32_t b) {
                           const double ca = entries_[a].low[axis] + entries_[a].high[axis];
                           const double cb = entries_[b].low[axis] + entries_[b].high[axis];
                           return ca == cb ? a < b : ca < cb;
                         });
        const auto left = buildNode(first, half);
        const auto right = buildNode(first + half, count - half);
        nodes_[index].left = left;
        nodes_[index].right = right;
      }
      return index;
    }
    std::vector<NativeLevelBounds> entries_;
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
  };

} // namespace hongtu::renderer
