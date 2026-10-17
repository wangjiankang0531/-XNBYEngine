// 编辑器项目加载(cloud_render_server --open <项目目录>)。
//
// 解析 hongtu-editor 的项目文件,云端渲染"编辑器里搭的那个场景":
//   <project>/.editor.json            — default_scene、camera、assets_root
//   <project>/scenes/x.editor.json    — vegetation 层 / ground / models
//
// 只读不写;schema 对齐 hongtu-editor/src/editor_app.cpp saveEditorScene()
// 与 panels/vegetation_panel.cpp toJson()。模型用 1 实例植被原型渲染
// (yaw + 均匀缩放,云渲染 v1 够用;全欧拉角后续接 FSR 实体路径)。
#pragma once

#include <hongtu/renderer/vegetation/vegetation_layer.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace crs {

struct ProjectVegLayer {
  std::string glb;          ///< 已解析的可打开路径
  float       sway = 0.35f;
  bool        casts_shadow = true;
  std::vector<hongtu::renderer::VegetationInstance> instances;
};

struct ProjectModel {
  std::uint64_t id = 0;   ///< 编辑器稳定 id(动态接口按此寻址;0=旧存档无 id)
  std::string glb;
  hongtu::renderer::VegetationInstance instance;  ///< pos/yaw/scale(v1 映射,弃用)
  // 完整变换(PBR 实体路径用)。编辑器保存的 rotation_deg 已含导入时的
  // Rx+90° 立正(Y-up→Z-up),按 T·Rz·Ry·Rx·S 复合即与编辑器逐字节一致。
  glm::vec3 position{0.0f};
  glm::vec3 rotation_deg{0.0f};  // 欧拉角(度),应用序 z→y→x
  glm::vec3 scale{1.0f};
};

struct ProjectFire {
  std::uint64_t id = 0;          ///< 编辑器稳定 id(动态接口按此寻址)
  std::string   type = "pool_fire";  ///< 已映射到 disasterEmitter 类型
  glm::vec3     position{0.0f};
  float         scale   = 1.0f;
  bool          enabled = true;
};

struct ProjectPath {
  std::vector<glm::vec2> points;
  float                  half_width = 2.5f;
  int                    smooth     = 0;
  std::string            texture    = "grass_path_2";
};

struct ProjectScene {
  bool  ground_enabled = false;
  float ground_half    = 300.0f;
  std::vector<ProjectPath> paths;
  std::vector<ProjectVegLayer> vegetation;
  std::vector<ProjectModel>    models;
  std::vector<ProjectFire>     fires;
  // 初始相机(.editor.json 的 camera 键)
  glm::vec3 cam_target{0.0f, 0.0f, 2.0f};
  float     cam_distance = 90.0f;
  float     cam_yaw   = -0.785f;  // -45°(编辑器 OrbitCameraController 默认)
  float     cam_pitch = -0.436f;  // -25°
  std::string name;
};

inline nlohmann::json loadJsonFile(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f) return {};
  try {
    return nlohmann::json::parse(f);
  } catch (const std::exception& e) {
    spdlog::error("[project] bad json {}: {}", p.string(), e.what());
    return {};
  }
}

/// glb 引用解析:层里的 glb 可能是仓库相对路径(assets/park/...)、项目
/// assets 相对路径或绝对路径 —— 按序尝试。
inline std::string resolveGlb(const std::string& ref,
                              const std::filesystem::path& project_dir,
                              const std::string& assets_root) {
  namespace fs = std::filesystem;
  if (ref.empty()) return {};
  const fs::path candidates[] = {
      fs::path(ref),                          // 绝对 / 相对 cwd(仓库根)
      project_dir / assets_root / ref,        // 项目资产
      project_dir / ref,
  };
  for (const auto& c : candidates) {
    if (fs::exists(c)) return c.string();
  }
  return {};
}

/// 读取项目;失败返回 false 并打日志(调用方决定退出)。
inline bool loadProjectScene(const std::string& project_dir_str, ProjectScene& out) {
  namespace fs = std::filesystem;
  const fs::path project_dir(project_dir_str);
  const auto pj = loadJsonFile(project_dir / ".editor.json");
  if (pj.is_null() || pj.empty()) {
    spdlog::error("[project] {}/.editor.json missing or invalid", project_dir_str);
    return false;
  }
  out.name = pj.value("name", project_dir.filename().string());
  const std::string assets_root = pj.value("assets_root", "assets");

  if (pj.contains("camera")) {
    const auto& c = pj["camera"];
    out.cam_distance = c.value("distance", 90.0f);
    out.cam_yaw      = c.value("yaw", out.cam_yaw);
    out.cam_pitch    = c.value("pitch", out.cam_pitch);
    if (c.contains("target") && c["target"].is_array() && c["target"].size() == 3) {
      out.cam_target = {c["target"][0].get<float>(), c["target"][1].get<float>(),
                        c["target"][2].get<float>()};
    }
  }

  // default_scene 是 .scene.json;编辑器扩展数据在同名 .editor.json。
  std::string scene_rel = pj.value("default_scene", "scenes/main.scene.json");
  const std::string suffix = ".scene.json";
  if (scene_rel.size() >= suffix.size() &&
      scene_rel.compare(scene_rel.size() - suffix.size(), suffix.size(), suffix) == 0) {
    scene_rel.erase(scene_rel.size() - suffix.size());
    scene_rel += ".editor.json";
  }
  const auto sj = loadJsonFile(project_dir / scene_rel);
  if (sj.is_null() || sj.empty()) {
    spdlog::error("[project] scene file {} missing or invalid", scene_rel);
    return false;
  }

  if (sj.contains("ground")) {
    out.ground_enabled = sj["ground"].value("enabled", false);
    out.ground_half    = sj["ground"].value("half", 300.0f);
  }

  for (const auto& e : sj.value("vegetation", nlohmann::json::array())) {
    ProjectVegLayer layer;
    const std::string ref = e.value("glb", "");
    layer.glb = resolveGlb(ref, project_dir, assets_root);
    if (layer.glb.empty()) {
      spdlog::warn("[project] vegetation glb not found: {} — layer skipped", ref);
      continue;
    }
    layer.sway         = e.value("sway", 0.35f);
    layer.casts_shadow = e.value("shadow", true);
    for (const auto& it : e.value("instances", nlohmann::json::array())) {
      if (!it.is_array() || it.size() < 6) continue;
      hongtu::renderer::VegetationInstance v;
      v.pos   = {it[0].get<float>(), it[1].get<float>(), it[2].get<float>()};
      v.yaw   = it[3].get<float>();
      v.scale = it[4].get<float>();
      v.tint  = it[5].get<float>();
      layer.instances.push_back(v);
    }
    if (!layer.instances.empty()) out.vegetation.push_back(std::move(layer));
  }

  for (const auto& e : sj.value("paths", nlohmann::json::array())) {
    ProjectPath d;
    d.half_width = e.value("half_width", 2.5f);
    d.smooth     = e.value("smooth", 0);
    d.texture    = e.value("texture", "grass_path_2");
    for (const auto& q : e.value("points", nlohmann::json::array())) {
      if (q.is_array() && q.size() >= 2) {
        d.points.emplace_back(q[0].get<float>(), q[1].get<float>());
      }
    }
    if (d.points.size() >= 2) out.paths.push_back(std::move(d));
  }

  for (const auto& e : sj.value("models", nlohmann::json::array())) {
    if (!e.value("visible", true)) continue;
    const std::string ref = e.value("source_asset_id", "");
    const std::string glb = resolveGlb(ref, project_dir, assets_root);
    if (glb.empty()) {
      spdlog::warn("[project] model glb not found: {} — skipped", ref);
      continue;
    }
    ProjectModel m;
    m.id  = e.value("id", 0ull);
    m.glb = glb;
    hongtu::renderer::VegetationInstance v;
    if (e.contains("position") && e["position"].size() == 3) {
      m.position = {e["position"][0].get<float>(), e["position"][1].get<float>(),
                    e["position"][2].get<float>()};
      v.pos = m.position;
    }
    if (e.contains("rotation_deg") && e["rotation_deg"].size() == 3) {
      m.rotation_deg = {e["rotation_deg"][0].get<float>(),
                        e["rotation_deg"][1].get<float>(),
                        e["rotation_deg"][2].get<float>()};
      v.yaw = glm::radians(m.rotation_deg.z);  // 植被兜底:仅 yaw
    }
    if (e.contains("scale") && e["scale"].size() == 3) {
      m.scale = {e["scale"][0].get<float>(), e["scale"][1].get<float>(),
                 e["scale"][2].get<float>()};
      v.scale = m.scale.x;  // 植被兜底:均匀缩放取 x
    }
    m.instance = v;
    out.models.push_back(std::move(m));
  }

  // 火苗:编辑器 "fires"。kind→disasterEmitter 类型映射;带稳定 id 供动态接口寻址。
  auto mapFireType = [](const std::string& kind) -> std::string {
    if (kind == "smoke")  return "smoke";
    if (kind == "sparks") return "sparks";
    if (kind == "jet")    return "jet_fire";
    return "pool_fire";  // "fire" 及未知
  };
  for (const auto& e : sj.value("fires", nlohmann::json::array())) {
    ProjectFire f;
    f.id      = e.value("id", 0ull);
    f.type    = mapFireType(e.value("kind", std::string("fire")));
    f.enabled = e.value("enabled", true);
    f.scale   = e.value("rate_scale", 1.0f);
    if (e.contains("position") && e["position"].is_array() && e["position"].size() >= 3)
      f.position = {e["position"][0].get<float>(), e["position"][1].get<float>(),
                    e["position"][2].get<float>()};
    out.fires.push_back(std::move(f));
  }

  spdlog::info("[project] '{}' loaded: ground={} (half={}), {} veg layers, {} models, {} fires, {} paths",
               out.name, out.ground_enabled, out.ground_half,
               out.vegetation.size(), out.models.size(), out.fires.size(), out.paths.size());
  return true;
}

}  // namespace crs
