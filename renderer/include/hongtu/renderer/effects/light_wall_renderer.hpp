#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

// =============================================================================
// LightWallRenderer — 电子围栏 / 光墙:把一圈边界点向上拉成垂直发光墙带。
//
// 数字孪生园区招牌效果。每条边界边生成一个垂直 quad(地面 → 墙高),片元
// alpha 自底向上衰减(底实顶虚),可叠加沿墙竖向流动光带。unlit、alpha 混
// 合、深度测试开、不写深度、双面 —— 作为半透明发光叠加层。
// =============================================================================

/// 一个垂直墙顶点:position(墙带空间,通常 RTC)+ uv(.x=沿周长弧长,
/// .y=高度比 0 底/1 顶)。
struct WallVertex {
  glm::vec3 position;
  glm::vec2 uv;
};

/// 边界点(同空间,首尾相连为闭环)拉成墙的三角网。`height` 墙高(米),
/// `closed` 闭环则补 last→first 一段。点数 < 2 返回空。
std::vector<WallVertex>
buildLightWall(const std::vector<glm::vec3>& boundary, float height,
               bool closed = true);

class LightWallRenderer {
 public:
  struct WallDraw {
    std::vector<glm::vec3> boundary;
    float                  height = 8.0f;
    glm::vec4              color  = {0.3f, 0.8f, 1.0f, 0.6f};
    bool                   closed = true;
    float                  flow_speed = 0.0f;  ///< 流动光带速度(0=静态)
    bool                   flow_vertical = false;  ///< true=竖向(自底向上) false=横向(沿周长)
  };

  struct Params {
    glm::mat4 view_proj;
    float     time_s = 0.0f;
  };

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);
  void record(const RHICommandBufferPtr& cmd, const Params& p,
              const std::vector<WallDraw>& walls);

 private:
  struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 color;       // 墙色(.a 基透明度)
    glm::vec4 fx;          // x=time y=flow_speed z=flow_vertical(0/1)
  };
  static_assert(sizeof(PushConstants) == 96, "LightWallRenderer PC layout");

  bool createPipeline(RHIRenderPassPtr render_pass);
  void upload(const std::vector<WallVertex>& verts);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_, fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;
  RHIBufferPtr         vbo_;
  uint32_t             vbo_capacity_ = 0;
};

}  // namespace hongtu::renderer
