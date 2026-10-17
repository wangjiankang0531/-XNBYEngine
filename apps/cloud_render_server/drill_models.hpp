// drill_models.hpp — 演练要素的程序化占位模型(无外部 glb 依赖)。
//
// 由 DynamicScene 在收到 `drill://person/<subtype>` / `drill://vehicle/<subtype>`
// 等 url 时调用 buildDrillModel() 直接造 CPU 侧 ModelData,交给 VegetationLayer
// 实例化渲染(走 PBR,有阴影/光照)。颜色按岗位/车种子类型区分,前端面板对齐。
//
// 几何移植自已删的 apps/drill_render_server/drill_assets.hpp(M2 方案);后续要换
// 精模 glb 时,前端把 url 改成真实路径即可,本文件可整体退役。
#pragma once

#include <hongtu/asset/GltfLoader.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drs {

using hongtu::asset::MaterialData;
using hongtu::asset::MeshData;
using hongtu::asset::ModelData;
using hongtu::asset::TextureData;

// ---- 基础几何拼装(Z-up 直觉书写;makeModel 末尾换回 Y-up)------------------

inline void appendBox(MeshData& m, glm::vec3 c, glm::vec3 h) {
  const glm::vec3 n[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  for (int f = 0; f < 6; ++f) {
    const glm::vec3 nor = n[f];
    glm::vec3 u = (std::abs(nor.z) > 0.5f) ? glm::vec3(1,0,0) : glm::cross(glm::vec3(0,0,1), nor);
    glm::vec3 v = glm::cross(nor, u);
    const uint32_t base = static_cast<uint32_t>(m.positions.size());
    for (int i = 0; i < 4; ++i) {
      const float su = (i == 1 || i == 2) ? 1.0f : -1.0f;
      const float sv = (i >= 2) ? 1.0f : -1.0f;
      m.positions.push_back(c + nor * glm::abs(h * nor) +
                            u * (su * glm::abs(glm::dot(h, u))) +
                            v * (sv * glm::abs(glm::dot(h, v))));
      m.normals.push_back(nor);
      m.uvs.push_back({(su + 1) * 0.5f, (sv + 1) * 0.5f});
    }
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
}

inline void appendCylinder(MeshData& m, glm::vec3 base_c, float r, float h, int segs = 16,
                           bool cap_bottom = true, bool cap_top = true) {
  const uint32_t base = static_cast<uint32_t>(m.positions.size());
  for (int i = 0; i <= segs; ++i) {
    const float a = glm::two_pi<float>() * i / segs;
    const glm::vec3 nor{std::cos(a), std::sin(a), 0.0f};
    for (int z = 0; z <= 1; ++z) {
      m.positions.push_back(base_c + nor * r + glm::vec3(0, 0, h * z));
      m.normals.push_back(nor);
      m.uvs.push_back({static_cast<float>(i) / segs, static_cast<float>(z)});
    }
  }
  for (int i = 0; i < segs; ++i) {
    const uint32_t a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
    m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
  }
  auto cap = [&](float z, float nz) {
    const uint32_t cc = static_cast<uint32_t>(m.positions.size());
    m.positions.push_back(base_c + glm::vec3(0, 0, z));
    m.normals.push_back({0, 0, nz});
    m.uvs.push_back({0.5f, 0.5f});
    for (int i = 0; i <= segs; ++i) {
      const float a = glm::two_pi<float>() * i / segs;
      m.positions.push_back(base_c + glm::vec3(std::cos(a) * r, std::sin(a) * r, z));
      m.normals.push_back({0, 0, nz});
      m.uvs.push_back({0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
    }
    for (int i = 0; i < segs; ++i) {
      if (nz > 0) m.indices.insert(m.indices.end(), {cc, cc + 1u + i, cc + 2u + i});
      else        m.indices.insert(m.indices.end(), {cc, cc + 2u + i, cc + 1u + i});
    }
  };
  if (cap_bottom) cap(0.0f, -1.0f);
  if (cap_top)    cap(h, 1.0f);
}

inline void appendCone(MeshData& m, glm::vec3 base_c, float r, float h, int segs = 16) {
  const uint32_t base = static_cast<uint32_t>(m.positions.size());
  const float slope = r / std::sqrt(r * r + h * h);
  for (int i = 0; i <= segs; ++i) {
    const float a = glm::two_pi<float>() * i / segs;
    const glm::vec3 dir{std::cos(a), std::sin(a), 0.0f};
    const glm::vec3 nor = glm::normalize(glm::vec3(dir.x, dir.y, slope));
    m.positions.push_back(base_c + dir * r);
    m.normals.push_back(nor);
    m.uvs.push_back({static_cast<float>(i) / segs, 0.0f});
    m.positions.push_back(base_c + glm::vec3(0, 0, h));
    m.normals.push_back(nor);
    m.uvs.push_back({static_cast<float>(i) / segs, 1.0f});
  }
  for (int i = 0; i < segs; ++i) {
    const uint32_t a = base + i * 2, t = a + 1, b = a + 2;
    m.indices.insert(m.indices.end(), {a, b, t});
  }
  const uint32_t cc = static_cast<uint32_t>(m.positions.size());
  m.positions.push_back(base_c);
  m.normals.push_back({0, 0, -1});
  m.uvs.push_back({0.5f, 0.5f});
  for (int i = 0; i <= segs; ++i) {
    const float a = glm::two_pi<float>() * i / segs;
    m.positions.push_back(base_c + glm::vec3(std::cos(a) * r, std::sin(a) * r, 0));
    m.normals.push_back({0, 0, -1});
    m.uvs.push_back({0.5f, 0.5f});
  }
  for (int i = 0; i < segs; ++i) {
    m.indices.insert(m.indices.end(), {cc, cc + 2u + i, cc + 1u + i});
  }
}

inline ModelData makeModel(MeshData mesh, glm::vec4 color, float rough = 0.8f,
                           glm::vec3 emissive = glm::vec3(0.0f)) {
  // 几何按 Z-up 书写;VegetationLayer 对原型统一做 Y-up→Z-up 的 Rx+90,
  // 这里预先反向(Z-up→Y-up):(x,y,z) → (x, z, -y),抵消后立正。
  for (auto& p : mesh.positions) p = {p.x, p.z, -p.y};
  for (auto& n : mesh.normals)   n = {n.x, n.z, -n.y};
  mesh.material_id = 0;
  ModelData md;
  // 颜色不靠材质 base:VegetationLayer 着色器用 baseColorTex × **实例色**,忽略材质
  // base_color_factor。要素颜色由 DynamicScene 把 drillColor(url) 写进实例 base_color。
  MaterialData mat;
  mat.base_color_factor = color;
  mat.metallic_factor   = 0.0f;
  mat.roughness_factor  = rough;
  mat.emissive_factor   = emissive;
  // 自发光:着色器从 emissiveTex 采样(emissive_factor 不被采样),故把自发光色烘进
  // 1×1 贴图。夜间(night_factor)经 shader ×6 发亮 → dusk/夜里要素不再被压成黑。
  // 贴图为 sRGB,这里按 sRGB 字节存(>0 时才建,避免给本无自发光的几何加贴图)。
  if (glm::dot(emissive, emissive) > 1e-6f) {
    auto u8 = [](float c) {
      return static_cast<uint8_t>(glm::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    TextureData etex;
    etex.width = 1; etex.height = 1; etex.channels = 4;
    etex.pixels = {u8(emissive.r), u8(emissive.g), u8(emissive.b), 255};
    md.textures.push_back(std::move(etex));
    mat.emissive_texture = 0;  // 指向上面的 1×1 自发光色块
  }
  md.meshes.push_back(std::move(mesh));
  md.materials.push_back(std::move(mat));
  md.source_path = "procedural://drill";
  return md;
}

/// 人员:1.7m 胶囊(圆柱身 + 锥顶),颜色按岗位子类型。
/// 夜间自发光 = 身份色 × 0.55(经 shader×night_factor×6 → 暗光下仍可辨识)。
inline ModelData makePerson(glm::vec4 color) {
  MeshData m;
  appendCylinder(m, {0, 0, 0.0f}, 0.26f, 1.35f, 14, true, false);
  appendCone(m, {0, 0, 1.35f}, 0.26f, 0.38f, 14);
  return makeModel(std::move(m), color, 0.8f, glm::vec3(color) * 0.55f);
}

/// 救援车:车身 + 车头 + 顶部水炮座,车头朝 +X,长 ~5.6m,颜色按车种。
inline ModelData makeVehicle(glm::vec4 color) {
  MeshData m;
  appendBox(m, {-0.6f, 0.0f, 1.25f}, {2.2f, 1.05f, 1.25f});
  appendBox(m, {2.05f, 0.0f, 0.95f}, {0.75f, 1.0f, 0.95f});
  appendCylinder(m, {0, 0, 2.5f}, 0.18f, 0.5f, 8);
  return makeModel(std::move(m), color, 0.55f, glm::vec3(color) * 0.55f);
}

/// 报警:黄锥 + 红色自发光顶。
inline ModelData makeAlarm() {
  MeshData m;
  appendCone(m, {0, 0, 0}, 0.55f, 1.5f, 18);
  return makeModel(std::move(m), {0.95f, 0.65f, 0.08f, 1.0f}, 0.5f, {0.8f, 0.1f, 0.05f});
}

/// 标注钉:细杆 + 头部方块,橙色。
inline ModelData makeMarker() {
  MeshData m;
  appendCylinder(m, {0, 0, 0}, 0.06f, 1.6f, 8);
  appendBox(m, {0, 0, 1.85f}, {0.28f, 0.28f, 0.25f});
  return makeModel(std::move(m), {0.95f, 0.45f, 0.08f, 1.0f}, 0.6f);
}

// ---- 子类型 → 颜色(前端 catalog.js 配色对齐)------------------------------

inline glm::vec4 personColor(const std::string& st) {
  if (st == "fire_fighter") return {0.85f, 0.25f, 0.10f, 1.0f};  // 消防 橙红
  if (st == "medic")        return {0.92f, 0.92f, 0.95f, 1.0f};  // 救护 白
  if (st == "worker")       return {0.18f, 0.42f, 0.85f, 1.0f};  // 工作 蓝
  if (st == "rescuer")      return {0.95f, 0.55f, 0.10f, 1.0f};  // 抢险 橙
  if (st == "civilian")     return {0.45f, 0.52f, 0.48f, 1.0f};  // 群众 灰绿
  if (st == "gas_detector") return {0.92f, 0.85f, 0.15f, 1.0f};  // 气体侦测 黄
  if (st == "env_monitor")  return {0.20f, 0.70f, 0.35f, 1.0f};  // 环境监测 绿
  return {0.18f, 0.42f, 0.85f, 1.0f};
}

inline glm::vec4 vehicleColor(const std::string& st) {
  if (st == "fire_truck")   return {0.78f, 0.12f, 0.10f, 1.0f};  // 消防车 红
  if (st == "ambulance")    return {0.92f, 0.92f, 0.95f, 1.0f};  // 救护车 白
  if (st == "command")      return {0.16f, 0.35f, 0.70f, 1.0f};  // 指挥车 蓝
  if (st == "water_truck")  return {0.15f, 0.60f, 0.70f, 1.0f};  // 洒水车 青
  if (st == "hazmat_truck") return {0.90f, 0.70f, 0.12f, 1.0f};  // 危化品 黄
  return {0.78f, 0.12f, 0.10f, 1.0f};
}

/// drill:// url → 该要素的实例颜色(写进 VegetationInstance.base_color);
/// 非 drill:// 返回白(不改色,植被/glb 走原有 tint 逻辑)。
inline glm::vec4 drillColor(const std::string& url) {
  const std::string prefix = "drill://";
  if (url.rfind(prefix, 0) != 0) return glm::vec4(1.0f);
  std::string rest = url.substr(prefix.size());
  const auto slash = rest.find('/');
  const std::string kind = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  const std::string sub  = (slash == std::string::npos) ? "" : rest.substr(slash + 1);
  if (kind == "person")  return personColor(sub);
  if (kind == "vehicle") return vehicleColor(sub);
  if (kind == "alarm")   return {0.95f, 0.65f, 0.08f, 1.0f};
  if (kind == "marker")  return {0.95f, 0.45f, 0.08f, 1.0f};
  return glm::vec4(1.0f);
}

/// 解析 `drill://<kind>/<subtype>` → 程序化 ModelData。非 drill:// 返回 nullopt。
inline std::optional<ModelData> buildDrillModel(const std::string& url) {
  const std::string prefix = "drill://";
  if (url.rfind(prefix, 0) != 0) return std::nullopt;
  std::string rest = url.substr(prefix.size());  // "person/fire_fighter"
  const auto slash = rest.find('/');
  const std::string kind = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  const std::string sub  = (slash == std::string::npos) ? "" : rest.substr(slash + 1);
  if (kind == "person")  return makePerson(personColor(sub));
  if (kind == "vehicle") return makeVehicle(vehicleColor(sub));
  if (kind == "alarm")   return makeAlarm();
  if (kind == "marker")  return makeMarker();
  return std::nullopt;
}

}  // namespace drs
