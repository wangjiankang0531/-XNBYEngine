#include <hongtu/scene/scene_loader.hpp>

#include <hongtu/scene/emitter_entity.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace hongtu::scene {

namespace {

/// Reconstruct EmitterDesc from preset name + params block. `src` is the
/// emitter-local spawn center (always (0,0,0) in P0 since the entity owns
/// world position separately).
hongtu::renderer::EmitterDesc descForPreset(const std::string& preset,
                                             const glm::vec3& src,
                                             const nlohmann::json& params) {
  namespace P = hongtu::renderer::EmitterPresets;
  float rate = params.value("emit_rate", 80.0f);
  if (preset == "fire")   return P::makeFire(src, rate);
  if (preset == "smoke") {
    glm::vec3 wind {0.5f, 0.0f, 0.0f};
    if (params.contains("wind") && params["wind"].is_array()
        && params["wind"].size() >= 3) {
      wind = {params["wind"][0].get<float>(),
              params["wind"][1].get<float>(),
              params["wind"][2].get<float>()};
    }
    return P::makeSmoke(src, wind, rate);
  }
  if (preset == "sparks") return P::makeSparks(src, rate);
  if (preset == "jet") {
    glm::vec3 dir {1.0f, 0.0f, 0.0f};
    if (params.contains("direction") && params["direction"].is_array()
        && params["direction"].size() >= 3) {
      dir = {params["direction"][0].get<float>(),
             params["direction"][1].get<float>(),
             params["direction"][2].get<float>()};
    }
    return P::makeJet(src, dir, rate);
  }
  return P::makeFire(src, rate);
}

/// We can't recover "which preset originated this EmitterDesc" by inspecting
/// the desc alone — the presets bake their color ramps into common fields.
/// To keep load+save lossless without polluting EmitterEntity itself, we
/// stash preset name in a process-wide map keyed by entity pointer.
///
/// Limitation: outlives SceneLoader instances. For multi-scene apps this is
/// fine (pointer identity is unique). Future work: move into SceneGraph
/// as an opaque per-entity payload.
struct EmitterMeta {
  std::string preset;
};
std::unordered_map<const LocalEmitter*, EmitterMeta>& emitterMetaTable() {
  static std::unordered_map<const LocalEmitter*, EmitterMeta> t;
  return t;
}

}  // namespace

std::optional<std::unique_ptr<SceneGraph>>
SceneLoader::load(const std::string& json_path) {
  last_error_.clear();
  std::ifstream f(json_path);
  if (!f.is_open()) {
    last_error_ = "cannot open " + json_path;
    return std::nullopt;
  }
  std::stringstream ss; ss << f.rdbuf();

  auto j = nlohmann::json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) { last_error_ = "invalid JSON"; return std::nullopt; }

  if (!j.contains("version")) { last_error_ = "missing 'version'"; return std::nullopt; }
  if (j["version"].get<int>() != 1) {
    last_error_ = "unsupported version (only 1)"; return std::nullopt;
  }
  std::string mode = j.value("mode", "local");
  if (mode != "geo" && mode != "local") {
    last_error_ = "unknown mode '" + mode + "'"; return std::nullopt;
  }

  auto sg = std::make_unique<SceneGraph>();

  // Annotations — delegate.
  if (j.contains("annotations") && j["annotations"].is_array()) {
    nlohmann::json wrapped;
    wrapped["version"]     = 1;
    wrapped["annotations"] = j["annotations"];
    if (!sg->annotations().fromJson(wrapped)) {
      spdlog::warn("[SceneLoader] annotation parse had non-fatal errors");
    }
  }

  // Emitters — P0:only `local` positions supported (geo mode 简化 per spec §8).
  if (j.contains("emitters") && j["emitters"].is_array()) {
    for (const auto& e : j["emitters"]) {
      if (!e.contains("id") || !e.contains("preset")) continue;
      const std::string id     = e["id"].get<std::string>();
      const std::string preset = e["preset"].get<std::string>();
      const bool enabled       = e.value("enabled", false);
      const auto  params       = e.value("params", nlohmann::json::object());

      glm::dvec3 pos {0.0, 0.0, 0.0};
      if (e.contains("local") && e["local"].is_array() && e["local"].size() >= 3) {
        pos = {e["local"][0].get<double>(),
               e["local"][1].get<double>(),
               e["local"][2].get<double>()};
      }
      auto desc = descForPreset(preset, glm::vec3(0.0f), params);
      desc.enabled = enabled;
      auto* em = sg->create<LocalEmitter>(id);
      if (em) {
        em->setPosition(pos);
        em->setDesc(desc);
        emitterMetaTable()[em] = {preset};
      }
    }
  }

  // Timeline.
  if (j.contains("timeline") && j["timeline"].is_object()) {
    const auto& tl = j["timeline"];
    if (tl.contains("duration_seconds")) {
      sg->timeline().setDuration(tl["duration_seconds"].get<float>());
    }
    if (tl.contains("tracks") && tl["tracks"].is_object()) {
      for (auto it = tl["tracks"].begin(); it != tl["tracks"].end(); ++it) {
        const std::string& key = it.key();
        auto dot = key.find('.');
        if (dot == std::string::npos) {
          spdlog::warn("[SceneLoader] timeline track '{}' missing '.<field>'", key);
          continue;
        }
        const std::string ent_name = key.substr(0, dot);
        const std::string field    = key.substr(dot + 1);
        if (field != "enabled") {
          spdlog::warn("[SceneLoader] track '{}' field '{}' not supported in P0",
                       key, field);
          continue;
        }
        auto* em = sg->getAsByName<LocalEmitter>(ent_name);
        if (!em) {
          spdlog::warn("[SceneLoader] track '{}' target emitter not found", key);
          continue;
        }
        Track<bool> t;
        const auto& trk = it.value();
        if (trk.contains("keys") && trk["keys"].is_array()) {
          for (const auto& k : trk["keys"]) {
            t.addKey(k["t"].get<float>(), k["v"].get<bool>());
          }
        }
        sg->timeline().addDriver(std::make_unique<CallbackDriver<bool>>(
            std::move(t),
            [em](const bool& on) {
              auto d = em->getDesc();
              d.enabled = on;
              em->setDesc(d);
            }));
      }
    }
  }

  return sg;
}

bool SceneLoader::save(const SceneGraph& sg, const std::string& json_path) const {
  last_error_.clear();
  nlohmann::json j;
  j["version"] = 1;
  j["mode"]    = "local";    // P0:always emits local mode

  auto ann_json = sg.annotations().toJson();
  j["annotations"] = ann_json.contains("annotations")
      ? ann_json["annotations"] : nlohmann::json::array();

  auto& emit_arr = j["emitters"] = nlohmann::json::array();
  auto& mut_sg = const_cast<SceneGraph&>(sg);
  auto& meta_table = emitterMetaTable();
  for (const auto& info : mut_sg.allWithMeta<LocalEmitter>()) {
    nlohmann::json e;
    e["id"]     = info.name.empty()
                    ? std::string{"emitter_"} + std::to_string(info.id)
                    : info.name;
    auto meta_it = meta_table.find(info.ptr);
    e["preset"] = (meta_it == meta_table.end()) ? std::string{"fire"}
                                                  : meta_it->second.preset;
    auto pos = info.ptr->getPosition();
    e["local"]   = {pos.x, pos.y, pos.z};
    e["params"]  = {{"emit_rate", info.ptr->getDesc().emit_rate}};
    e["enabled"] = info.ptr->getDesc().enabled;
    emit_arr.push_back(std::move(e));
  }

  j["timeline"] = {
      {"duration_seconds", sg.timeline().getDuration()},
      {"tracks", nlohmann::json::object()},
  };
  // P0:Timeline tracks aren't round-tripped — CallbackDriver closures own
  // their state opaquely. Future: store track key data alongside each
  // driver so save can reconstruct.

  std::ofstream f(json_path);
  if (!f.is_open()) { last_error_ = "cannot open " + json_path; return false; }
  f << j.dump(2);
  return true;
}

}  // namespace hongtu::scene
