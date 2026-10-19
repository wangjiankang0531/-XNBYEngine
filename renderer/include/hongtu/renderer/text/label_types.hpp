#pragma once

// 文字叠加的纯数据类型(标注与文字批):标注装配 builder(ht_scene)产出、版式 / GPU pass(renderer/text)消费。
// 不含任何 GPU / ImGui 依赖。字段与缺省值照搬编辑器(annotation.hpp 文字样式、editor_app.hpp ScreenTextItem)。

#include <glm/glm.hpp>

#include <string>

namespace hongtu::renderer::text {

  /// 标牌样式(Text 标注与图标文字标签共用)。数值规则见 label_layout.hpp。
  struct BadgeStyle {
    std::string text;
    float text_size_m = 1.0f; ///< 字号系数:font_px = clamp(14·text_size_m, 10, 48)(ImGui 单位)
    glm::vec4 color{1.0f};    ///< 前景色;标牌只用 rgb(alpha 恒 1,同编辑器)
    bool background = true;
    glm::vec4 bg_color{0.0f, 0.0f, 0.0f, 0.55f};
    bool leader = true;        ///< 引线 + 锚点圆
    int effect = 0;            ///< 0 无 1 描边 2 发光 3 阴影
    bool symbol_label = false; ///< 图标标签:标牌更高,越过图标(-(40 + symbol_size_m·18))
    float symbol_size_m = 1.0f;
  };

  /// 一个标牌锚点:pos 与该标注几何在同一绘制帧(编辑器 local 米 / geo 相对 entity RTC 原点;云端场景系)。
  struct LabelAnchor {
    glm::vec3 pos{0.0f};
    BadgeStyle style;
  };

  /// 屏幕文字(HUD)样式:nx, ny = 视口归一化坐标下的文字块中心;size_px 固定像素(ImGui 单位)。
  struct ScreenTextStyle {
    std::string text;
    float nx = 0.5f;
    float ny = 0.08f;
    float size_px = 28.0f;
    glm::vec4 color{1.0f}; ///< alpha 生效(与标牌不同)
    int effect = 0;
    bool background = false;
    glm::vec4 bg_color{0.0f, 0.0f, 0.0f, 0.5f};
  };

} // namespace hongtu::renderer::text
