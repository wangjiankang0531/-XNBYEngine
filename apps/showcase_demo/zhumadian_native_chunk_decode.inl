#include "zhumadian_owned_worker.inl"
// Complete source frontier split into bounded geometry/atlas uploads.
ZhumadianNativeCpu decodeZhumadianNativeChunks(int tile, int depth, bool buildPicking = false) {
  ZhumadianNativeCpu result;
  try {
#ifdef _WIN32
    const fs::path repo = resolveProjectPath("tools/convert_zhumadian_native_chunks.py").parent_path().parent_path();
    const char* configured = std::getenv("HT_NATIVE_PYTHON");
    const char* profile = std::getenv("USERPROFILE");
    const fs::path portablePython = repo / "runtime/python/python.exe";
    const fs::path python = configured ? fs::path(configured)
                            : fs::exists(portablePython)
                                ? portablePython
                                : fs::path(profile ? profile : "") /
                                      ".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
    if (!fs::exists(python))
      throw std::runtime_error("Native chunk decoder Python runtime unavailable");
    std::wstring command = L"\"" + python.wstring() + L"\" -X utf8 \"" +
                           (repo / "tools/convert_zhumadian_native_chunks.py").wstring() + L"\" --tile=" +
                           std::to_wstring(tile) + L" --depth=" + std::to_wstring(depth);
    const DWORD code = runOwnedNativeChunkWorker(python, command, repo, tile, depth);
    const fs::path folder = repo / "work/zhumadian_native_chunk_cache" /
                            ("tile_" + std::to_string(tile) + "_depth_" + std::to_string(depth));
    if (code != 0) {
      nlohmann::json failure;
      std::ifstream error(folder / "failure.json");
      if (error) {
        error >> failure;
        throw std::runtime_error(failure.at("reason").get<std::string>());
      }
      throw std::runtime_error("Native chunk frontier conversion failed or timed out");
    }
    nlohmann::json stamp;
    std::ifstream input(folder / "complete.json");
    input >> stamp;
    if (stamp.at("schema") != "hongtu.osgb_native_chunk_bundle.v1" || stamp.at("tile_id") != tile ||
        stamp.at("depth") != depth || !stamp.at("complete_frontier").get<bool>() || stamp.at("parts").empty() ||
        stamp.at("parts").size() > 64)
      throw std::runtime_error("Incomplete native chunk frontier marker");
    uint64_t indices = 0;
    auto picking =
        buildPicking ? std::make_shared<hongtu::renderer::TriangleBvh>(stamp.at("indices").get<size_t>() / 3) : nullptr;
    for (const auto& item : stamp.at("parts")) {
      const std::string directory = item.at("directory");
      if (!std::regex_match(directory, std::regex("part_[0-9]{3}")))
        throw std::runtime_error("Native part path escapes bundle");
      const auto partFolder = folder / directory;
      ZhumadianNativeCpu part;
      part.payload = item.at("gpu_payload_bytes");
      if (!part.payload || part.payload > 32ull * 1024 * 1024)
        throw std::runtime_error("Native part exceeds upload limit");
      hongtu::asset::GltfLoader loader;
      auto model = loader.load((partFolder / "geometry.gltf").string());
      if (!model || model->meshes.size() != 1)
        throw std::runtime_error("Native part glTF decode failed");
      const auto& mesh = model->meshes.front();
      if (mesh.positions.size() != item.at("vertices").get<size_t>() ||
          mesh.indices.size() != item.at("indices").get<size_t>() || mesh.indices.empty() || mesh.indices.size() % 3 ||
          mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size())
        throw std::runtime_error("Native part attributes/counts changed");
      for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const auto p = mesh.positions[i], n = mesh.normals[i];
        const auto uv = mesh.uvs[i];
        if (!std::isfinite(p.x + p.y + p.z + n.x + n.y + n.z + uv.x + uv.y))
          throw std::runtime_error("Nonfinite native part geometry");
        part.vertices.push_back({p,
                                 {1, 1, 1},
                                 glm::length(n) > .001f ? glm::normalize(n) : glm::vec3(0, 0, 1),
                                 uv,
                                 1.5f,
                                 {-2, 0, .92f, 5}});
      }
      part.indices = mesh.indices;
      if (std::any_of(
              part.indices.begin(), part.indices.end(), [&](uint32_t i) { return i >= part.vertices.size(); }) ||
          !loadRgbaImage(partFolder / "atlas.jpg", part.image) || part.image.width != 1024 || part.image.height != 1024)
        throw std::runtime_error("Native part indices/image changed");
      if (picking)
        for (size_t i = 0; i < part.indices.size(); i += 3)
          picking->addTriangle(part.vertices[part.indices[i]].position,
                               part.vertices[part.indices[i + 1]].position,
                               part.vertices[part.indices[i + 2]].position);
      result.payload += part.payload;
      indices += part.indices.size();
      result.parts.push_back(std::move(part));
    }
    if (result.payload != stamp.at("gpu_payload_bytes").get<uint64_t>() ||
        indices != stamp.at("indices").get<uint64_t>() || result.payload > 192ull * 1024 * 1024)
      throw std::runtime_error("Native parts do not form the complete bounded frontier");
    result.depth = depth;
    result.edge = stamp.at("longest_edge_p90_m");
    result.frontier = stamp.at("frontier_count");
    if (picking) {
      picking->build();
      if (picking->triangleCount() != indices / 3)
        throw std::runtime_error("Picking mesh omits native frontier triangles");
      result.picking = std::move(picking);
    }
    result.valid = std::isfinite(result.edge) && result.edge > 0 && result.frontier > 0;
#else
    throw std::runtime_error("Native chunk decoder requires Windows OSGB runtime");
#endif
  } catch (const std::exception& error) {
    result = {};
    result.error = error.what();
  }
  return result;
}
