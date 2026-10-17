// Included after demo geometry/image helpers, inside its implementation namespace.
// One worker decodes an entire descendant frontier. RHI work stays on the render
// thread, after its submission fence; parents remain until the whole root is ready.
struct ZhumadianNativeCpu {
  bool valid = false;
  int depth = 0;
  uint64_t payload = 0;
  float edge = 0;
  uint32_t frontier = 0;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  LoadedRgbaImage image;
  std::string error;
  std::vector<ZhumadianNativeCpu> parts;
  std::shared_ptr<const hongtu::renderer::TriangleBvh> picking;
};

#include "zhumadian_native_chunk_decode.inl"

ZhumadianNativeCpu decodeZhumadianNativeRoot(int tile, int depth, bool chunked = false, bool buildPicking = false) {
  if (chunked)
    return decodeZhumadianNativeChunks(tile, depth, buildPicking);
  ZhumadianNativeCpu result;
  try {
#ifdef _WIN32
    const fs::path repo = resolveProjectPath("tools/convert_zhumadian_native_root.py").parent_path().parent_path();
    const char* configured = std::getenv("HT_NATIVE_PYTHON");
    const char* profile = std::getenv("USERPROFILE");
    const fs::path python = configured
                                ? fs::path(configured)
                                : fs::path(profile ? profile : "") /
                                      ".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe";
    if (!fs::exists(python))
      throw std::runtime_error("Native decoder Python runtime unavailable; set HT_NATIVE_PYTHON");
    std::wstring command = L"\"" + python.wstring() + L"\" \"" +
                           (repo / "tools/convert_zhumadian_native_root.py").wstring() + L"\" --tile=" +
                           std::to_wstring(tile) + L" --depth=" + std::to_wstring(depth) + L" --atlas-size=1024";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(python.c_str(),
                        command.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        repo.c_str(),
                        &start,
                        &child))
      throw std::runtime_error("Cannot start native decoder");
    CloseHandle(child.hThread);
    const DWORD wait = WaitForSingleObject(child.hProcess, 120000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0)
      GetExitCodeProcess(child.hProcess, &code);
    else {
      // Only this worker's own decoder is terminated, never a user demo.
      TerminateProcess(child.hProcess, 1);
      WaitForSingleObject(child.hProcess, 5000);
    }
    CloseHandle(child.hProcess);
    const fs::path folder = repo / "work/zhumadian_native_cache" /
                            ("tile_" + std::to_string(tile) + "_depth_" + std::to_string(depth) + "_atlas_1024");
    if (code != 0) {
      nlohmann::json failure;
      std::ifstream error(folder / "failure.json");
      if (error) {
        error >> failure;
        throw std::runtime_error(failure.at("reason").get<std::string>());
      }
      throw std::runtime_error("Source frontier conversion/validation failed or timed out");
    }
    nlohmann::json stamp;
    std::ifstream input(folder / "complete.json");
    input >> stamp;
    if (stamp.at("tile_id") != tile || stamp.at("depth") != depth || !stamp.at("complete_frontier").get<bool>())
      throw std::runtime_error("Incomplete native frontier marker");
    result.payload = stamp.at("gpu_payload_bytes");
    if (!result.payload || result.payload > 32ull * 1024 * 1024)
      throw std::runtime_error("Native root exceeds upload limit");
    hongtu::asset::GltfLoader loader;
    auto model = loader.load((folder / "geometry.gltf").string());
    if (!model || model->meshes.size() != 1)
      throw std::runtime_error("Native glTF decode failed");
    const auto& mesh = model->meshes.front();
    if (mesh.positions.size() != stamp.at("vertices").get<size_t>() ||
        mesh.indices.size() != stamp.at("indices").get<size_t>() || mesh.indices.empty() || mesh.indices.size() % 3 ||
        mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size())
      throw std::runtime_error("Native attributes/counts changed");
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
      const auto p = mesh.positions[i], n = mesh.normals[i];
      const auto uv = mesh.uvs[i];
      if (!std::isfinite(p.x + p.y + p.z + n.x + n.y + n.z + uv.x + uv.y))
        throw std::runtime_error("Nonfinite native geometry");
      result.vertices.push_back({p,
                                 {1, 1, 1},
                                 glm::length(n) > 0.001f ? glm::normalize(n) : glm::vec3(0, 0, 1),
                                 uv,
                                 1.5f,
                                 {-2, 0, 0.92f, 5}});
    }
    result.indices = mesh.indices;
    if (std::any_of(
            result.indices.begin(), result.indices.end(), [&](uint32_t i) { return i >= result.vertices.size(); }) ||
        !loadRgbaImage(folder / "atlas.jpg", result.image) || result.image.width != 1024 || result.image.height != 1024)
      throw std::runtime_error("Native indices/image changed");
    result.depth = depth;
    result.edge = stamp.at("longest_edge_p90_m");
    result.frontier = stamp.at("frontier_count");
    if (buildPicking) {
      auto picking = std::make_shared<hongtu::renderer::TriangleBvh>(result.indices.size() / 3);
      for (size_t i = 0; i < result.indices.size(); i += 3)
        picking->addTriangle(result.vertices[result.indices[i]].position,
                             result.vertices[result.indices[i + 1]].position,
                             result.vertices[result.indices[i + 2]].position);
      picking->build();
      result.picking = std::move(picking);
    }
    result.valid = std::isfinite(result.edge) && result.edge > 0 && result.frontier > 0;
#else
    throw std::runtime_error("Native converter currently requires the Windows OSGB runtime");
#endif
  } catch (const std::exception& error) {
    result = {};
    result.error = error.what();
  }
  return result;
}

struct ZhumadianNativePager {
  struct Root {
    int tile = 0, maxDepth = 0;
    float baseEdge = 0;
    std::vector<hongtu::asset::MeshIndexRange> spans;
    std::shared_ptr<const hongtu::renderer::TriangleBvh> picking;
  };
  struct Resident {
    RHIBufferPtr vertices, indices;
    RHITexturePtr texture;
    RHITextureViewPtr view;
    RHIDescriptorSetPtr descriptor;
    uint64_t payload = 0;
    uint32_t indexCount = 0, frontier = 0;
    int depth = 0;
    float edge = 0;
    std::vector<Resident> parts;
    std::shared_ptr<const hongtu::renderer::TriangleBvh> picking;
  };
  static constexpr uint64_t kUploadLimit = 32ull * 1024 * 1024;
  static constexpr size_t kResidentLimit = 8;
  static constexpr size_t kAdaptiveResidentLimit = 16;
  std::vector<Root> roots;
  std::vector<ZhumadianTexturePage> bounds;
  std::map<int, Resident> resident;
  std::map<int, int> desired, failed;
  std::vector<int> ranking;
  std::map<int, float> projectedEdges, projectedBaseEdges, distances;
  std::map<std::pair<int, int>, float> tierEdges;
  int focusId = -1;
  uint64_t coarsenings = 0, budgetFallbacks = 0;
  std::future<ZhumadianNativeCpu> pending;
  std::optional<ZhumadianNativeCpu> staged;
  Resident stagingResident;
  size_t stagingNext = 0;
  int stagingId = -1;
  bool chunked = false;
  bool pickingEnabled = false;
  std::shared_ptr<const hongtu::renderer::TriangleBvh> contextPicking;
  uint64_t partUploads = 0, stagingCancellations = 0, maxParts = 0;
  int pendingId = -1, pendingDepth = 0;
  uint32_t prefixIndices = 0, baseIndices = 9166851;
  uint64_t budget = 0, payload = 0, peakPayload = 0, uploads = 0, requests = 0, evictions = 0, stale = 0, failures = 0;
  uint64_t drawnBase = 0, drawnChild = 0, removed = 0, maxUpload = 0;
  std::string lastError;

  void clear() {
    if (pending.valid())
      pending.get();
    staged.reset();
    stagingResident = {};
    stagingId = -1;
    resident.clear();
    contextPicking.reset();
    for (auto& root : roots)
      root.picking.reset();
    pickingEnabled = false;
    desired.clear();
    payload = 0;
  }
  size_t residentLimit() const {
    return chunked ? kAdaptiveResidentLimit : kResidentLimit;
  }
  uint64_t rootBudget(int id) const {
    return chunked ? std::min<uint64_t>(budget / (id == focusId ? 2 : 8), (id == focusId ? 192ull : 64ull) * 1048576)
                   : kUploadLimit;
  }
  struct SurfacePick {
    hongtu::renderer::PickHit hit;
    int tile = 0, depth = 0;
    const char* source = "context";
  };
  std::optional<SurfacePick> pickRay(const hongtu::renderer::WorldRay& ray,
                                     bool baseOnly = false,
                                     hongtu::renderer::TriangleBvh::QueryStats* stats = nullptr) const {
    if (!pickingEnabled)
      return {};
    std::optional<SurfacePick> nearest;
    const auto query = [&](const auto& mesh, int tile, int depth, const char* source) {
      if (!mesh)
        throw std::logic_error("Visible landscape has no corresponding picking mesh");
      if (auto hit = mesh->intersect(ray, nearest ? nearest->hit.t : std::numeric_limits<double>::infinity(), stats))
        nearest = SurfacePick{*hit, tile, depth, source};
    };
    query(contextPicking, 0, 0, "context");
    for (size_t id = 0; id < roots.size(); ++id) {
      const auto ready = resident.find(static_cast<int>(id));
      // draw() excludes the ready root's entire base span. Picking must do so
      // too, including real holes in the child mesh; never pick hidden parents.
      if (!baseOnly && ready != resident.end())
        query(ready->second.picking, roots[id].tile, ready->second.depth, "native");
      else
        query(roots[id].picking, roots[id].tile, 0, "base");
    }
    return nearest;
  }
  nlohmann::json probePicks(const glm::mat4& viewProjection, int width, int height) const {
    if (!pickingEnabled)
      throw std::logic_error("Pick probe needs visible-surface BVHs");
    auto samples = nlohmann::json::array();
    const auto inverse = glm::inverse(glm::dmat4(viewProjection));
    uint64_t fineHits = 0, changed = 0;
    const auto point = [](glm::dvec3 p) {
      return nlohmann::json::array({p.x, p.y, p.z});
    };
    for (int y = 1; y <= 5; ++y)
      for (int x = 1; x <= 5; ++x) {
        const double px = width * x / 6.0, py = height * y / 6.0;
        // The Vulkan projection already flips Y for its positive viewport.
        const auto ray = hongtu::renderer::screenToWorldRay(px, height - py, width, height, inverse);
        hongtu::renderer::TriangleBvh::QueryStats stats;
        const auto start = std::chrono::steady_clock::now();
        const auto hit = pickRay(ray, false, &stats);
        const double millis =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto coarse = pickRay(ray, true);
        nlohmann::json sample = {{"pixel", {px, py}},
                                 {"ray_origin", point(ray.origin)},
                                 {"ray_direction", point(ray.direction)},
                                 {"query_ms", millis},
                                 {"bounds_tests", stats.boundsTests},
                                 {"triangle_tests", stats.triangleTests}};
        if (hit) {
          sample["hit"] = {{"point", point(hit->hit.point)},
                           {"distance", hit->hit.t},
                           {"tile", hit->tile},
                           {"depth", hit->depth},
                           {"source", hit->source}};
          if (hit->depth > 0)
            ++fineHits;
        }
        if (coarse) {
          sample["base_point"] = point(coarse->hit.point);
          if (hit && glm::distance(hit->hit.point, coarse->hit.point) > .01)
            ++changed;
        }
        samples.push_back(std::move(sample));
      }
    return {{"schema", "hongtu.landscape.visible_surface_pick.v1"},
            {"samples", samples},
            {"fine_hits", fineHits},
            {"different_from_base", changed},
            {"source_policy", "context plus exactly the complete base/native frontiers currently drawn"}};
  }
  void initialize(uint32_t indexCount,
                  uint64_t requestedBudget,
                  bool useChunks = false,
                  const std::vector<Vertex>& baseVertices = {},
                  const std::vector<uint32_t>& baseMeshIndices = {},
                  bool buildPicking = false) {
    clear();
    roots.clear();
    bounds.clear();
    failed.clear();
    chunked = useChunks;
    pickingEnabled = buildPicking;
    focusId = -1;
    coarsenings = budgetFallbacks = 0;
    tierEdges.clear();
    partUploads = stagingCancellations = maxParts = 0;
    nlohmann::json doc;
    std::ifstream input(resolveProjectPath("data/showcase/zhumadian_native_hierarchy.json"));
    input >> doc;
    if (doc.at("schema") != "hongtu.osgb_native_hierarchy.v1" || doc.at("base_root_count") != 2346 ||
        doc.at("base_indices") != baseIndices || doc.at("total_source_nodes") != 237453 || indexCount < baseIndices)
      throw std::runtime_error("Native hierarchy must preserve the complete audited source base");
    prefixIndices = indexCount - baseIndices;
    if (pickingEnabled) {
      // Exclude the sky shell: it is drawn, but not an editable ground surface.
      if (prefixIndices < 663552 || baseMeshIndices.size() != indexCount || prefixIndices % 3)
        throw std::runtime_error("Picking requires the complete landscape/context indices");
      auto picking = std::make_shared<hongtu::renderer::TriangleBvh>((prefixIndices - 663552) / 3);
      for (uint32_t i = 663552; i < prefixIndices; i += 3)
        picking->addTriangle(baseVertices.at(baseMeshIndices.at(i)).position,
                             baseVertices.at(baseMeshIndices.at(i + 1)).position,
                             baseVertices.at(baseMeshIndices.at(i + 2)).position);
      picking->build();
      contextPicking = std::move(picking);
    }
    budget = requestedBudget;
    if (budget < kUploadLimit)
      throw std::runtime_error("Native geometry/texture budget must be at least 32 MiB");
    std::vector<hongtu::asset::MeshIndexRange> spans;
    for (const auto& item : doc.at("root_records")) {
      Root root;
      root.tile = item.at("tile_id");
      root.maxDepth = item.at("max_depth");
      if (root.tile != static_cast<int>(roots.size()) + 1 || root.maxDepth < 1 || root.maxDepth > 32)
        throw std::runtime_error("Invalid native root ordering/depth");
      ZhumadianTexturePage box;
      for (int axis = 0; axis < 3; ++axis) {
        box.boundsMin[axis] = item.at("bounds_min").at(axis);
        box.boundsMax[axis] = item.at("bounds_max").at(axis);
        if (!std::isfinite(box.boundsMin[axis]) || !std::isfinite(box.boundsMax[axis]) ||
            box.boundsMin[axis] > box.boundsMax[axis])
          throw std::runtime_error("Invalid native root bounds");
      }
      for (const auto& span : item.at("base_spans")) {
        root.spans.push_back({span.at(0), span.at(1)});
        spans.push_back(root.spans.back());
      }
      if (chunked || pickingEnabled) {
        std::vector<float> edges;
        size_t triangleCount = 0;
        for (const auto& span : root.spans)
          triangleCount += span.count / 3;
        auto picking = pickingEnabled ? std::make_shared<hongtu::renderer::TriangleBvh>(triangleCount) : nullptr;
        for (const auto& span : root.spans)
          for (uint32_t j = 0; j < span.count; j += 3) {
            const auto a = baseVertices.at(baseMeshIndices.at(prefixIndices + span.first + j)).position;
            const auto b = baseVertices.at(baseMeshIndices.at(prefixIndices + span.first + j + 1)).position;
            const auto c = baseVertices.at(baseMeshIndices.at(prefixIndices + span.first + j + 2)).position;
            if (chunked)
              edges.push_back(std::max({glm::length(a - b), glm::length(b - c), glm::length(c - a)}));
            if (picking)
              picking->addTriangle(a, b, c);
          }
        if (chunked && edges.empty())
          throw std::runtime_error("Native base quality has no audited triangles");
        if (chunked) {
          const size_t p90 = std::min(edges.size() - 1, size_t(edges.size() * 0.9));
          std::nth_element(edges.begin(), edges.begin() + p90, edges.end());
          root.baseEdge = edges[p90];
        }
        if (picking) {
          picking->build();
          root.picking = std::move(picking);
        }
      }
      roots.push_back(std::move(root));
      bounds.push_back(box);
    }
    std::sort(spans.begin(), spans.end(), [](auto a, auto b) { return a.first < b.first; });
    if (roots.size() != 2346 || !hongtu::asset::retainedMeshIndexRanges(baseIndices, spans).empty())
      throw std::runtime_error("Native fallback partition has holes");
    uploads = requests = evictions = stale = failures = peakPayload = maxUpload = 0;
  }

  void service(RHIDevice* device,
               const ShowcaseMaterialTextureBinding& material,
               glm::vec3 camera,
               glm::vec3 target,
               float fov,
               float aspect,
               int height) {
    ranking = rankZhumadianVisibleBounds(bounds, camera, target, true, fov, aspect, !chunked);
    // Large nearby foreground boxes must not starve the actual view center.
    // Reserve three priorities for centers closest to the sight ray, then retain
    // projected-coverage ordering for the remaining bounded working set.
    const auto forward = glm::normalize(target - camera);
    auto centers = ranking;
    std::stable_sort(centers.begin(), centers.end(), [&](int a, int b) {
      const auto score = [&](int id) {
        const auto delta = (bounds[id].boundsMin + bounds[id].boundsMax) * 0.5f - camera;
        const float depth = glm::dot(delta, forward);
        if (depth <= 0)
          return std::numeric_limits<float>::max();
        return glm::dot(delta, delta) / (depth * depth) - 1.0f;
      };
      return score(a) < score(b);
    });
    if (chunked) {
      // Interactive target is a virtual point 500 m down the sight ray. Do not
      // mistake it for the inspected surface. Prefer the on-axis source center;
      // for near-collinear centers, inspect the nearer visible root first.
      std::stable_sort(centers.begin(), centers.end(), [&](int a, int b) {
        const auto score = [&](int id) {
          const auto delta = (bounds[id].boundsMin + bounds[id].boundsMax) * 0.5f - camera;
          const float depth = glm::dot(delta, forward);
          const float angle = depth <= 0 ? 1e6f : glm::dot(delta, delta) / (depth * depth) - 1.0f;
          return std::pair<int64_t, float>{int64_t(std::max(angle, 0.0f) * 100000.0f), glm::length(delta)};
        };
        return score(a) < score(b);
      });
    }
    centers.resize(std::min<size_t>(chunked ? 1 : 3, centers.size()));
    for (int id : ranking)
      if (std::find(centers.begin(), centers.end(), id) == centers.end())
        centers.push_back(id);
    ranking = std::move(centers);
    desired.clear();
    projectedEdges.clear();
    projectedBaseEdges.clear();
    distances.clear();
    for (int id : ranking) {
      const auto& box = bounds[id];
      const float distance =
          glm::length(glm::max(glm::max(box.boundsMin - camera, camera - box.boundsMax), glm::vec3(0)));
      const float screen =
          hongtu::renderer::projectedNativeEdge(glm::length(box.boundsMax - box.boundsMin), distance, fov, height);
      auto ready = resident.find(id);
      const int depth = ready == resident.end() ? 0 : ready->second.depth;
      const float edge = ready == resident.end()
                             ? 0
                             : hongtu::renderer::projectedNativeEdge(ready->second.edge, distance, fov, height);
      const float baseEdge = hongtu::renderer::projectedNativeEdge(roots[id].baseEdge, distance, fov, height);
      const auto coarse = tierEdges.find({id, depth - 1});
      const float parentEdge =
          coarse == tierEdges.end() ? 0 : hongtu::renderer::projectedNativeEdge(coarse->second, distance, fov, height);
      int next = chunked ? hongtu::renderer::selectAdaptiveNativeDepth(
                               screen, baseEdge, edge, depth, roots[id].maxDepth, parentEdge)
                         : hongtu::renderer::selectNativeGeometryDepth(screen, edge, depth, roots[id].maxDepth);
      if (next && desired.size() < residentLimit()) {
        desired[id] = next;
        projectedEdges[id] = depth ? edge : baseEdge;
        projectedBaseEdges[id] = baseEdge;
        distances[id] = distance;
      }
    }
    if (chunked) {
      const auto first = std::find_if(ranking.begin(), ranking.end(), [&](int id) { return desired.contains(id); });
      const int nextFocus = first == ranking.end() ? -1 : *first;
      if (nextFocus != focusId) {
        focusId = nextFocus;
        // A previously lower-priority block can now use the inspection quota.
        // Its earlier budget fallback must not permanently suppress refinement.
        failed.clear();
      }
      for (auto& [id, depth] : desired)
        if (resident.contains(id) && resident.at(id).payload > rootBudget(id) && resident.at(id).depth > 1)
          depth = resident.at(id).depth - 1;
    }
    for (auto it = failed.begin(); it != failed.end();)
      if (!desired.contains(it->first))
        it = failed.erase(it);
      else
        ++it;
    for (auto it = resident.begin(); it != resident.end();)
      if (!desired.contains(it->first)) {
        payload -= it->second.payload;
        it = resident.erase(it);
        ++evictions;
      } else
        ++it;
    if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      auto cpu = pending.get();
      const int id = pendingId;
      pendingId = -1;
      if (!desired.contains(id) || desired.at(id) != pendingDepth) {
        ++stale;
      } else if (!cpu.valid) {
        failed[id] = pendingDepth;
        lastError = cpu.error;
        ++failures;
        std::cerr << "[native_lod] tile=" << roots[id].tile << " depth=" << pendingDepth << " fallback: " << lastError
                  << '\n';
      } else {
        if (chunked)
          tierEdges[{id, cpu.depth}] = cpu.edge;
        // Candidate plus the old ready root must fit concurrently. If necessary,
        // release lower-ranked roots to their complete base before allocating it.
        for (auto it = ranking.rbegin(); !chunked && payload + cpu.payload > budget && it != ranking.rend(); ++it) {
          if (*it != id && resident.contains(*it)) {
            payload -= resident.at(*it).payload;
            resident.erase(*it);
            desired.erase(*it);
            failed[*it] = -1;
            ++evictions;
          }
        }
        const uint64_t oldPayload = resident.contains(id) ? resident.at(id).payload : 0;
        const uint64_t steadyBudget = chunked ? budget - std::min<uint64_t>(budget / 8, 64ull * 1048576) : budget;
        if (payload + cpu.payload > budget ||
            (chunked && (cpu.payload > rootBudget(id) || payload - oldPayload + cpu.payload > steadyBudget))) {
          failed[id] = pendingDepth;
          lastError = "Adaptive quality budget reached; previous complete root retained";
          ++budgetFallbacks;
          ++failures;
        } else if (chunked) {
          // Reserve the entire candidate before uploading its first part.
          // Higher-priority inspected roots cannot be evicted by a neighbor.
          staged = std::move(cpu);
          stagingId = id;
          stagingNext = 0;
          stagingResident = {};
        } else {
          Resident candidate;
          candidate.vertices = makeHostBuffer(device, cpu.vertices, BufferUsageFlagBits::Vertex);
          candidate.indices = makeHostBuffer(device, cpu.indices, BufferUsageFlagBits::Index);
          const bool image = createUploadedRgbaTexture(
              device, cpu.image.width, cpu.image.height, cpu.image.rgba, candidate.texture, candidate.view, true);
          if (candidate.vertices && candidate.indices && image) {
            DescriptorSetDesc descriptors;
            for (uint32_t binding = 0; binding < 13; ++binding) {
              DescriptorBinding d;
              d.binding = binding;
              d.type = DescriptorType::CombinedImageSampler;
              d.textureView = candidate.view;
              d.sampler = material.sampler;
              descriptors.bindings.push_back(d);
            }
            candidate.descriptor = device->createDescriptorSet(material.layout, descriptors);
          }
          if (!candidate.descriptor) {
            failed[id] = pendingDepth;
            ++failures;
          } else {
            peakPayload = std::max(peakPayload, payload + cpu.payload);
            if (resident.contains(id))
              payload -= resident.at(id).payload;
            candidate.payload = cpu.payload;
            candidate.depth = cpu.depth;
            candidate.frontier = cpu.frontier;
            candidate.edge = cpu.edge;
            candidate.indexCount = static_cast<uint32_t>(cpu.indices.size());
            candidate.picking = std::move(cpu.picking);
            payload += candidate.payload;
            resident.insert_or_assign(id, std::move(candidate));
            ++uploads;
            maxUpload = std::max(maxUpload, cpu.payload);
          }
        }
      }
    }
    if (staged) {
      if (!desired.contains(stagingId) || desired.at(stagingId) != staged->depth) {
        staged.reset();
        stagingResident = {};
        stagingId = -1;
        ++stale;
        ++stagingCancellations;
      } else {
        auto& cpu = staged->parts.at(stagingNext);
        Resident part;
        part.vertices = makeHostBuffer(device, cpu.vertices, BufferUsageFlagBits::Vertex);
        part.indices = makeHostBuffer(device, cpu.indices, BufferUsageFlagBits::Index);
        const bool image = createUploadedRgbaTexture(
            device, cpu.image.width, cpu.image.height, cpu.image.rgba, part.texture, part.view, true);
        if (part.vertices && part.indices && image) {
          DescriptorSetDesc descriptors;
          for (uint32_t binding = 0; binding < 13; ++binding) {
            DescriptorBinding d;
            d.binding = binding;
            d.type = DescriptorType::CombinedImageSampler;
            d.textureView = part.view;
            d.sampler = material.sampler;
            descriptors.bindings.push_back(d);
          }
          part.descriptor = device->createDescriptorSet(material.layout, descriptors);
        }
        if (!part.descriptor) {
          failed[stagingId] = staged->depth;
          lastError = "Native part GPU allocation failed; previous complete frontier retained";
          ++failures;
          staged.reset();
          stagingResident = {};
          stagingId = -1;
        } else {
          part.payload = cpu.payload;
          part.indexCount = static_cast<uint32_t>(cpu.indices.size());
          stagingResident.payload += part.payload;
          stagingResident.indexCount += part.indexCount;
          stagingResident.parts.push_back(std::move(part));
          maxUpload = std::max(maxUpload, cpu.payload);
          peakPayload = std::max(peakPayload, payload + stagingResident.payload);
          ++partUploads;
          // Only one part per render-thread service. Parents keep drawing until
          // every sibling part is uploaded, even across many frames.
          cpu = {};
          ++stagingNext;
          if (stagingNext == staged->parts.size()) {
            const int id = stagingId;
            if (resident.contains(id)) {
              if (staged->depth < resident.at(id).depth)
                ++coarsenings;
              payload -= resident.at(id).payload;
            }
            stagingResident.depth = staged->depth;
            stagingResident.edge = staged->edge;
            stagingResident.frontier = staged->frontier;
            // Surface picking changes in the same transaction as all GPU parts.
            stagingResident.picking = std::move(staged->picking);
            if (stagingResident.payload != staged->payload)
              throw std::runtime_error("Incomplete native part transaction");
            payload += stagingResident.payload;
            maxParts = std::max<uint64_t>(maxParts, stagingResident.parts.size());
            resident.insert_or_assign(id, std::move(stagingResident));
            stagingResident = {};
            staged.reset();
            stagingId = -1;
            ++uploads;
          }
        }
      }
    }
    if (!pending.valid() && !staged) {
      auto queue = ranking;
      if (chunked)
        std::stable_sort(queue.begin(), queue.end(), [&](int a, int b) {
          const auto phase = [&](int id) {
            if (!desired.contains(id))
              return 3;
            if (resident.contains(id) && desired.at(id) < resident.at(id).depth)
              return 0;
            return resident.contains(id) ? 2 : 1;
          };
          if (phase(a) != phase(b))
            return phase(a) < phase(b);
          // Every wanted block gets an initial complete frontier before deep
          // upgrades. Then fix the largest remaining on-screen tessellation.
          if (phase(a) == 2 && a != focusId && b != focusId)
            return projectedEdges[a] > projectedEdges[b];
          return a == focusId && b != focusId;
        });
      for (int id : queue) {
        if (!desired.contains(id) || (failed.contains(id) && (!chunked || failed.at(id) == desired.at(id))))
          continue;
        const int depth = desired.at(id);
        if (!resident.contains(id) || (chunked ? resident.at(id).depth != depth : resident.at(id).depth < depth)) {
          pendingId = id;
          pendingDepth = depth;
          const int tile = roots[id].tile;
          const bool chunks = chunked;
          const bool picking = pickingEnabled;
          pending = std::async(std::launch::async, [tile, depth, chunks, picking] {
            return decodeZhumadianNativeRoot(tile, depth, chunks, picking);
          });
          ++requests;
          break;
        }
      }
    }
    if (payload + stagingResident.payload > budget || peakPayload > budget || resident.size() > residentLimit() ||
        maxUpload > kUploadLimit)
      throw std::runtime_error("Native pager exceeded payload limits");
  }
  bool settled() const {
    if (pending.valid() || staged)
      return false;
    for (const auto& [id, depth] : desired)
      if (!(failed.contains(id) && (!chunked || failed.at(id) == depth)) &&
          (!resident.contains(id) || (chunked ? resident.at(id).depth != depth : resident.at(id).depth < depth)))
        return false;
    return true;
  }
  void settle(RHIDevice* device,
              const ShowcaseMaterialTextureBinding& material,
              glm::vec3 camera,
              glm::vec3 target,
              float fov,
              float aspect,
              int height) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(chunked ? 600 : 180);
    bool stable = false;
    uint64_t previousUploads = uploads;
    do {
      service(device, material, camera, target, fov, aspect, height);
      const bool done = settled();
      // A newly committed tier may itself require another refinement. Recompute
      // projected quality once before declaring an offscreen capture converged.
      if (done && stable && uploads == previousUploads)
        return;
      stable = done;
      previousUploads = uploads;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Native pager observation did not settle within capture deadline");
  }
  void draw(const RHICommandBufferPtr& cmd,
            const RHIBufferPtr& vbo,
            const RHIBufferPtr& ibo,
            uint32_t indexCount,
            const RHIPipelineLayoutPtr& layout,
            const RHIDescriptorSetPtr& baseDescriptor) {
    std::vector<hongtu::asset::MeshIndexRange> ranges;
    removed = 0;
    for (const auto& [id, ready] : resident)
      for (auto span : roots[id].spans) {
        removed += span.count;
        span.first += prefixIndices;
        ranges.push_back(span);
      }
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.first < b.first; });
    cmd->bindDescriptorSet(0, baseDescriptor, layout);
    cmd->bindVertexBuffer(0, vbo);
    cmd->bindIndexBuffer(ibo);
    drawnBase = drawnChild = 0;
    for (const auto& span : hongtu::asset::retainedMeshIndexRanges(indexCount, ranges)) {
      cmd->drawIndexed(span.count, 1, span.first);
      drawnBase += span.count;
    }
    for (const auto& [id, ready] : resident) {
      const auto drawPart = [&](const Resident& part) {
        cmd->bindDescriptorSet(0, part.descriptor, layout);
        cmd->bindVertexBuffer(0, part.vertices);
        cmd->bindIndexBuffer(part.indices);
        cmd->drawIndexed(part.indexCount);
      };
      if (ready.parts.empty())
        drawPart(ready);
      else
        for (const auto& part : ready.parts)
          drawPart(part);
      drawnChild += ready.indexCount;
    }
  }
  nlohmann::json stats() const {
    auto ready = nlohmann::json::array();
    uint64_t pickingBytes = contextPicking ? contextPicking->memoryBytes() : 0;
    uint64_t basePickingTriangles = contextPicking ? contextPicking->triangleCount() : 0;
    for (const auto& root : roots)
      if (root.picking) {
        pickingBytes += root.picking->memoryBytes();
        basePickingTriangles += root.picking->triangleCount();
      }
    if (staged && staged->picking)
      pickingBytes += staged->picking->memoryBytes();
    for (const auto& [id, r] : resident) {
      if (r.picking)
        pickingBytes += r.picking->memoryBytes();
      ready.push_back({{"tile", roots[id].tile},
                       {"depth", r.depth},
                       {"frontier", r.frontier},
                       {"triangles", r.indexCount / 3},
                       {"edge_p90_m", r.edge},
                       {"upload_parts", r.parts.empty() ? 1 : r.parts.size()},
                       {"payload_bytes", r.payload},
                       {"picking_triangles", r.picking ? r.picking->triangleCount() : 0}});
    }
    auto targets = nlohmann::json::array();
    for (const auto& [id, depth] : desired)
      targets.push_back({{"tile", roots[id].tile},
                         {"requested_depth", depth},
                         {"distance_m", distances.at(id)},
                         {"base_edge_pixels", projectedBaseEdges.at(id)},
                         {"ready_edge_pixels", projectedEdges.at(id)},
                         {"root_budget_bytes", rootBudget(id)}});
    return {{"catalog_roots", roots.size()},
            {"picking_enabled", pickingEnabled},
            {"picking_cpu_bytes", pickingBytes},
            {"base_picking_triangles", basePickingTriangles},
            {"chunked_uploads", chunked},
            {"staging_payload_bytes", stagingResident.payload},
            {"staging_parts_uploaded", stagingResident.parts.size()},
            {"staging_tile", stagingId < 0 ? 0 : roots[stagingId].tile},
            {"part_uploads", partUploads},
            {"staging_cancellations", stagingCancellations},
            {"max_root_upload_parts", maxParts},
            {"partial_root_drawn", false},
            {"catalog_source_nodes", 237453},
            {"resident", ready},
            {"payload_bytes", payload},
            {"peak_concurrent_payload_bytes", peakPayload},
            {"budget_bytes", budget},
            {"upload_limit_bytes", kUploadLimit},
            {"max_upload_bytes", maxUpload},
            {"resident_limit", residentLimit()},
            {"quality_targets", targets},
            {"focus_tile", focusId < 0 ? 0 : roots[focusId].tile},
            {"coarsenings", coarsenings},
            {"budget_fallbacks", budgetFallbacks},
            {"requests", requests},
            {"uploads", uploads},
            {"evictions", evictions},
            {"stale", stale},
            {"failures", failures},
            {"last_fallback_reason", lastError},
            {"pending", pending.valid() || staged.has_value()},
            {"settled", settled()},
            {"drawn_base_indices", drawnBase},
            {"drawn_child_indices", drawnChild},
            {"removed_parent_indices", removed},
            {"base_indices", baseIndices},
            {"prefix_indices", prefixIndices},
            {"quality_policy",
             chunked ? "measured base/ready edge p90, 6px refine / 2px coarsen; coverage-first adaptive budget; not "
                       "geometric-deviation SSE"
                     : "projected source-edge p90, 6 pixel refinement; not geometric-deviation SSE"}};
  }
};
