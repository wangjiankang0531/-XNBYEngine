#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace hongtu::landscape {
  using Json = nlohmann::json;
  inline void validateLabel(const Json& label);
  inline Json emptyDocument() {
    return {{"schema", "hongtu.landscape.layers.v1"},
            {"scene", "zhumadian"},
            {"coordinates",
             {{"srs", "EPSG:32650"},
              {"origin", {225319.5, 3648440.1, 30.9}},
              {"axes", "east,north,up"},
              {"unit", "metre"}}},
            {"layers",
             Json::array({{{"id", "layer-1"},
                           {"name", "默认图层"},
                           {"visible", true},
                           {"locked", false},
                           {"features", Json::array()}}})}};
  }
  inline float cross2(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  }
  // Simple outer rings only. Reject crossing/touching non-adjacent edges rather than
  // silently rendering a convex fan over an invalid or concave region.
  inline std::vector<size_t> triangulate(const std::vector<glm::vec3>& p) {
    if (p.size() < 3)
      throw std::runtime_error("区域至少需要三个点");
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
      if (glm::length(glm::vec2(p[i] - p[(i + 1) % n])) < .001f)
        throw std::runtime_error("区域存在重复顶点");
      for (size_t j = i + 1; j < n; ++j) {
        if (j == i + 1 || (i == 0 && j == n - 1))
          continue;
        const auto a = p[i], b = p[(i + 1) % n], c = p[j], d = p[(j + 1) % n];
        auto on = [](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
          return std::abs(cross2(a, b, c)) < .0001f && c.x >= std::min(a.x, b.x) - .0001f &&
                 c.x <= std::max(a.x, b.x) + .0001f && c.y >= std::min(a.y, b.y) - .0001f &&
                 c.y <= std::max(a.y, b.y) + .0001f;
        };
        if ((cross2(a, b, c) * cross2(a, b, d) < 0 && cross2(c, d, a) * cross2(c, d, b) < 0) || on(a, b, c) ||
            on(a, b, d) || on(c, d, a) || on(c, d, b))
          throw std::runtime_error("区域边界不能自相交或相互接触");
      }
    }
    double area = 0;
    for (size_t i = 0; i < n; ++i)
      area += double(p[i].x) * p[(i + 1) % n].y - double(p[(i + 1) % n].x) * p[i].y;
    if (std::abs(area) < .001)
      throw std::runtime_error("区域面积为零");
    std::vector<size_t> ring(n), out;
    for (size_t i = 0; i < n; ++i)
      ring[i] = area > 0 ? i : n - 1 - i;
    while (ring.size() > 3) {
      bool clipped = false;
      for (size_t j = 0; j < ring.size(); ++j) {
        auto a = ring[(j + ring.size() - 1) % ring.size()], b = ring[j], c = ring[(j + 1) % ring.size()];
        if (cross2(p[a], p[b], p[c]) <= .00001f)
          continue;
        bool inside = false;
        for (auto k : ring)
          if (k != a && k != b && k != c && cross2(p[a], p[b], p[k]) >= -.00001f &&
              cross2(p[b], p[c], p[k]) >= -.00001f && cross2(p[c], p[a], p[k]) >= -.00001f)
            inside = true;
        if (inside)
          continue;
        out.insert(out.end(), {a, b, c});
        ring.erase(ring.begin() + j);
        clipped = true;
        break;
      }
      if (!clipped)
        throw std::runtime_error("区域无法三角化，请检查边界");
    }
    out.insert(out.end(), ring.begin(), ring.end());
    return out;
  }
  inline std::vector<glm::vec3> positions(const Json& feature) {
    std::vector<glm::vec3> p;
    for (const auto& v : feature.at("positions")) {
      if (!v.is_array() || v.size() != 3)
        throw std::runtime_error("坐标须为 XYZ 三元组");
      glm::vec3 q;
      for (int k = 0; k < 3; ++k) {
        if (!v[k].is_number() || !std::isfinite(v[k].get<double>()) || std::abs(v[k].get<double>()) > 100000)
          throw std::runtime_error("坐标超出驻马店局部范围");
        q[k] = v[k].get<float>();
      }
      p.push_back(q);
    }
    return p;
  }
  inline void validate(const Json& doc) {
    if (doc.at("schema") != "hongtu.landscape.layers.v1" || doc.at("scene") != "zhumadian" ||
        doc.at("coordinates") != emptyDocument().at("coordinates"))
      throw std::runtime_error("图层文件的格式或驻马店坐标系不匹配");
    const auto& layers = doc.at("layers");
    if (!layers.is_array() || layers.size() > 256)
      throw std::runtime_error("图层数应小于等于 256");
    std::set<std::string> ids;
    size_t total = 0;
    for (const auto& l : layers) {
      auto id = l.at("id").get<std::string>();
      if (id.empty() || !ids.insert("l:" + id).second)
        throw std::runtime_error("图层 ID 重复或为空");
      l.at("name").get<std::string>();
      l.at("visible").get<bool>();
      l.at("locked").get<bool>();
      if (!l.at("features").is_array())
        throw std::runtime_error("图层要素必须为数组");
      for (const auto& f : l.at("features")) {
        if (++total > 10000)
          throw std::runtime_error("此阶段最多支持 10000 个标注");
        auto fid = f.at("id").get<std::string>();
        if (fid.empty() || !ids.insert("f:" + fid).second)
          throw std::runtime_error("标注 ID 重复或为空");
        f.at("name").get<std::string>();
        if (f.contains("label"))
          validateLabel(f.at("label"));
        auto type = f.at("type").get<std::string>();
        auto p = positions(f);
        if (p.size() > 1000 || (type == "point"    ? p.size() != 1
                                : type == "line"   ? p.size() < 2
                                : type == "region" ? p.size() < 3
                                                   : true))
          throw std::runtime_error("标注类型或顶点数量不正确");
        if (type == "region")
          triangulate(p);
        const auto& s = f.at("style");
        for (const auto* field : {"color", "fillColor"}) {
          auto color = s.at(field).get<std::string>();
          if (color.size() != 7 || color[0] != '#' ||
              color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            throw std::runtime_error("颜色须使用 #RRGGBB 格式");
        }
        for (const auto* field : {"opacity", "fillOpacity", "widthPx", "sizePx"}) {
          double v = s.at(field).get<double>();
          double lo = std::string(field) == "widthPx" ? .5 : std::string(field) == "sizePx" ? 8 : 0;
          double hi = std::string(field) == "widthPx" ? 32 : std::string(field) == "sizePx" ? 128 : 1;
          if (!std::isfinite(v) || v < lo || v > hi)
            throw std::runtime_error("透明度、线宽或图标尺寸超出范围");
        }
        if (s.at("pattern") != "solid" && s.at("pattern") != "dash" && s.at("pattern") != "dot")
          throw std::runtime_error("不支持的线型");
        auto icon = s.at("icon").get<std::string>();
        if (icon != "pin" && icon != "circle" && icon != "square" && icon != "star" && icon != "warning" &&
            icon != "custom")
          throw std::runtime_error("不支持的图标");
        if (icon == "custom") {
          auto data = s.at("iconData").get<std::string>();
          if (data.rfind("data:image/png;base64,", 0) != 0 || data.size() > 6 * 1024 * 1024)
            throw std::runtime_error("自定义图标须为内嵌 PNG，小于 4 MiB");
        }
      }
    }
  }
  inline Json defaultStyle() {
    return {{"icon", "pin"},
            {"sizePx", 32},
            {"pattern", "solid"},
            {"widthPx", 4},
            {"color", "#22D3EE"},
            {"opacity", 1.0},
            {"fillColor", "#22D3EE"},
            {"fillOpacity", .25}};
  }
  inline Json defaultLabel() {
    return {{"visible", false},
            {"text", ""},
            {"fontPx", 16},
            {"color", "#FFFFFF"},
            {"opacity", 1.0},
            {"offset", {12, -16}}};
  }
  inline void validateLabel(const Json& label) {
    label.at("visible").get<bool>();
    if (label.at("text").get<std::string>().size() > 768)
      throw std::runtime_error("文字标牌最多 256 个汉字");
    const auto color = label.at("color").get<std::string>();
    if (color.size() != 7 || color[0] != '#' ||
        color.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
      throw std::runtime_error("标牌文字颜色须为 #RRGGBB");
    for (auto [key, lo, hi] :
         std::initializer_list<std::tuple<const char*, double, double>>{{"fontPx", 8, 48}, {"opacity", 0, 1}}) {
      const double value = label.at(key).get<double>();
      if (!std::isfinite(value) || value < lo || value > hi)
        throw std::runtime_error("标牌字号或透明度超出范围");
    }
    if (!label.at("offset").is_array() || label.at("offset").size() != 2)
      throw std::runtime_error("标牌偏移须为两个屏幕像素数");
    for (const auto& v : label.at("offset"))
      if (!v.is_number() || !std::isfinite(v.get<double>()) || std::abs(v.get<double>()) > 1000)
        throw std::runtime_error("标牌偏移超出范围");
  }
  inline glm::vec3 labelAnchor(const Json& feature) {
    const auto p = positions(feature);
    if (p.empty())
      return {};
    if (feature.at("type") == "point" || p.size() < 2)
      return p[0];
    if (feature.at("type") == "region" && p.size() >= 3) {
      const auto triangles = triangulate(p);
      float largest = -1;
      glm::vec3 anchor = p[0];
      for (size_t k = 0; k < triangles.size(); k += 3) {
        auto a = p[triangles[k]], b = p[triangles[k + 1]], c = p[triangles[k + 2]];
        const float area = std::abs(cross2(a, b, c));
        if (area > largest) {
          largest = area;
          anchor = (a + b + c) / 3.f;
        }
      }
      return anchor;
    }
    float length = 0;
    for (size_t k = 1; k < p.size(); ++k)
      length += glm::length(p[k] - p[k - 1]);
    float remaining = length * .5f;
    for (size_t k = 1; k < p.size(); ++k) {
      float segment = glm::length(p[k] - p[k - 1]);
      if (segment > .0001f && remaining <= segment)
        return glm::mix(p[k - 1], p[k], remaining / segment);
      remaining -= segment;
    }
    return p.back();
  }
  inline Json load(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > 16 * 1024 * 1024)
      throw std::runtime_error("图层文件过大");
    std::ifstream in(file, std::ios::binary);
    Json doc;
    in >> doc;
    validate(doc);
    return doc;
  }
  inline void save(const std::filesystem::path& file, const Json& doc) {
    validate(doc);
    auto temp = file;
    temp += L".tmp";
    {
      std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      out << doc.dump(2);
      out.flush();
      if (!out)
        throw std::runtime_error("写入图层文件失败");
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::runtime_error("替换图层文件失败，原文件仍保留");
#else
    std::filesystem::rename(temp, file);
#endif
  }
  inline std::vector<uint8_t> decode64(const std::string& text) {
    std::vector<uint8_t> out;
    unsigned value = 0;
    int bits = 0;
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (char c : text) {
      if (c == '=')
        break;
      auto k = chars.find(c);
      if (k == std::string::npos)
        throw std::runtime_error("PNG 的 Base64 编码无效");
      value = (value << 6) | unsigned(k);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        out.push_back(uint8_t(value >> bits));
      }
    }
    return out;
  }
  inline std::string encode64(const std::vector<uint8_t>& bytes) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (auto b : bytes) {
      v = (v << 8) | b;
      bits += 8;
      while (bits >= 6) {
        bits -= 6;
        out += chars[(v >> bits) & 63];
      }
    }
    if (bits)
      out += chars[(v << (6 - bits)) & 63];
    while (out.size() % 4)
      out += '=';
    return out;
  }
} // namespace hongtu::landscape
