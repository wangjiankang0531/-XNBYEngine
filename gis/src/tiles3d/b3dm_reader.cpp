#include <hongtu/gis/tiles3d/b3dm_reader.hpp>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>

namespace hongtu::gis {

namespace {

// b3dm header layout (28 bytes, little-endian).
struct B3dmHeader {
  char     magic[4];              // "b3dm"
  uint32_t version;               // must be 1
  uint32_t byteLength;            // total file size
  uint32_t featureTableJsonLen;
  uint32_t featureTableBinLen;
  uint32_t batchTableJsonLen;
  uint32_t batchTableBinLen;
};
static_assert(sizeof(B3dmHeader) == 28, "b3dm header must be 28 bytes");

// GLB header (12 bytes).
struct GlbHeader {
  uint32_t magic;    // 0x46546C67 "glTF"
  uint32_t version;  // must be 2
  uint32_t length;
};

B3dmMesh makePlaceholderCube() {
  B3dmMesh mesh;
  static const glm::vec3 pos[8] = {
    {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f}, {0.5f,-0.5f, 0.5f}, {0.5f,0.5f, 0.5f}, {-0.5f,0.5f, 0.5f}
  };
  for (const auto& p : pos)
    mesh.vertices.push_back({p, glm::normalize(p), {0.0f, 0.0f}});
  static const u32 idx[] = {
    0,1,2, 0,2,3,  4,6,5, 4,7,6,
    0,1,5, 0,5,4,  2,3,7, 2,7,6,
    1,2,6, 1,6,5,  0,3,7, 0,7,4
  };
  for (auto i : idx) mesh.indices.push_back(i);
  mesh.valid = false;
  return mesh;
}

}  // namespace

B3dmPayload readB3dmPayload(const std::vector<u8>& bytes) {
  B3dmPayload p;

  if (bytes.size() < sizeof(B3dmHeader)) {
    p.error = "buffer too small for b3dm header";
    return p;
  }

  B3dmHeader hdr{};
  std::memcpy(&hdr, bytes.data(), sizeof(hdr));

  if (std::strncmp(hdr.magic, "b3dm", 4) != 0) {
    p.error = "invalid b3dm magic";
    return p;
  }
  if (hdr.version != 1) {
    p.error = "unsupported b3dm version " + std::to_string(hdr.version);
    return p;
  }
  if (static_cast<size_t>(hdr.byteLength) > bytes.size()) {
    p.error = "declared byteLength > buffer size";
    return p;
  }

  p.feature_table_json_len = hdr.featureTableJsonLen;
  p.feature_table_bin_len  = hdr.featureTableBinLen;
  p.batch_table_json_len   = hdr.batchTableJsonLen;
  p.batch_table_bin_len    = hdr.batchTableBinLen;
  p.glb_offset = sizeof(B3dmHeader)
               + hdr.featureTableJsonLen + hdr.featureTableBinLen
               + hdr.batchTableJsonLen   + hdr.batchTableBinLen;

  if (p.glb_offset + sizeof(GlbHeader) > bytes.size()) {
    p.error = "no room for embedded GLB header";
    return p;
  }

  GlbHeader glb{};
  std::memcpy(&glb, bytes.data() + p.glb_offset, sizeof(glb));

  // GLB magic: 0x46546C67 = "glTF" in little-endian.
  if (glb.magic != 0x46546C67u) {
    p.error = "embedded GLB magic mismatch";
    return p;
  }
  if (glb.version != 2) {
    p.error = "embedded GLB version != 2";
    return p;
  }

  // Slice the GLB bytes. The declared GLB length must fit inside the b3dm.
  size_t glb_end = p.glb_offset + glb.length;
  if (glb_end > bytes.size()) {
    p.error = "GLB length runs past end of b3dm buffer";
    return p;
  }
  p.glb_bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(p.glb_offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(glb_end));
  p.valid = true;
  return p;
}

B3dmMesh readB3dm(const std::vector<u8>& bytes) {
  B3dmPayload p = readB3dmPayload(bytes);
  if (!p.valid) {
    spdlog::warn("b3dm: {}", p.error);
    return makePlaceholderCube();
  }
  // Placeholder remains the right answer for the legacy mesh struct; real
  // decode goes through `readB3dmPayload + asset::GltfLoader::loadFromMemory`.
  B3dmMesh mesh = makePlaceholderCube();
  mesh.valid = true;  // header + GLB magic validated
  spdlog::debug("b3dm: parsed header OK, GLB at offset {} ({} bytes)",
                p.glb_offset, p.glb_bytes.size());
  return mesh;
}

}  // namespace hongtu::gis
