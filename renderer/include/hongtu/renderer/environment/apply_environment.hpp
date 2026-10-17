#pragma once

#include "hongtu/renderer/environment/environment_settings.hpp"
#include "hongtu/renderer/pbr/forward_scene_renderer.hpp"
#include "hongtu/renderer/pbr/hdr_compose_target.hpp"

namespace hongtu::renderer {

/// 把规范环境设置映射到前向渲染参数 + compose 后处理参数。
///
/// `time` 驱动 tod 推进与 grain/雨雪相位。`sun_uv` 不在此设置(留默认,
/// 调用方在有相机时覆盖)。
///
/// 移植自编辑器 editor_app.cpp:1694-1856 的 EnvironmentDoc → ViewportRenderer::Lighting
/// 映射,保留所有业务逻辑(条件门控、skylight_bleed 基值、tod 接管等)。
void applyEnvironment(const EnvironmentSettings& s, float time,
                      ForwardSceneRenderer::FrameParams& fp,
                      HdrComposeTarget::ComposeParams& cp);

}  // namespace hongtu::renderer
