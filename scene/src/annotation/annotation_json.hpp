#pragma once

// Internal JSON helpers shared by the 10 annotation .cpp files.
// Not part of the public include surface — lives under scene/src/.

#include <glm/glm.hpp>
#include <hongtu/scene/annotation/annotation.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>

namespace hongtu::scene::detail {

  inline const char* kindToString(AnnotationKind k) {
    switch (k) {
      case AnnotationKind::Polyline:
        return "polyline";
      case AnnotationKind::Polygon:
        return "polygon";
      case AnnotationKind::Symbol:
        return "symbol";
      case AnnotationKind::Text:
        return "text";
      case AnnotationKind::Arrow:
        return "arrow";
    }
    return "polyline";
  }

  inline const char* modeToString(AnnotationMode m) {
    return m == AnnotationMode::Geo ? "geo" : "local";
  }

  inline nlohmann::json vec4ToJson(const glm::vec4& v) {
    return {v.x, v.y, v.z, v.w};
  }

  inline glm::vec4 vec4FromJson(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4)
      return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
  }

  /// Style → JSON (only non-default fields, for compactness).
  inline nlohmann::json styleToJson(const AnnotationStyle& s) {
    nlohmann::json j = nlohmann::json::object();
    AnnotationStyle d; // defaults
    if (s.color != d.color)
      j["color"] = vec4ToJson(s.color);
    if (s.stroke_width_m != d.stroke_width_m)
      j["stroke_width_m"] = s.stroke_width_m;
    if (s.symbol_size_m != d.symbol_size_m)
      j["symbol_size_m"] = s.symbol_size_m;
    if (s.symbol_index != d.symbol_index)
      j["symbol_index"] = s.symbol_index;
    if (!s.symbol_texture_id.empty())
      j["symbol_texture_id"] = s.symbol_texture_id;
    if (s.symbol_effect != d.symbol_effect)
      j["symbol_effect"] = s.symbol_effect;
    if (s.symbol_freq != d.symbol_freq)
      j["symbol_freq"] = s.symbol_freq;
    if (s.filled != d.filled)
      j["filled"] = s.filled;
    if (s.fill_type != d.fill_type)
      j["fill_type"] = static_cast<int>(s.fill_type);
    if (s.fill_param != d.fill_param)
      j["fill_param"] = s.fill_param;
    if (s.wall != d.wall)
      j["wall"] = s.wall;
    if (s.wall_height != d.wall_height)
      j["wall_height"] = s.wall_height;
    if (s.wall_flow != d.wall_flow)
      j["wall_flow"] = s.wall_flow;
    if (s.wall_flow_vertical != d.wall_flow_vertical)
      j["wall_flow_vertical"] = s.wall_flow_vertical;
    if (s.water != d.water)
      j["water"] = s.water;
    if (s.corner_smooth != d.corner_smooth)
      j["corner_smooth"] = s.corner_smooth;
    if (s.circle != d.circle)
      j["circle"] = s.circle;
    if (s.circle_radius_m != d.circle_radius_m)
      j["circle_radius_m"] = s.circle_radius_m;
    if (s.dash_period_m != d.dash_period_m)
      j["dash_period_m"] = s.dash_period_m;
    if (s.fx_outline != d.fx_outline)
      j["fx_outline"] = s.fx_outline;
    if (s.fx_glow != d.fx_glow)
      j["fx_glow"] = s.fx_glow;
    if (s.fx_pattern != d.fx_pattern)
      j["fx_pattern"] = s.fx_pattern;
    if (s.fx_arc != d.fx_arc)
      j["fx_arc"] = s.fx_arc;
    if (s.line_type != d.line_type)
      j["line_type"] = static_cast<int>(s.line_type);
    if (s.line_placement != d.line_placement)
      j["line_placement"] = static_cast<int>(s.line_placement);
    if (s.arrow_head != d.arrow_head)
      j["arrow_head"] = s.arrow_head;
    if (s.arrow_tail != d.arrow_tail)
      j["arrow_tail"] = s.arrow_tail;
    if (s.outline_color != d.outline_color)
      j["outline_color"] = vec4ToJson(s.outline_color);
    if (s.glow_power != d.glow_power)
      j["glow_power"] = s.glow_power;
    if (s.flow_speed != d.flow_speed)
      j["flow_speed"] = s.flow_speed;
    if (s.line_tex_tiling != d.line_tex_tiling)
      j["line_tex_tiling"] = s.line_tex_tiling;
    if (s.arc_height_ratio != d.arc_height_ratio)
      j["arc_height_ratio"] = s.arc_height_ratio;
    if (!s.line_texture.empty())
      j["line_texture"] = s.line_texture;
    if (!s.line_texture_asset.empty())
      j["line_texture_asset"] = s.line_texture_asset;
    if (!s.text.empty())
      j["text"] = s.text;
    if (s.text_size_m != d.text_size_m)
      j["text_size_m"] = s.text_size_m;
    if (s.text_background != d.text_background)
      j["text_background"] = s.text_background;
    if (s.text_leader != d.text_leader)
      j["text_leader"] = s.text_leader;
    if (s.text_bg_color != d.text_bg_color)
      j["text_bg_color"] = vec4ToJson(s.text_bg_color);
    if (s.text_effect != d.text_effect)
      j["text_effect"] = s.text_effect;
    if (s.symbol_label != d.symbol_label)
      j["symbol_label"] = s.symbol_label;
    return j;
  }

  /// JSON → Style (missing fields keep defaults).
  inline AnnotationStyle styleFromJson(const nlohmann::json& j) {
    AnnotationStyle s;
    if (!j.is_object())
      return s;
    if (j.contains("color"))
      s.color = vec4FromJson(j["color"], s.color);
    if (j.contains("stroke_width_m"))
      s.stroke_width_m = j["stroke_width_m"].get<float>();
    if (j.contains("symbol_size_m"))
      s.symbol_size_m = j["symbol_size_m"].get<float>();
    if (j.contains("symbol_index"))
      s.symbol_index = j["symbol_index"].get<uint32_t>();
    if (j.contains("symbol_texture_id"))
      s.symbol_texture_id = j["symbol_texture_id"].get<std::string>();
    if (j.contains("symbol_effect"))
      s.symbol_effect = j["symbol_effect"].get<int>();
    if (j.contains("symbol_freq"))
      s.symbol_freq = j["symbol_freq"].get<float>();
    if (j.contains("filled"))
      s.filled = j["filled"].get<bool>();
    if (j.contains("fill_type"))
      s.fill_type = static_cast<FillType>(j["fill_type"].get<int>());
    if (j.contains("fill_param"))
      s.fill_param = j["fill_param"].get<float>();
    if (j.contains("wall"))
      s.wall = j["wall"].get<bool>();
    if (j.contains("wall_height"))
      s.wall_height = j["wall_height"].get<float>();
    if (j.contains("wall_flow"))
      s.wall_flow = j["wall_flow"].get<float>();
    if (j.contains("wall_flow_vertical"))
      s.wall_flow_vertical = j["wall_flow_vertical"].get<bool>();
    if (j.contains("water"))
      s.water = j["water"].get<bool>();
    if (j.contains("corner_smooth"))
      s.corner_smooth = j["corner_smooth"].get<int>();
    if (j.contains("circle"))
      s.circle = j["circle"].get<bool>();
    if (j.contains("circle_radius_m"))
      s.circle_radius_m = j["circle_radius_m"].get<float>();
    if (j.contains("dash_period_m"))
      s.dash_period_m = j["dash_period_m"].get<float>();
    bool has_fx = false;
    if (j.contains("fx_outline")) {
      s.fx_outline = j["fx_outline"].get<bool>();
      has_fx = true;
    }
    if (j.contains("fx_glow")) {
      s.fx_glow = j["fx_glow"].get<bool>();
      has_fx = true;
    }
    if (j.contains("fx_pattern")) {
      s.fx_pattern = j["fx_pattern"].get<bool>();
      has_fx = true;
    }
    if (j.contains("fx_arc")) {
      s.fx_arc = j["fx_arc"].get<bool>();
      has_fx = true;
    }
    if (j.contains("line_type"))
      s.line_type = static_cast<LineType>(j["line_type"].get<int>());
    // 迁移:旧工程没有 fx_* 只有单选 line_type → 映射到可叠加开关。
    if (!has_fx && s.line_type != LineType::Normal) {
      switch (s.line_type) {
        case LineType::Outline:
          s.fx_outline = true;
          break;
        case LineType::Glow:
          s.fx_glow = true;
          break;
        case LineType::Textured:
          s.fx_pattern = true;
          s.flow_speed = 0.0f;
          break;
        case LineType::Flow:
          s.fx_pattern = true;
          break;
        case LineType::Arc:
          s.fx_arc = true;
          break;
        default:
          break;
      }
    }
    if (j.contains("line_placement"))
      s.line_placement = static_cast<LinePlacement>(j["line_placement"].get<int>());
    if (j.contains("arrow_head"))
      s.arrow_head = j["arrow_head"].get<bool>();
    if (j.contains("arrow_tail"))
      s.arrow_tail = j["arrow_tail"].get<bool>();
    if (j.contains("outline_color"))
      s.outline_color = vec4FromJson(j["outline_color"], s.outline_color);
    if (j.contains("glow_power"))
      s.glow_power = j["glow_power"].get<float>();
    if (j.contains("flow_speed"))
      s.flow_speed = j["flow_speed"].get<float>();
    if (j.contains("line_tex_tiling"))
      s.line_tex_tiling = j["line_tex_tiling"].get<float>();
    if (j.contains("arc_height_ratio"))
      s.arc_height_ratio = j["arc_height_ratio"].get<float>();
    if (j.contains("line_texture"))
      s.line_texture = j["line_texture"].get<std::string>();
    if (j.contains("line_texture_asset"))
      s.line_texture_asset = j["line_texture_asset"].get<std::string>();
    if (j.contains("text"))
      s.text = j["text"].get<std::string>();
    if (j.contains("text_size_m"))
      s.text_size_m = j["text_size_m"].get<float>();
    if (j.contains("text_background"))
      s.text_background = j["text_background"].get<bool>();
    if (j.contains("text_leader"))
      s.text_leader = j["text_leader"].get<bool>();
    if (j.contains("text_bg_color"))
      s.text_bg_color = vec4FromJson(j["text_bg_color"], s.text_bg_color);
    if (j.contains("text_effect"))
      s.text_effect = j["text_effect"].get<int>();
    if (j.contains("symbol_label"))
      s.symbol_label = j["symbol_label"].get<bool>();
    return s;
  }

  /// Common base fields(id / visible / style / layer_id) — written as
  /// top-level keys.
  inline void writeBaseFields(nlohmann::json& j, const Annotation& a) {
    j["id"] = a.id();
    j["visible"] = a.visible();
    j["layer_id"] = a.layerId();
    auto s = styleToJson(a.style());
    if (!s.empty())
      j["style"] = s;
  }

  inline void readBaseFields(const nlohmann::json& j, Annotation& a) {
    if (j.contains("id"))
      a.setId(j["id"].get<uint64_t>());
    if (j.contains("visible"))
      a.setVisible(j["visible"].get<bool>());
    if (j.contains("layer_id"))
      a.setLayerId(j["layer_id"].get<uint32_t>());
    if (j.contains("style"))
      a.setStyle(styleFromJson(j["style"]));
  }

  // ---------------------------------------------------------------------------
  // Vertex array helpers — kind-agnostic, dispatched by AnnotationMode
  // ---------------------------------------------------------------------------

  inline nlohmann::json geodeticToJson(const coordinate::Geodetic& g) {
    constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
    return {g.lat * kRadToDeg, g.lon * kRadToDeg, g.alt};
  }

  inline coordinate::Geodetic geodeticFromJson(const nlohmann::json& j) {
    constexpr double kDegToRad = std::numbers::pi_v<double> / 180.0;
    return {
        j[0].get<double>() * kDegToRad,
        j[1].get<double>() * kDegToRad,
        j.size() >= 3 ? j[2].get<double>() : 0.0,
    };
  }

  inline nlohmann::json dvec3ToJson(const glm::dvec3& p) {
    return {p.x, p.y, p.z};
  }

  inline glm::dvec3 dvec3FromJson(const nlohmann::json& j) {
    return {j[0].get<double>(), j[1].get<double>(), j.size() >= 3 ? j[2].get<double>() : 0.0};
  }

} // namespace hongtu::scene::detail
