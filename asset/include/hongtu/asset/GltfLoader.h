#pragma once

#include <hongtu/renderer/animation/animation_clip.hpp>
#include <hongtu/renderer/animation/skeleton.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hongtu::asset {

/// A single mesh primitive: one vertex array + one index array, with one
/// material assignment. A glTF "mesh" can contain multiple primitives;
/// each becomes a separate `MeshData` here so the renderer can draw them
/// with their own pipeline state.
struct MeshData {
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;    // may be empty if the source lacked normals
  std::vector<glm::vec2> uvs;        // may be empty if the source lacked UV0
  std::vector<uint32_t>  indices;    // always 32-bit; loader widens uint16 source
  int                    material_id = -1;

  // ---------------------------------------------------------------------------
  // Skinning — populated only when the source primitive has JOINTS_0 +
  // WEIGHTS_0 attributes. Both arrays size-matched to `positions`.
  // ---------------------------------------------------------------------------
  std::vector<glm::uvec4> joint_indices;
  std::vector<glm::vec4>  joint_weights;
  int                     skin_id = -1;   // index into ModelData::skins, -1 if not skinned
};

/// PBR metallic-roughness material — the glTF default.
struct MaterialData {
  std::string  name;
  glm::vec4    base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
  float        metallic_factor   = 1.0f;
  float        roughness_factor  = 1.0f;
  int          base_color_texture       = -1;   // index into ModelData::textures
  int          metallic_roughness_texture = -1;
  int          normal_texture           = -1;
  glm::vec3    emissive_factor{0.0f};           // emissiveFactor × KHR emissive_strength
  int          emissive_texture         = -1;   // index into ModelData::textures
};

/// A texture image — owns its pixel data so it survives independently of the
/// underlying tinygltf::Model.
struct TextureData {
  std::string             uri;       // source path (relative) or empty for embedded
  int                     width  = 0;
  int                     height = 0;
  int                     channels = 0;   // 1/2/3/4
  std::vector<uint8_t>    pixels;
};

/// A node in the scene graph: optional mesh, optional children, transform.
/// Translation/rotation/scale are kept as the glTF source representation;
/// the renderer composes them as needed.
struct NodeData {
  std::string         name;
  std::vector<int>    children;
  int                 mesh = -1;            // index into ModelData::meshes
  glm::dvec3          translation{0.0, 0.0, 0.0};
  glm::quat           rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3           scale{1.0f, 1.0f, 1.0f};
};

/// A skin = one Skeleton (joint hierarchy) extracted from a glTF `skins[i]`.
/// Joint indices in MeshData::joint_indices refer to joints inside this skin.
struct SkinData {
  std::string                          name;
  renderer::animation::Skeleton        skeleton;
};

/// The full loaded model: nodes form the scene graph; meshes/materials/
/// textures are indexed flat arrays.
struct ModelData {
  std::vector<MeshData>     meshes;
  std::vector<MaterialData> materials;
  std::vector<TextureData>  textures;
  std::vector<NodeData>     nodes;
  std::vector<int>          root_nodes;    // indices into nodes — the scene roots
  std::string               source_path;

  // ---------------------------------------------------------------------------
  // Animations + skins — empty arrays when the source has no skinning data.
  // ---------------------------------------------------------------------------
  std::vector<SkinData>                          skins;
  std::vector<renderer::animation::AnimationClip> animations;

  bool empty() const { return meshes.empty(); }

  /// Find the first node named `name` (exact match) and return its world
  /// transform — parent transforms composed down from the scene roots, in the
  /// SAME space mesh vertices are baked into (see node-transform baking). Used
  /// to read attachment points authored in a glb, e.g. a `nozzle` empty marking
  /// a fire monitor's water outlet (translation = emission point, the node's
  /// local +X axis = spray direction). Returns nullopt when no such node exists.
  std::optional<glm::mat4> findNodeWorldTransform(const std::string& name) const;
};

/// glTF / glb loader.
///
/// Auto-detects the container by header magic — same API for both:
///   .gltf : JSON text + optional external .bin and image files (resolved
///           relative to the .gltf path)
///   .glb  : single binary container with embedded JSON + bin + images
///
/// Usage:
///   GltfLoader loader;
///   auto result = loader.load("path/to/model.glb");
///   if (!result.has_value()) { ...handle loader.lastError()... }
///   const ModelData& model = *result;
class GltfLoader {
 public:
  GltfLoader() = default;

  /// Load from a filesystem path. Container is detected automatically:
  /// extension `.gltf` (or any ASCII-JSON content) loads as text glTF;
  /// extension `.glb` (or the `glTF` binary magic 0x46546C67) loads as glb.
  std::optional<ModelData> load(const std::string& path);

  /// Load from an in-memory buffer. Used by the 3D Tiles b3dm pipeline,
  /// which carves the embedded GLB out of a larger binary and hands the
  /// raw bytes here. `base_dir` is used by tinygltf to resolve external
  /// URIs (empty string disables external references).
  std::optional<ModelData>
  loadFromMemory(const unsigned char* bytes, size_t size,
                 bool binary = true,
                 const std::string& base_dir = std::string{});

  /// Last error / warning produced by `load`. Empty when load succeeded.
  const std::string& lastError()   const { return last_error_; }
  const std::string& lastWarning() const { return last_warning_; }

 private:
  std::string last_error_;
  std::string last_warning_;
};

}  // namespace hongtu::asset
