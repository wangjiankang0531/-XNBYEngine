// 演练要素模型 — 引擎层纯数据。
//
// DrillElement 只承载"演练语义状态"(位置、朝向、kind 专有 props、沿路线移动
// 状态);渲染绑定(粒子/骨骼角色/折线等)由 app 层(drill_render_server)按
// kind 映射到既有渲染实体完成,引擎演练层零渲染依赖。
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace hongtu::scene::drill {

/// 八类演练要素中的"标绘类"七种;应急处置操作(开阀/喷淋)建模为对
/// vehicle/disaster 等要素的 props 更新,不单设 kind。
enum class ElementKind {
  Person,    ///< 人员(姿态/路线跟随)
  Vehicle,   ///< 救援车辆(路线跟随/喷淋开关)
  Route,     ///< 路线标绘(静态警戒线/动态集结路线),几何在 props.points
  Region,    ///< 区域标绘(多边形/圆),几何在 props
  Disaster,  ///< 灾害(火/喷射火/毒气/爆炸/烟雾),类型与参数在 props
  Alarm,     ///< 报警点(全厂/中控),props.on 开关
  Marker,    ///< 通用标注(符号/文字/箭头)
};

const char* toString(ElementKind k);
bool kindFromString(const std::string& s, ElementKind& out);

struct DrillElement {
  uint64_t    id   = 0;
  ElementKind kind = ElementKind::Marker;
  std::string name;
  glm::dvec3  pos{0.0};      ///< 底座场景局部坐标,米
  double      yaw_rad = 0.0;
  nlohmann::json props = nlohmann::json::object();  ///< kind 专有参数

  // 沿路线移动状态(仅 Person / Vehicle)
  bool                    moving = false;
  std::vector<glm::dvec3> route;             ///< 激活中的路线(局部坐标)
  double                  route_progress_m = 0.0;
  double                  speed_mps        = 0.0;

  nlohmann::json toJson() const;
};

}  // namespace hongtu::scene::drill
