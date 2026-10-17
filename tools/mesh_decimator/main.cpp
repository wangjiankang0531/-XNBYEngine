// mesh_decimator — glTF → decimated .glb, preserving UVs/normals.
//
//   mesh_decimator in.gltf out.glb --ratio=0.05 [--error=0.01] [--keep=leaves:0.15]
//
// Per-primitive meshoptimizer quadric simplification with attribute weighting
// (meshopt_simplifyWithAttributes), so leaf cards keep their UV mapping and
// the alpha-tested foliage silhouette survives. Textures are embedded into
// the output .glb (single self-contained runtime asset).
//
// --keep=NAME:RATIO overrides the ratio for materials whose name contains
// NAME (case-insensitive) — foliage usually needs a gentler ratio than the
// trunk, whose dense photoscan geometry collapses well.

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Args {
  std::string in_path;
  std::string out_path;
  float       ratio = 0.05f;   // default 20:1
  float       error = 0.01f;   // meshopt target error (relative)
  bool        sloppy = false;  // simplifySloppy: ignores topology/attribute
                               // seams — crushes stubborn foliage for far LODs
  std::vector<std::pair<std::string, float>> keep;  // material-name → ratio
};

bool parseArgs(int argc, char** argv, Args& a) {
  std::vector<std::string> pos;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--ratio=", 0) == 0) {
      a.ratio = std::stof(s.substr(8));
    } else if (s.rfind("--error=", 0) == 0) {
      a.error = std::stof(s.substr(8));
    } else if (s == "--sloppy") {
      a.sloppy = true;
    } else if (s.rfind("--keep=", 0) == 0) {
      std::string kv = s.substr(7);
      auto colon = kv.find(':');
      if (colon == std::string::npos) return false;
      a.keep.emplace_back(kv.substr(0, colon), std::stof(kv.substr(colon + 1)));
    } else {
      pos.push_back(s);
    }
  }
  if (pos.size() != 2) return false;
  a.in_path  = pos[0];
  a.out_path = pos[1];
  return true;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Read an accessor into a contiguous float array (handles interleaved views).
std::vector<float> readFloats(const tinygltf::Model& m, int accessor_idx,
                              int components) {
  const auto& acc  = m.accessors[accessor_idx];
  const auto& view = m.bufferViews[acc.bufferView];
  const auto& buf  = m.buffers[view.buffer];
  const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset;
  size_t stride = view.byteStride ? view.byteStride
                                  : sizeof(float) * static_cast<size_t>(components);
  std::vector<float> out(acc.count * components);
  for (size_t i = 0; i < acc.count; ++i) {
    std::memcpy(&out[i * components], base + i * stride,
                sizeof(float) * components);
  }
  return out;
}

std::vector<unsigned int> readIndices(const tinygltf::Model& m, int accessor_idx) {
  const auto& acc  = m.accessors[accessor_idx];
  const auto& view = m.bufferViews[acc.bufferView];
  const auto& buf  = m.buffers[view.buffer];
  const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset;
  std::vector<unsigned int> out(acc.count);
  for (size_t i = 0; i < acc.count; ++i) {
    switch (acc.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        out[i] = reinterpret_cast<const unsigned int*>(base)[i];
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        out[i] = reinterpret_cast<const unsigned short*>(base)[i];
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        out[i] = base[i];
        break;
      default:
        std::fprintf(stderr, "unsupported index component type\n");
        std::exit(2);
    }
  }
  return out;
}

// Append raw bytes to model.buffers[0], returning the accessor index that
// describes them. Creates one bufferView per accessor (simple, valid glTF).
int appendAccessor(tinygltf::Model& m, const void* data, size_t count,
                   int component_type, int type, int components,
                   bool is_index) {
  if (m.buffers.empty()) m.buffers.emplace_back();
  auto& buf = m.buffers[0];
  size_t elem =
      (component_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? 4u
                                                              : sizeof(float)) *
      static_cast<size_t>(components);
  // 4-byte align
  while (buf.data.size() % 4) buf.data.push_back(0);

  tinygltf::BufferView view;
  view.buffer     = 0;
  view.byteOffset = buf.data.size();
  view.byteLength = elem * count;
  view.target     = is_index ? TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER
                             : TINYGLTF_TARGET_ARRAY_BUFFER;
  const auto* p = reinterpret_cast<const unsigned char*>(data);
  buf.data.insert(buf.data.end(), p, p + view.byteLength);
  m.bufferViews.push_back(view);

  tinygltf::Accessor acc;
  acc.bufferView    = static_cast<int>(m.bufferViews.size()) - 1;
  acc.byteOffset    = 0;
  acc.componentType = component_type;
  acc.count         = count;
  acc.type          = type;

  if (!is_index && components == 3) {
    // POSITION requires min/max in glTF.
    const float* f = reinterpret_cast<const float*>(data);
    std::vector<double> mn(3, 1e30), mx(3, -1e30);
    for (size_t i = 0; i < count; ++i) {
      for (int c = 0; c < 3; ++c) {
        mn[c] = std::min(mn[c], static_cast<double>(f[i * 3 + c]));
        mx[c] = std::max(mx[c], static_cast<double>(f[i * 3 + c]));
      }
    }
    acc.minValues = mn;
    acc.maxValues = mx;
  }
  m.accessors.push_back(acc);
  return static_cast<int>(m.accessors.size()) - 1;
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    std::fprintf(stderr,
                 "usage: mesh_decimator in.gltf out.glb --ratio=R "
                 "[--error=E] [--keep=NAME:R]...\n");
    return 1;
  }

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;
  bool ok = args.in_path.size() > 4 &&
                    args.in_path.substr(args.in_path.size() - 4) == ".glb"
                ? loader.LoadBinaryFromFile(&model, &err, &warn, args.in_path)
                : loader.LoadASCIIFromFile(&model, &err, &warn, args.in_path);
  if (!ok) {
    std::fprintf(stderr, "load failed: %s\n", err.c_str());
    return 1;
  }
  if (!warn.empty()) std::fprintf(stderr, "warn: %s\n", warn.c_str());

  // Fresh output model: copy everything except mesh data containers, which
  // we rebuild (old buffers carry the heavy source geometry).
  tinygltf::Model out = model;
  out.buffers.clear();
  out.bufferViews.clear();
  out.accessors.clear();

  // Embed external texture files as raw bufferViews (mimeType + bytes) so the
  // output .glb is fully self-contained. tinygltf's embedImages would re-encode
  // to PNG (bloat); storing the original jpg bytes keeps size and quality.
  {
    std::string dir = ".";
    auto slash = args.in_path.find_last_of('/');
    if (slash != std::string::npos) dir = args.in_path.substr(0, slash);
    for (auto& img : out.images) {
      if (img.uri.empty()) continue;
      const std::string path = dir + "/" + img.uri;
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f) {
        std::fprintf(stderr, "texture missing: %s\n", path.c_str());
        return 1;
      }
      std::vector<unsigned char> bytes(static_cast<size_t>(f.tellg()));
      f.seekg(0);
      f.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));

      if (out.buffers.empty()) out.buffers.emplace_back();
      auto& buf = out.buffers[0];
      while (buf.data.size() % 4) buf.data.push_back(0);
      tinygltf::BufferView view;
      view.buffer     = 0;
      view.byteOffset = buf.data.size();
      view.byteLength = bytes.size();
      buf.data.insert(buf.data.end(), bytes.begin(), bytes.end());
      out.bufferViews.push_back(view);

      const bool png = img.uri.size() > 4 &&
                       img.uri.substr(img.uri.size() - 4) == ".png";
      img.bufferView = static_cast<int>(out.bufferViews.size()) - 1;
      img.mimeType   = png ? "image/png" : "image/jpeg";
      img.uri.clear();
      img.image.clear();
      img.width = img.height = img.component = -1;
    }
  }

  size_t tris_in = 0, tris_out = 0;
  for (auto& mesh : out.meshes) {
    // The simplifier operates on triangle lists. Assimp may preserve FBX
    // helper curves as line primitives; the runtime renderer does not draw
    // those and their two-index topology must never enter the triangle path.
    mesh.primitives.erase(
        std::remove_if(mesh.primitives.begin(), mesh.primitives.end(),
                       [](const tinygltf::Primitive& prim) {
                         return prim.mode != TINYGLTF_MODE_TRIANGLES ||
                                prim.indices < 0 ||
                                !prim.attributes.count("POSITION");
                       }),
        mesh.primitives.end());
    for (auto& prim : mesh.primitives) {
      auto pos = readFloats(model, prim.attributes.at("POSITION"), 3);
      auto idx = readIndices(model, prim.indices);
      std::vector<float> nrm, uv;
      if (prim.attributes.count("NORMAL"))
        nrm = readFloats(model, prim.attributes.at("NORMAL"), 3);
      if (prim.attributes.count("TEXCOORD_0"))
        uv = readFloats(model, prim.attributes.at("TEXCOORD_0"), 2);

      const size_t vcount = pos.size() / 3;
      tris_in += idx.size() / 3;

      // Per-material ratio override (e.g. keep more of the leaves).
      float ratio = args.ratio;
      if (prim.material >= 0) {
        std::string mat = lower(model.materials[prim.material].name);
        for (const auto& [name, r] : args.keep) {
          if (mat.find(lower(name)) != std::string::npos) ratio = r;
        }
      }

      // Interleave attributes for meshopt: weight UV strongly so leaf-card
      // texture mapping survives; normals get a light weight.
      std::vector<float> attrs;
      std::vector<float> attr_weights;
      size_t attr_stride = 0;
      if (!uv.empty())  { attr_stride += 2; }
      if (!nrm.empty()) { attr_stride += 3; }
      if (attr_stride) {
        attrs.resize(vcount * attr_stride);
        for (size_t v = 0; v < vcount; ++v) {
          size_t o = v * attr_stride;
          if (!uv.empty())  { attrs[o] = uv[v*2]; attrs[o+1] = uv[v*2+1]; o += 2; }
          if (!nrm.empty()) { attrs[o] = nrm[v*3]; attrs[o+1] = nrm[v*3+1]; attrs[o+2] = nrm[v*3+2]; }
        }
        if (!uv.empty())  { attr_weights.push_back(1.0f); attr_weights.push_back(1.0f); }
        if (!nrm.empty()) { attr_weights.insert(attr_weights.end(), {0.2f, 0.2f, 0.2f}); }
      }

      const size_t source_triangles = idx.size() / 3;
      const size_t requested_triangles =
          source_triangles * static_cast<size_t>(ratio * 1000) / 1000;
      const size_t target_triangles =
          std::min(source_triangles, std::max<size_t>(requested_triangles, 1));
      const size_t target = target_triangles * 3;
      std::vector<unsigned int> simplified(idx.size());
      float result_error = 0.0f;
      size_t new_count;
      if (target >= idx.size()) {
        simplified = idx;
        new_count = idx.size();
      } else if (args.sloppy) {
        // Topology-agnostic collapse — quality drops but it actually reaches
        // the target on photoscan foliage where edge-collapse stalls.
        new_count = meshopt_simplifySloppy(
            simplified.data(), idx.data(), idx.size(), pos.data(), vcount,
            sizeof(float) * 3, target, args.error, &result_error);
      } else {
        new_count = meshopt_simplifyWithAttributes(
            simplified.data(), idx.data(), idx.size(), pos.data(), vcount,
            sizeof(float) * 3,
            attrs.empty() ? nullptr : attrs.data(), sizeof(float) * attr_stride,
            attr_weights.empty() ? nullptr : attr_weights.data(), attr_weights.size(),
            nullptr, target, args.error, 0, &result_error);
      }
      if (new_count == 0) {
        simplified = idx;
        new_count = idx.size();
      }
      simplified.resize(new_count);

      // Compact: drop unreferenced vertices.
      std::vector<unsigned int> remap(vcount);
      size_t unique = meshopt_optimizeVertexFetchRemap(remap.data(),
                                                       simplified.data(),
                                                       new_count, vcount);
      std::vector<float> pos2(unique * 3), nrm2, uv2;
      meshopt_remapVertexBuffer(pos2.data(), pos.data(), vcount,
                                sizeof(float) * 3, remap.data());
      if (!nrm.empty()) {
        nrm2.resize(unique * 3);
        meshopt_remapVertexBuffer(nrm2.data(), nrm.data(), vcount,
                                  sizeof(float) * 3, remap.data());
      }
      if (!uv.empty()) {
        uv2.resize(unique * 2);
        meshopt_remapVertexBuffer(uv2.data(), uv.data(), vcount,
                                  sizeof(float) * 2, remap.data());
      }
      meshopt_remapIndexBuffer(simplified.data(), simplified.data(), new_count,
                               remap.data());
      meshopt_optimizeVertexCache(simplified.data(), simplified.data(),
                                  new_count, unique);

      tris_out += new_count / 3;

      // Write back into the fresh output model.
      prim.attributes["POSITION"] = appendAccessor(
          out, pos2.data(), unique, TINYGLTF_COMPONENT_TYPE_FLOAT,
          TINYGLTF_TYPE_VEC3, 3, false);
      if (!nrm2.empty())
        prim.attributes["NORMAL"] = appendAccessor(
            out, nrm2.data(), unique, TINYGLTF_COMPONENT_TYPE_FLOAT,
            TINYGLTF_TYPE_VEC3, 3, false);
      else
        prim.attributes.erase("NORMAL");
      if (!uv2.empty())
        prim.attributes["TEXCOORD_0"] = appendAccessor(
            out, uv2.data(), unique, TINYGLTF_COMPONENT_TYPE_FLOAT,
            TINYGLTF_TYPE_VEC2, 2, false);
      else
        prim.attributes.erase("TEXCOORD_0");
      // Drop any other attributes (tangents etc.) — runtime recomputes or
      // does without; keeping stale accessors would dangle.
      for (auto it = prim.attributes.begin(); it != prim.attributes.end();) {
        if (it->first != "POSITION" && it->first != "NORMAL" &&
            it->first != "TEXCOORD_0") {
          it = prim.attributes.erase(it);
        } else {
          ++it;
        }
      }
      prim.indices = appendAccessor(out, simplified.data(), new_count,
                                    TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                                    TINYGLTF_TYPE_SCALAR, 1, true);
    }
  }

  tinygltf::TinyGLTF writer;
  // embedImages=true → external texture files are pulled into the .glb.
  if (!writer.WriteGltfSceneToFile(&out, args.out_path,
                                   /*embedImages=*/false,
                                   /*embedBuffers=*/true,
                                   /*prettyPrint=*/false,
                                   /*writeBinary=*/true)) {
    std::fprintf(stderr, "write failed\n");
    return 1;
  }
  std::printf("%s: %.2fM → %.2fM tris (%.1f%%)\n", args.out_path.c_str(),
              tris_in / 1e6, tris_out / 1e6, 100.0 * tris_out / tris_in);
  return 0;
}
