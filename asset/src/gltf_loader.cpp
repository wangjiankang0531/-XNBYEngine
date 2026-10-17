// glTF / glb loader implementation. Uses tinygltf for container parsing and
// then translates into HongTu's plain-data ModelData. Container auto-detection
// is by file extension (case-insensitive) and falls back to magic-byte sniff
// when the extension is ambiguous.
//
// Image decoding is intentionally NOT routed through tinygltf — stb_image is
// already implemented in globe_renderer.cpp, and double-defining
// STB_IMAGE_IMPLEMENTATION across translation units would cause linker
// errors. For now textures keep their URIs but not their pixels; v2 of the
// loader will decode images explicitly using the shared stb symbols.

// stb_image's IMPLEMENTATION lives in renderer/src/globe/globe_renderer.cpp.
// We include the header for declarations only (no IMPLEMENTATION macro here)
// and link against the symbols there. Doing it this way lets us decode glTF
// embedded + external textures via stb without double-defining its bodies.
#include <stb_image.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <hongtu/asset/GltfLoader.h>
#include <tiny_gltf.h>
#ifndef GLM_ENABLE_EXPERIMENTAL
#  define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>

namespace hongtu::asset {

  namespace {

    bool endsWithIgnoreCase(const std::string& s, const std::string& suffix) {
      if (s.size() < suffix.size())
        return false;
      for (size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b)
          return false;
      }
      return true;
    }

    /// Sniff the first 4 bytes of the file. Returns true if they match the glb
    /// magic `glTF` (0x46546C67 little-endian).
    bool isBinaryGlb(const std::string& path) {
      std::ifstream f(path, std::ios::binary);
      if (!f)
        return false;
      char magic[4] = {0, 0, 0, 0};
      f.read(magic, 4);
      return magic[0] == 'g' && magic[1] == 'l' && magic[2] == 'T' && magic[3] == 'F';
    }

    /// Copy a typed accessor view into a std::vector<T>. Handles only
    /// tightly-packed accessors (byteStride == sizeof(T)); strided accessors are
    /// re-gathered element by element so the output is always contiguous.
    template <typename T> void readAccessor(const tinygltf::Model& m, int accessor_idx, std::vector<T>& out) {
      if (accessor_idx < 0)
        return;
      const auto& acc = m.accessors[accessor_idx];
      const auto& view = m.bufferViews[acc.bufferView];
      const auto& buf = m.buffers[view.buffer];
      const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;

      const size_t stride = view.byteStride != 0 ? view.byteStride : sizeof(T);
      out.resize(acc.count);
      for (size_t i = 0; i < acc.count; ++i) {
        std::memcpy(&out[i], base + i * stride, sizeof(T));
      }
    }

    /// Read an index accessor into uint32_t regardless of source component type
    /// (uint8 / uint16 / uint32).
    void readIndices(const tinygltf::Model& m, int accessor_idx, std::vector<uint32_t>& out) {
      if (accessor_idx < 0)
        return;
      const auto& acc = m.accessors[accessor_idx];
      const auto& view = m.bufferViews[acc.bufferView];
      const auto& buf = m.buffers[view.buffer];
      const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;

      out.resize(acc.count);
      switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
          for (size_t i = 0; i < acc.count; ++i)
            out[i] = base[i];
          break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
          const auto* src = reinterpret_cast<const uint16_t*>(base);
          for (size_t i = 0; i < acc.count; ++i)
            out[i] = src[i];
          break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
          const auto* src = reinterpret_cast<const uint32_t*>(base);
          std::memcpy(out.data(), src, acc.count * sizeof(uint32_t));
          break;
        }
        default:
          out.clear(); // unsupported — caller will see a missing index buffer
          break;
      }
    }

    void extractMeshes(const tinygltf::Model& m, ModelData& out) {
      // tinygltf::Mesh contains primitives; flatten each primitive into its own
      // MeshData so the renderer can bind a single VBO/IBO per primitive.
      for (const auto& mesh : m.meshes) {
        for (const auto& prim : mesh.primitives) {
          MeshData md;
          md.material_id = prim.material;

          auto posIt = prim.attributes.find("POSITION");
          auto nrmIt = prim.attributes.find("NORMAL");
          auto uvIt = prim.attributes.find("TEXCOORD_0");
          auto jIt = prim.attributes.find("JOINTS_0");
          auto wIt = prim.attributes.find("WEIGHTS_0");

          if (posIt != prim.attributes.end())
            readAccessor(m, posIt->second, md.positions);
          if (nrmIt != prim.attributes.end())
            readAccessor(m, nrmIt->second, md.normals);
          if (uvIt != prim.attributes.end())
            readAccessor(m, uvIt->second, md.uvs);
          readIndices(m, prim.indices, md.indices);

          // Many procedural / exported meshes ship WITHOUT a NORMAL attribute
          // (e.g. trimesh `process=False`). Empty normals → the PBR shader lights
          // every face from a garbage normal → solid-black faces. Synthesize
          // normals from the geometry. They follow the triangle winding, and the
          // forward shader's `if (!gl_FrontFacing) N = -N` then orients them to
          // the viewed side — so CW- and CCW-wound footprints both shade right.
          if (md.normals.empty() && !md.positions.empty()) {
            md.normals.assign(md.positions.size(), glm::vec3(0.0f));
            const std::size_t vcount = md.positions.size();
            auto accumTri = [&](uint32_t a, uint32_t b, uint32_t c) {
              if (a >= vcount || b >= vcount || c >= vcount)
                return;
              const glm::vec3 n = glm::cross(md.positions[b] - md.positions[a], md.positions[c] - md.positions[a]);
              md.normals[a] += n;
              md.normals[b] += n;
              md.normals[c] += n;
            };
            if (!md.indices.empty()) {
              for (std::size_t i = 0; i + 2 < md.indices.size(); i += 3)
                accumTri(md.indices[i], md.indices[i + 1], md.indices[i + 2]);
            } else {
              for (std::size_t i = 0; i + 2 < vcount; i += 3)
                accumTri(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
            }
            for (glm::vec3& n : md.normals) {
              const float len = glm::length(n);
              n = len > 1e-8f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
            }
          }

          // Skinning attributes — glTF spec mandates JOINTS_0 as uvec4 of
          // unsigned byte / short, and WEIGHTS_0 as vec4 of float / normalized
          // byte / short. tinygltf doesn't auto-widen; we widen below.
          if (jIt != prim.attributes.end() && wIt != prim.attributes.end()) {
            const auto& ja = m.accessors[jIt->second];
            const auto& jbv = m.bufferViews[ja.bufferView];
            const auto& jb = m.buffers[jbv.buffer];
            const uint8_t* jbytes = jb.data.data() + jbv.byteOffset + ja.byteOffset;
            md.joint_indices.resize(ja.count);
            for (size_t i = 0; i < ja.count; ++i) {
              if (ja.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                const uint8_t* p = jbytes + i * 4 * sizeof(uint8_t);
                md.joint_indices[i] = {p[0], p[1], p[2], p[3]};
              } else { // assume UNSIGNED_SHORT
                const uint16_t* p = reinterpret_cast<const uint16_t*>(jbytes + i * 4 * sizeof(uint16_t));
                md.joint_indices[i] = {p[0], p[1], p[2], p[3]};
              }
            }
            readAccessor(m, wIt->second, md.joint_weights); // vec4 float assumed
          }
          // skin_id is set later from node.skin in extractNodes association.

          out.meshes.push_back(std::move(md));
        }
      }
    }

    /// Local TRS/matrix of a glTF node as a mat4.
    glm::mat4 nodeLocalMatrix(const tinygltf::Node& n) {
      if (n.matrix.size() == 16) {
        glm::mat4 M(1.0f);
        for (int c = 0; c < 4; ++c)
          for (int r = 0; r < 4; ++r)
            M[c][r] = static_cast<float>(n.matrix[c * 4 + r]);
        return M;
      }
      glm::mat4 M(1.0f);
      if (n.translation.size() == 3)
        M = glm::translate(M, glm::vec3(n.translation[0], n.translation[1], n.translation[2]));
      if (n.rotation.size() == 4)
        M *= glm::mat4_cast(glm::quat(static_cast<float>(n.rotation[3]),
                                      static_cast<float>(n.rotation[0]),
                                      static_cast<float>(n.rotation[1]),
                                      static_cast<float>(n.rotation[2])));
      if (n.scale.size() == 3)
        M = glm::scale(M, glm::vec3(n.scale[0], n.scale[1], n.scale[2]));
      return M;
    }

    /// Global transform of `node_idx` = product of node locals from the scene root
    /// down to it. Uses a child→parent map built from all nodes' children lists.
    glm::mat4 nodeGlobalMatrix(const tinygltf::Model& m, int node_idx) {
      std::vector<int> parent(m.nodes.size(), -1);
      for (size_t p = 0; p < m.nodes.size(); ++p)
        for (int c : m.nodes[p].children)
          if (c >= 0 && c < static_cast<int>(parent.size()))
            parent[c] = static_cast<int>(p);
      // Walk up to collect the chain, then multiply root→node.
      std::vector<int> chain;
      for (int n = node_idx; n >= 0; n = parent[n])
        chain.push_back(n);
      glm::mat4 g(1.0f);
      for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        g = g * nodeLocalMatrix(m.nodes[*it]);
      return g;
    }

    /// Extract skins → SkinData + Skeleton with joints in topological order.
    void extractSkins(const tinygltf::Model& m, ModelData& out) {
      for (const auto& sk : m.skins) {
        SkinData sd;
        sd.name = sk.name;

        // Read inverse_bind_matrices (one mat4 per joint).
        std::vector<glm::mat4> ibms;
        if (sk.inverseBindMatrices >= 0) {
          // Read as flat floats then reinterpret as 16 per matrix.
          // A MAT4 accessor's `count` is the number of MATRICES (one per joint),
          // each 16 contiguous column-major floats (64 B). readAccessor<vec4>
          // would (mis)read `count` vec4s — only count/4 matrices. Read mat4s
          // directly: glm::mat4 is column-major in memory, matching glTF, so a
          // straight memcpy per matrix is correct.
          const auto& acc = m.accessors[sk.inverseBindMatrices];
          const auto& view = m.bufferViews[acc.bufferView];
          const auto& buf = m.buffers[view.buffer];
          const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;
          const size_t stride = view.byteStride != 0 ? view.byteStride : sizeof(glm::mat4);
          ibms.resize(acc.count);
          for (size_t i = 0; i < acc.count; ++i) {
            std::memcpy(&ibms[i], base + i * stride, sizeof(glm::mat4));
          }
        }

        // Build a node-index → joint-index map. Joints are inserted in glTF
        // skin.joints order; the Skeleton requires topological order (parent
        // before child). glTF doesn't guarantee that, so we resort:
        // 1. Find each joint's parent in the original node tree.
        // 2. Topologically sort, accumulating joint indices.
        const size_t N = sk.joints.size();
        std::vector<int> parent_of(N, -1); // parent in joint-space; -1 = root
        for (size_t i = 0; i < N; ++i) {
          int node_i = sk.joints[i];
          // Find a node that lists node_i as child.
          for (size_t j = 0; j < N; ++j) {
            int node_j = sk.joints[j];
            const auto& nj = m.nodes[node_j];
            if (std::find(nj.children.begin(), nj.children.end(), node_i) != nj.children.end()) {
              parent_of[i] = static_cast<int>(j);
              break;
            }
          }
        }
        // Topological order: BFS from roots(parent_of == -1).
        std::vector<int> order;
        order.reserve(N);
        std::vector<bool> placed(N, false);
        for (size_t i = 0; i < N; ++i) {
          if (parent_of[i] == -1) {
            order.push_back(static_cast<int>(i));
            placed[i] = true;
          }
        }
        for (size_t head = 0; head < order.size(); ++head) {
          int parent_joint_idx = order[head];
          for (size_t i = 0; i < N; ++i) {
            if (!placed[i] && parent_of[i] == parent_joint_idx) {
              order.push_back(static_cast<int>(i));
              placed[i] = true;
            }
          }
        }
        // Inverse map for translating parent indices.
        std::vector<int> remap(N, -1);
        for (size_t i = 0; i < order.size(); ++i)
          remap[order[i]] = static_cast<int>(i);

        for (size_t i = 0; i < order.size(); ++i) {
          int orig = order[i];
          int node_i = sk.joints[orig];
          const auto& n = m.nodes[node_i];
          renderer::animation::Joint joint;
          joint.name = n.name;
          joint.parent = parent_of[orig] >= 0 ? remap[parent_of[orig]] : -1;
          joint.inverse_bind = orig < static_cast<int>(ibms.size()) ? ibms[orig] : glm::mat4(1.0f);
          if (n.translation.size() == 3) {
            joint.local_translation = {static_cast<float>(n.translation[0]),
                                       static_cast<float>(n.translation[1]),
                                       static_cast<float>(n.translation[2])};
          }
          if (n.rotation.size() == 4) {
            joint.local_rotation = glm::quat(static_cast<float>(n.rotation[3]),
                                             static_cast<float>(n.rotation[0]),
                                             static_cast<float>(n.rotation[1]),
                                             static_cast<float>(n.rotation[2]));
          }
          if (n.scale.size() == 3) {
            joint.local_scale = {
                static_cast<float>(n.scale[0]), static_cast<float>(n.scale[1]), static_cast<float>(n.scale[2])};
          }
          sd.skeleton.addJoint(std::move(joint));
        }

        // Armature transform: the global transform of the node ABOVE the root
        // joint(s). glTF joint world transforms include every ancestor up to the
        // scene root; our Skeleton's hierarchy starts at the root joint, so the
        // non-joint ancestor transform (e.g. CesiumMan's Z-up→Y-up armature
        // rotation) must be carried separately or the bind pose won't reconstruct
        // to identity and the mesh renders rotated/lying down.
        if (!order.empty()) {
          const int root_node = sk.joints[order[0]];
          // Parent of root_node in the full node tree (the armature), if any.
          int armature = -1;
          for (size_t p = 0; p < m.nodes.size(); ++p)
            if (std::find(m.nodes[p].children.begin(), m.nodes[p].children.end(), root_node) !=
                m.nodes[p].children.end()) {
              armature = static_cast<int>(p);
              break;
            }
          if (armature >= 0)
            sd.skeleton.setRootTransform(nodeGlobalMatrix(m, armature));
        }

        out.skins.push_back(std::move(sd));
      }
    }

    /// Extract animations into AnimationClip list. Channel joint_index is
    /// resolved against the **first** skin's topological joint order — glTF
    /// allows animations to target arbitrary nodes; for our P0 (single skeleton
    /// per skinned mesh) we only handle channels whose target node appears in
    /// skin[0]. Channels targeting other nodes are dropped with a quiet skip.
    void extractAnimations(const tinygltf::Model& m, ModelData& out) {
      if (out.skins.empty() || m.animations.empty())
        return;
      // Build node→joint_index map for skin 0.
      const auto& sk0 = m.skins[0];
      std::vector<int> node_to_joint(m.nodes.size(), -1);
      // We need the *topological* (remapped) joint index, same as in extractSkins.
      // Re-derive the remap here:
      const size_t N = sk0.joints.size();
      std::vector<int> parent_of(N, -1);
      for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) {
          const auto& nj = m.nodes[sk0.joints[j]];
          if (std::find(nj.children.begin(), nj.children.end(), sk0.joints[i]) != nj.children.end()) {
            parent_of[i] = static_cast<int>(j);
            break;
          }
        }
      }
      std::vector<int> order;
      std::vector<bool> placed(N, false);
      for (size_t i = 0; i < N; ++i)
        if (parent_of[i] == -1) {
          order.push_back(static_cast<int>(i));
          placed[i] = true;
        }
      for (size_t h = 0; h < order.size(); ++h)
        for (size_t i = 0; i < N; ++i)
          if (!placed[i] && parent_of[i] == order[h]) {
            order.push_back(static_cast<int>(i));
            placed[i] = true;
          }
      for (size_t topo = 0; topo < order.size(); ++topo) {
        int orig = order[topo];
        node_to_joint[sk0.joints[orig]] = static_cast<int>(topo);
      }

      for (const auto& a : m.animations) {
        renderer::animation::AnimationClip clip;
        clip.name = a.name;
        float dur = 0.0f;

        for (const auto& ch : a.channels) {
          if (ch.target_node < 0)
            continue;
          int joint = node_to_joint[ch.target_node];
          if (joint < 0)
            continue; // target node not in skin

          const auto& sampler = a.samplers[ch.sampler];
          std::vector<float> times;
          readAccessor(m, sampler.input, times);
          if (!times.empty())
            dur = std::max(dur, times.back());
          renderer::animation::InterpolationKind kind = renderer::animation::InterpolationKind::Linear;
          if (sampler.interpolation == "STEP")
            kind = renderer::animation::InterpolationKind::Step;
          else if (sampler.interpolation == "CUBICSPLINE")
            kind = renderer::animation::InterpolationKind::CubicSpline;

          if (ch.target_path == "translation" || ch.target_path == "scale") {
            renderer::animation::AnimationChannel<glm::vec3> c;
            c.joint_index = joint;
            c.times = times;
            c.interp = kind;
            readAccessor(m, sampler.output, c.values);
            if (ch.target_path == "translation")
              clip.translation_channels.push_back(std::move(c));
            else
              clip.scale_channels.push_back(std::move(c));
          } else if (ch.target_path == "rotation") {
            renderer::animation::AnimationChannel<glm::quat> c;
            c.joint_index = joint;
            c.times = times;
            c.interp = kind;
            // glTF stores quat as (x, y, z, w) vec4s in the accessor.
            std::vector<glm::vec4> raw;
            readAccessor(m, sampler.output, raw);
            c.values.reserve(raw.size());
            for (const auto& q : raw)
              c.values.emplace_back(q.w, q.x, q.y, q.z);
            clip.rotation_channels.push_back(std::move(c));
          }
        }
        clip.duration_seconds = dur;
        out.animations.push_back(std::move(clip));
      }
    }

    /// After extractMeshes + extractNodes, link each skinned mesh primitive to
    /// the skin its referencing node specifies. glTF binds skin via `node.skin`,
    /// so we walk nodes and tag any primitives belonging to the node's mesh.
    void linkMeshSkins(const tinygltf::Model& m, ModelData& out) {
      // Build mesh-index → primitive-range in out.meshes (each tinygltf mesh
      // flattens to its primitive count consecutive MeshData entries).
      std::vector<std::pair<size_t, size_t>> mesh_range(m.meshes.size());
      size_t cursor = 0;
      for (size_t i = 0; i < m.meshes.size(); ++i) {
        size_t n = m.meshes[i].primitives.size();
        mesh_range[i] = {cursor, cursor + n};
        cursor += n;
      }
      for (const auto& n : m.nodes) {
        if (n.mesh < 0 || n.skin < 0)
          continue;
        auto [lo, hi] = mesh_range[n.mesh];
        for (size_t i = lo; i < hi; ++i)
          out.meshes[i].skin_id = n.skin;
      }
    }

    /// Remap each skinned mesh's JOINTS_0 indices from glTF `skin.joints` order to
    /// the Skeleton's TOPOLOGICAL order. extractSkins resorts joints (parent before
    /// child) and extractAnimations remaps channel targets accordingly — but the
    /// mesh joint_indices are read raw from JOINTS_0, which index `skin.joints`.
    /// Without this pass, any model whose skin.joints aren't already topologically
    /// ordered skins to garbage. Must run AFTER extractSkins + linkMeshSkins (so
    /// skin_id is set) and BEFORE bakeNodeTransforms (so clones inherit remapped).
    void remapSkinnedMeshJoints(const tinygltf::Model& m, ModelData& out) {
      for (size_t s = 0; s < m.skins.size(); ++s) {
        const auto& sk = m.skins[s];
        const size_t N = sk.joints.size();
        if (N == 0)
          continue;
        // Same topological sort as extractSkins → remap[orig_joint] = topo_index.
        std::vector<int> parent_of(N, -1);
        for (size_t i = 0; i < N; ++i) {
          for (size_t j = 0; j < N; ++j) {
            const auto& nj = m.nodes[sk.joints[j]];
            if (std::find(nj.children.begin(), nj.children.end(), sk.joints[i]) != nj.children.end()) {
              parent_of[i] = static_cast<int>(j);
              break;
            }
          }
        }
        std::vector<int> order;
        std::vector<bool> placed(N, false);
        for (size_t i = 0; i < N; ++i)
          if (parent_of[i] == -1) {
            order.push_back(static_cast<int>(i));
            placed[i] = true;
          }
        for (size_t h = 0; h < order.size(); ++h)
          for (size_t i = 0; i < N; ++i)
            if (!placed[i] && parent_of[i] == order[h]) {
              order.push_back(static_cast<int>(i));
              placed[i] = true;
            }
        std::vector<int> remap(N, 0);
        for (size_t topo = 0; topo < order.size(); ++topo)
          remap[order[topo]] = static_cast<int>(topo);

        auto mapIdx = [&](uint32_t v) -> uint32_t {
          return v < N ? static_cast<uint32_t>(remap[v]) : 0u;
        };
        for (auto& mesh : out.meshes) {
          if (mesh.skin_id != static_cast<int>(s))
            continue;
          for (auto& ji : mesh.joint_indices)
            ji = {mapIdx(ji.x), mapIdx(ji.y), mapIdx(ji.z), mapIdx(ji.w)};
        }
      }
    }

    // 节点世界变换烘焙:glTF 顶点在节点局部空间,场景树 matrix/TRS 决定摆放。
    // 单物体资产通常恒等所以一直没暴露;城市级 glb(实测 3676 节点带平移
    // 矩阵)不烘焙会把所有街区塌到原点(编辑器里只见 bbox 不见城)。
    // 规则:同一 mesh 被多节点引用时第一处就地烘焙、其余克隆追加;蒙皮网格
    // 跳过(顶点在绑定空间,由 joint 矩阵驱动)。
    void bakeNodeTransforms(const tinygltf::Model& m, ModelData& out) {
      if (m.nodes.empty() || m.scenes.empty())
        return;

      std::vector<std::pair<size_t, size_t>> mesh_range(m.meshes.size());
      size_t cursor = 0;
      for (size_t i = 0; i < m.meshes.size(); ++i) {
        const size_t n = m.meshes[i].primitives.size();
        mesh_range[i] = {cursor, cursor + n};
        cursor += n;
      }

      auto localMatrix = [](const tinygltf::Node& n) -> glm::dmat4 {
        if (n.matrix.size() == 16) {
          glm::dmat4 M;
          for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
              M[c][r] = n.matrix[c * 4 + r];
          return M;
        }
        glm::dmat4 M(1.0);
        if (n.translation.size() == 3) {
          M = glm::translate(M, glm::dvec3(n.translation[0], n.translation[1], n.translation[2]));
        }
        if (n.rotation.size() == 4) { // glTF 四元数 xyzw
          M *= glm::mat4_cast(glm::dquat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]));
        }
        if (n.scale.size() == 3) {
          M = glm::scale(M, glm::dvec3(n.scale[0], n.scale[1], n.scale[2]));
        }
        return M;
      };

      std::vector<std::vector<glm::dmat4>> instances(m.meshes.size());
      std::function<void(int, const glm::dmat4&)> walk = [&](int ni, const glm::dmat4& parent) {
        if (ni < 0 || ni >= static_cast<int>(m.nodes.size()))
          return;
        const auto& n = m.nodes[ni];
        const glm::dmat4 W = parent * localMatrix(n);
        if (n.mesh >= 0 && n.mesh < static_cast<int>(m.meshes.size()) && n.skin < 0) {
          instances[n.mesh].push_back(W);
        }
        for (int c : n.children)
          walk(c, W);
      };
      const int si = m.defaultScene >= 0 ? m.defaultScene : 0;
      for (int root : m.scenes[si].nodes)
        walk(root, glm::dmat4(1.0));

      auto isIdentity = [](const glm::dmat4& M) {
        const glm::dmat4 I(1.0);
        for (int c = 0; c < 4; ++c)
          for (int r = 0; r < 4; ++r)
            if (std::abs(M[c][r] - I[c][r]) > 1e-9)
              return false;
        return true;
      };
      auto apply = [&](MeshData& md, const glm::dmat4& W) {
        if (isIdentity(W))
          return;
        const glm::mat4 Wf(W);
        const glm::mat3 N = glm::transpose(glm::inverse(glm::mat3(Wf)));
        for (auto& pos : md.positions)
          pos = glm::vec3(Wf * glm::vec4(pos, 1.0f));
        for (auto& nrm : md.normals) {
          const glm::vec3 t = N * nrm;
          const float len = glm::length(t);
          if (len > 1e-8f)
            nrm = t / len;
        }
      };

      for (size_t mi = 0; mi < instances.size(); ++mi) {
        const auto& mats = instances[mi];
        if (mats.empty())
          continue;
        const auto [lo, hi] = mesh_range[mi];
        for (size_t k = 1; k < mats.size(); ++k) { // 实例 2..N:克隆烘焙
          for (size_t i = lo; i < hi; ++i) {
            MeshData clone = out.meshes[i];
            apply(clone, mats[k]);
            out.meshes.push_back(std::move(clone));
          }
        }
        for (size_t i = lo; i < hi; ++i)
          apply(out.meshes[i], mats[0]);
      }
    }

    void extractMaterials(const tinygltf::Model& m, ModelData& out) {
      for (const auto& mat : m.materials) {
        MaterialData md;
        md.name = mat.name;
        const auto& pbr = mat.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() == 4) {
          md.base_color_factor = glm::vec4(static_cast<float>(pbr.baseColorFactor[0]),
                                           static_cast<float>(pbr.baseColorFactor[1]),
                                           static_cast<float>(pbr.baseColorFactor[2]),
                                           static_cast<float>(pbr.baseColorFactor[3]));
        }
        md.metallic_factor = static_cast<float>(pbr.metallicFactor);
        md.roughness_factor = static_cast<float>(pbr.roughnessFactor);
        md.base_color_texture = pbr.baseColorTexture.index;
        md.metallic_roughness_texture = pbr.metallicRoughnessTexture.index;
        md.normal_texture = mat.normalTexture.index;
        // 解析 emissiveFactor + KHR_materials_emissive_strength 扩展。
        if (mat.emissiveFactor.size() == 3) {
          md.emissive_factor = glm::vec3(static_cast<float>(mat.emissiveFactor[0]),
                                         static_cast<float>(mat.emissiveFactor[1]),
                                         static_cast<float>(mat.emissiveFactor[2]));
        }
        if (auto it = mat.extensions.find("KHR_materials_emissive_strength");
            it != mat.extensions.end() && it->second.Has("emissiveStrength")) {
          md.emissive_factor *= static_cast<float>(it->second.Get("emissiveStrength").GetNumberAsDouble());
        }
        md.emissive_texture = mat.emissiveTexture.index;
        out.materials.push_back(std::move(md));
      }
    }

    void extractTextures(const tinygltf::Model& m, ModelData& out) {
      // Copy decoded RGBA8 pixels from each referenced image into TextureData.
      // tinygltf has already invoked our stb_image_loader by this point (during
      // LoadASCII/LoadBinary), so `img.image` is the decoded byte buffer.
      for (const auto& tex : m.textures) {
        TextureData td;
        if (tex.source >= 0 && tex.source < static_cast<int>(m.images.size())) {
          const auto& img = m.images[tex.source];
          td.uri = img.uri;
          td.width = img.width;
          td.height = img.height;
          td.channels = img.component;
          td.pixels = img.image; // already RGBA8 thanks to req_comp=4
        }
        out.textures.push_back(std::move(td));
      }
    }

    void extractNodes(const tinygltf::Model& m, ModelData& out) {
      out.nodes.reserve(m.nodes.size());
      for (const auto& n : m.nodes) {
        NodeData nd;
        nd.name = n.name;
        nd.children = n.children;
        nd.mesh = n.mesh;

        // glTF defines TRS as optional individual fields or a single matrix.
        // Prefer TRS — if a matrix is present we leave TRS at default; the
        // caller can decompose if needed.
        if (n.translation.size() == 3) {
          nd.translation = glm::dvec3(n.translation[0], n.translation[1], n.translation[2]);
        }
        if (n.rotation.size() == 4) {
          // glTF stores rotation as (x, y, z, w); glm::quat ctor is (w, x, y, z).
          nd.rotation = glm::quat(static_cast<float>(n.rotation[3]),
                                  static_cast<float>(n.rotation[0]),
                                  static_cast<float>(n.rotation[1]),
                                  static_cast<float>(n.rotation[2]));
        }
        if (n.scale.size() == 3) {
          nd.scale =
              glm::vec3(static_cast<float>(n.scale[0]), static_cast<float>(n.scale[1]), static_cast<float>(n.scale[2]));
        }
        // glTF allows a node's transform as a single 4x4 matrix instead of TRS.
        // Decompose it so NodeData (TRS) is always meaningful — findNodeWorldTransform
        // composes from TRS, and attachment empties exported with a matrix would
        // otherwise resolve to identity.
        if (n.matrix.size() == 16) {
          glm::dmat4 M;
          for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
              M[c][r] = n.matrix[c * 4 + r];
          glm::vec3 t, s, skew;
          glm::quat q;
          glm::vec4 persp;
          if (glm::decompose(glm::mat4(M), s, q, t, skew, persp)) {
            nd.translation = glm::dvec3(t);
            nd.rotation = q;
            nd.scale = s;
          }
        }
        out.nodes.push_back(std::move(nd));
      }

      // Pick scene roots. glTF can carry multiple scenes — we use the default
      // one if defined, otherwise scene 0, otherwise fall back to all top-level
      // nodes (those not referenced as a child anywhere).
      int scene_idx = m.defaultScene >= 0 ? m.defaultScene : (m.scenes.empty() ? -1 : 0);
      if (scene_idx >= 0) {
        out.root_nodes = m.scenes[scene_idx].nodes;
      } else {
        std::vector<bool> referenced(m.nodes.size(), false);
        for (const auto& n : m.nodes)
          for (int c : n.children)
            if (c >= 0 && c < static_cast<int>(referenced.size()))
              referenced[c] = true;
        for (size_t i = 0; i < m.nodes.size(); ++i)
          if (!referenced[i])
            out.root_nodes.push_back(static_cast<int>(i));
      }
    }

  } // namespace

  std::optional<glm::mat4> ModelData::findNodeWorldTransform(const std::string& name) const {
    std::optional<glm::mat4> found;
    std::function<bool(int, const glm::mat4&)> walk = [&](int ni, const glm::mat4& parent) -> bool {
      if (ni < 0 || ni >= static_cast<int>(nodes.size()))
        return false;
      const NodeData& n = nodes[ni];
      const glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(n.translation)) * glm::mat4_cast(n.rotation) *
                              glm::scale(glm::mat4(1.0f), n.scale);
      const glm::mat4 world = parent * local;
      if (n.name == name) {
        found = world;
        return true;
      }
      for (int c : n.children)
        if (walk(c, world))
          return true;
      return false;
    };
    for (int r : root_nodes)
      if (walk(r, glm::mat4(1.0f)))
        break;
    return found;
  }

  namespace {

    // Configure the stb-based image loader callback. Shared between path-based
    // and in-memory loading; tinygltf invokes this whenever it hits an embedded
    // or referenced image buffer (PNG/JPG bytes).
    void installStbImageLoader(tinygltf::TinyGLTF& ctx) {
      auto stb_image_loader = [](tinygltf::Image* image,
                                 const int /*idx*/,
                                 std::string* err,
                                 std::string* /*warn*/,
                                 int /*req_w*/,
                                 int /*req_h*/,
                                 const unsigned char* bytes,
                                 int size,
                                 void* /*user*/) -> bool {
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
        if (!pixels) {
          if (err)
            *err = stbi_failure_reason() ? stbi_failure_reason() : "stb decode failed";
          return false;
        }
        image->width = w;
        image->height = h;
        image->component = 4;
        image->bits = 8;
        image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        image->image.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
        return true;
      };
      ctx.SetImageLoader(stb_image_loader, nullptr);
    }

  } // namespace

  std::optional<ModelData> GltfLoader::loadFromMemory(const unsigned char* bytes,
                                                      size_t size,
                                                      bool binary,
                                                      const std::string& base_dir) {
    last_error_.clear();
    last_warning_.clear();

    if (!bytes || size == 0) {
      last_error_ = "GltfLoader::loadFromMemory: empty buffer";
      return std::nullopt;
    }

    tinygltf::Model raw;
    tinygltf::TinyGLTF ctx;
    std::string err, warn;
    installStbImageLoader(ctx);

    const bool ok =
        binary
            ? ctx.LoadBinaryFromMemory(&raw, &err, &warn, bytes, static_cast<unsigned int>(size), base_dir)
            : ctx.LoadASCIIFromString(
                  &raw, &err, &warn, reinterpret_cast<const char*>(bytes), static_cast<unsigned int>(size), base_dir);

    last_warning_ = warn;
    if (!ok) {
      last_error_ = err.empty() ? "tinygltf: unknown error" : err;
      return std::nullopt;
    }

    ModelData out;
    out.source_path = "<memory>";
    extractMeshes(raw, out);
    extractMaterials(raw, out);
    extractTextures(raw, out);
    extractNodes(raw, out);
    extractSkins(raw, out);
    linkMeshSkins(raw, out);
    remapSkinnedMeshJoints(raw, out);
    bakeNodeTransforms(raw, out); // 必须在 linkMeshSkins 后(克隆继承 skin_id)
    extractAnimations(raw, out);
    return out;
  }

  std::optional<ModelData> GltfLoader::load(const std::string& path) {
    last_error_.clear();
    last_warning_.clear();

    tinygltf::Model raw;
    tinygltf::TinyGLTF ctx;
    std::string err, warn;

    installStbImageLoader(ctx);

    // Container detection: extension is the cheap path. Fall back to a magic
    // sniff so users with unusual filenames (or omitted extensions) still work.
    const bool ext_glb = endsWithIgnoreCase(path, ".glb");
    const bool ext_gltf = endsWithIgnoreCase(path, ".gltf");
    const bool binary = ext_glb || (!ext_gltf && isBinaryGlb(path));

    const bool ok =
        binary ? ctx.LoadBinaryFromFile(&raw, &err, &warn, path) : ctx.LoadASCIIFromFile(&raw, &err, &warn, path);

    last_warning_ = warn;
    if (!ok) {
      last_error_ = err.empty() ? "tinygltf: unknown error" : err;
      return std::nullopt;
    }

    ModelData out;
    out.source_path = path;
    extractMeshes(raw, out);
    extractMaterials(raw, out);
    extractTextures(raw, out);
    extractNodes(raw, out);
    extractSkins(raw, out);
    linkMeshSkins(raw, out);
    remapSkinnedMeshJoints(raw, out);
    bakeNodeTransforms(raw, out); // 必须在 linkMeshSkins 后(克隆继承 skin_id)
    extractAnimations(raw, out);
    return out;
  }

} // namespace hongtu::asset
