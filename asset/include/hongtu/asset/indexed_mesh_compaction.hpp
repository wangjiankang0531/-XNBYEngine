#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace hongtu::asset {

  // Ranges address the original triangle-index stream, before any removal.
  struct MeshIndexRange {
    uint32_t first = 0;
    uint32_t count = 0;
  };

  struct CompactMeshIndexPlan {
    // New vertex N copies the complete attributes of source_vertex_indices[N].
    // Distinct source vertices stay distinct, including normal/material/UV seams.
    std::vector<uint32_t> source_vertex_indices;
    std::vector<uint32_t> indices;
  };

  // Keep parents until their replacement buffers are resident. Returned ranges
  // address the original index buffer; no indices or attributes are rewritten.
  inline std::vector<MeshIndexRange> retainedMeshIndexRanges(uint32_t index_count,
                                                             std::span<const MeshIndexRange> removed_ranges) {
    if (index_count % 3)
      throw std::invalid_argument("triangle index count must be a multiple of three");
    std::vector<MeshIndexRange> result;
    uint64_t end = 0;
    for (const auto& range : removed_ranges) {
      if (!range.count || range.first % 3 || range.count % 3 || range.first < end ||
          uint64_t(range.first) + range.count > index_count)
        throw std::invalid_argument("removed triangle ranges must be ordered, disjoint and in bounds");
      if (range.first > end)
        result.push_back({static_cast<uint32_t>(end), static_cast<uint32_t>(range.first - end)});
      end = uint64_t(range.first) + range.count;
    }
    if (end < index_count)
      result.push_back({static_cast<uint32_t>(end), static_cast<uint32_t>(index_count - end)});
    return result;
  }

  inline CompactMeshIndexPlan compactMeshIndices(uint32_t vertex_count,
                                                 std::span<const uint32_t> indices,
                                                 std::span<const MeshIndexRange> removed_ranges) {
    if (indices.size() % 3 != 0)
      throw std::invalid_argument("triangle index count must be a multiple of three");
    uint64_t end = 0;
    uint64_t removed = 0;
    for (const auto& range : removed_ranges) {
      if (!range.count || range.first % 3 || range.count % 3 || range.first < end ||
          uint64_t(range.first) + range.count > indices.size())
        throw std::invalid_argument("removed triangle ranges must be ordered, disjoint and in bounds");
      end = uint64_t(range.first) + range.count;
      removed += range.count;
    }
    constexpr uint32_t missing = std::numeric_limits<uint32_t>::max();
    if (removed == indices.size())
      return {};
    std::vector<uint32_t> remap(vertex_count, missing);
    CompactMeshIndexPlan plan;
    plan.indices.reserve(indices.size() - removed);
    plan.source_vertex_indices.reserve(std::min<uint64_t>(vertex_count, indices.size() - removed));
    std::size_t range_index = 0;
    for (std::size_t i = 0; i < indices.size(); ++i) {
      while (range_index < removed_ranges.size() &&
             i >= uint64_t(removed_ranges[range_index].first) + removed_ranges[range_index].count)
        ++range_index;
      if (range_index < removed_ranges.size() && i >= removed_ranges[range_index].first) {
        i = uint64_t(removed_ranges[range_index].first) + removed_ranges[range_index].count - 1;
        continue;
      }
      const uint32_t source = indices[i];
      if (source >= vertex_count)
        throw std::invalid_argument("retained vertex index is out of bounds");
      if (remap[source] == missing) {
        remap[source] = static_cast<uint32_t>(plan.source_vertex_indices.size());
        plan.source_vertex_indices.push_back(source);
      }
      plan.indices.push_back(remap[source]);
    }
    return plan;
  }

} // namespace hongtu::asset
