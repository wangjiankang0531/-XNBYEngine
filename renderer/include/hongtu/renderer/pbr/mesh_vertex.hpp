#pragma once

#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Standard interleaved vertex used by the PBR forward path and shadow pass.
/// Position + flat-or-smooth normal + a single UV channel is enough for the
/// glTF metallic-roughness model the engine targets; richer tangents are
/// derived in-shader via `tbnFromDerivatives()`.
struct MeshVertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;

  static VertexInputBindingDescription binding() {
    VertexInputBindingDescription b;
    b.binding     = 0;
    b.stride      = sizeof(MeshVertex);
    b.perInstance = false;
    return b;
  }

  static std::vector<VertexInputAttributeDescription> attributes() {
    std::vector<VertexInputAttributeDescription> a(3);
    a[0].location = 0;
    a[0].binding  = 0;
    a[0].format   = Format::R32G32B32_SFLOAT;
    a[0].offset   = offsetof(MeshVertex, position);
    a[1].location = 1;
    a[1].binding  = 0;
    a[1].format   = Format::R32G32B32_SFLOAT;
    a[1].offset   = offsetof(MeshVertex, normal);
    a[2].location = 2;
    a[2].binding  = 0;
    a[2].format   = Format::R32G32_SFLOAT;
    a[2].offset   = offsetof(MeshVertex, uv);
    return a;
  }
};

}  // namespace hongtu::renderer
