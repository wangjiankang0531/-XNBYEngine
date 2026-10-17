#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace hongtu::gis {

/// Parsed b3dm container. Callers chain this with `asset::GltfLoader::
/// loadFromMemory(glb_bytes.data(), glb_bytes.size())` to materialise the
/// model. The reader does NOT depend on the asset module to avoid a
/// gis ↔ renderer ↔ asset cycle (renderer already depends on gis).
struct B3dmPayload {
  std::vector<u8>   glb_bytes;             ///< embedded GLB, byte-exact slice
  size_t            glb_offset    = 0;     ///< where the GLB began in the input
  u32 feature_table_json_len      = 0;
  u32 feature_table_bin_len       = 0;
  u32 batch_table_json_len        = 0;
  u32 batch_table_bin_len         = 0;
  bool valid = false;                      ///< true iff header + GLB magic OK
  std::string error;                       ///< empty on success
};

/// Legacy minimal mesh struct kept for callers that bypass tinygltf and
/// just want a placeholder unit cube to draw while the real glTF decode
/// runs on a worker thread.
struct B3dmMesh {
  struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
  };
  std::vector<Vertex> vertices;
  std::vector<u32>    indices;
  glm::vec4 base_color{0.6f, 0.7f, 0.8f, 1.0f};
  bool valid = false;
};

/// Parse a .b3dm binary buffer.
///   * Validates the 28-byte header (magic / version / byteLength).
///   * Computes the GLB offset = sizeof(header) + featureTable + batchTable.
///   * Validates the embedded `glTF`/v2 magic.
///   * Slices the GLB bytes into `payload.glb_bytes` for downstream decode.
///
/// On any failure `payload.valid = false` and `payload.error` carries the
/// diagnostic.
B3dmPayload readB3dmPayload(const std::vector<u8>& bytes);

/// Legacy entrypoint — returns a placeholder unit-cube mesh always.
/// `valid` is true iff the b3dm header passed validation. Use
/// `readB3dmPayload` for the real GLB bytes.
B3dmMesh readB3dm(const std::vector<u8>& bytes);

}  // namespace hongtu::gis
