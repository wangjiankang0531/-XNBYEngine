#pragma once

#include <hongtu/renderer/picking/ray_caster.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace hongtu::renderer {
  // Immutable acceleration for the same f32 local mesh submitted to the GPU.
  // Intersection math remains f64; t uses the WorldRay direction's units.
  // Degenerate/thin source faces are retained, never filtered for appearance.
  class TriangleBvh {
  public:
    struct QueryStats {
      uint64_t boundsTests = 0;
      uint64_t triangleTests = 0;
    };

    explicit TriangleBvh(size_t expectedTriangles = 0) {
      triangles_.reserve(expectedTriangles);
    }

    void addTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
      if (built_)
        throw std::logic_error("Cannot mutate a built triangle BVH");
      for (auto p : {a, b, c})
        for (int axis = 0; axis < 3; ++axis)
          if (!std::isfinite(p[axis]))
            throw std::invalid_argument("Nonfinite picking triangle");
      if (triangles_.size() >= std::numeric_limits<uint32_t>::max() / 2)
        throw std::length_error("Picking mesh is too large");
      triangles_.push_back({a, b, c});
    }

    void build() {
      if (built_)
        throw std::logic_error("Triangle BVH already built");
      nodes_.reserve(triangles_.size() / 2 + 1);
      if (!triangles_.empty())
        buildNode(0, static_cast<uint32_t>(triangles_.size()));
      built_ = true;
    }

    size_t triangleCount() const {
      return triangles_.size();
    }
    size_t memoryBytes() const {
      return sizeof(*this) + triangles_.capacity() * sizeof(Triangle) + nodes_.capacity() * sizeof(Node);
    }

    std::optional<PickHit> intersect(const WorldRay& ray,
                                     double maxT = std::numeric_limits<double>::infinity(),
                                     QueryStats* stats = nullptr) const {
      if (!built_)
        throw std::logic_error("Picking requires a built triangle BVH");
      if (nodes_.empty() || std::isnan(maxT) || maxT <= 0)
        return {};
      for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(ray.origin[axis]) || !std::isfinite(ray.direction[axis]))
          return {};
      if (glm::dot(ray.direction, ray.direction) <= 0)
        return {};
      double nearest = maxT;
      bool found = false;
      std::vector<uint32_t> stack;
      stack.reserve(32);
      stack.push_back(0);
      while (!stack.empty()) {
        const auto index = stack.back();
        stack.pop_back();
        const Node& node = nodes_[index];
        if (stats)
          ++stats->boundsTests;
        if (!intersectsBounds(ray, node, nearest))
          continue;
        if (!node.count) {
          stack.push_back(node.left);
          stack.push_back(node.right);
          continue;
        }
        for (uint32_t i = node.first; i < node.first + node.count; ++i) {
          const auto& triangle = triangles_[i];
          if (stats)
            ++stats->triangleTests;
          if (auto hit = intersectRayTriangle(ray, triangle.a, triangle.b, triangle.c); hit && *hit <= nearest) {
            nearest = *hit;
            found = true;
          }
        }
      }
      return found ? std::optional<PickHit>{{ray.origin + ray.direction * nearest, nearest}} : std::nullopt;
    }

  private:
    struct Triangle {
      glm::vec3 a, b, c;
      glm::vec3 center() const {
        return a / 3.f + b / 3.f + c / 3.f;
      }
    };
    struct Node {
      glm::vec3 low{std::numeric_limits<float>::max()}, high{std::numeric_limits<float>::lowest()};
      uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    bool built_ = false;

    uint32_t buildNode(uint32_t first, uint32_t count) {
      Node node;
      glm::vec3 centerLow(std::numeric_limits<float>::max()), centerHigh(std::numeric_limits<float>::lowest());
      for (uint32_t i = first; i < first + count; ++i) {
        const auto& triangle = triangles_[i];
        for (auto p : {triangle.a, triangle.b, triangle.c}) {
          node.low = glm::min(node.low, p);
          node.high = glm::max(node.high, p);
        }
        centerLow = glm::min(centerLow, triangle.center());
        centerHigh = glm::max(centerHigh, triangle.center());
      }
      const auto index = static_cast<uint32_t>(nodes_.size());
      nodes_.push_back(node);
      if (count <= 8) {
        nodes_[index].first = first;
        nodes_[index].count = count;
      } else {
        const glm::vec3 extent = centerHigh - centerLow;
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
        const auto half = count / 2;
        std::nth_element(triangles_.begin() + first,
                         triangles_.begin() + first + half,
                         triangles_.begin() + first + count,
                         [axis](const Triangle& a, const Triangle& b) { return a.center()[axis] < b.center()[axis]; });
        const uint32_t left = buildNode(first, half), right = buildNode(first + half, count - half);
        nodes_[index].left = left;
        nodes_[index].right = right;
      }
      return index;
    }

    static bool intersectsBounds(const WorldRay& ray, const Node& node, double maxT) {
      double first = 0, last = maxT;
      for (int axis = 0; axis < 3; ++axis) {
        if (ray.direction[axis] == 0) {
          if (ray.origin[axis] < node.low[axis] || ray.origin[axis] > node.high[axis])
            return false;
        } else {
          double a = (double(node.low[axis]) - ray.origin[axis]) / ray.direction[axis];
          double b = (double(node.high[axis]) - ray.origin[axis]) / ray.direction[axis];
          if (a > b)
            std::swap(a, b);
          first = std::max(first, a);
          last = std::min(last, b);
          if (last < first)
            return false;
        }
      }
      return true;
    }
  };
} // namespace hongtu::renderer
