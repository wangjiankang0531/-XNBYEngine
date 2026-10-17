#pragma once

// 离屏 geo 迷你场景:GlobeCamera + GlobeRenderer + 离屏 target,复用
// offscreen_demo 的"无 swapchain 设备 → 离屏渲染 → RGBA readback"路径。
// 设备不可用(headless / 无 MoltenVK)时 init() 返回 false,调用方应 GTEST_SKIP()。

#include <glm/glm.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/globe/globe_renderer.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

class GeoSceneFixture {
public:
  static constexpr std::uint32_t kW = 256, kH = 256;

  bool init() {
#ifdef __APPLE__
    if (!std::getenv("VK_ICD_FILENAMES")) {
      const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
      if (std::ifstream(icd).good()) {
        setenv("VK_ICD_FILENAMES", icd, 0);
        setenv("VK_DRIVER_FILES", icd, 0);
      }
    }
#endif
    // GlobeRenderer::loadDefaultTiles reads the relative path "texture/earth".
    // ctest runs with CWD = build dir where that path doesn't exist, so chdir
    // to the engine source root (compile-time define) to load the default
    // imagery deterministically and exercise the real textured render path.
#ifdef HT_ENGINE_SOURCE_DIR
    std::error_code ec;
    std::filesystem::current_path(HT_ENGINE_SOURCE_DIR, ec);
    if (ec)
      return false;
#endif
    factory_ = HongTu::RHI::Backend::createFactory(HongTu::RHI::Backend::Type::Vulkan);
    if (!factory_)
      return false;
    device_ = factory_->createDevice(false);
    if (!device_)
      return false;
    target_ = device_->createOffscreenTarget(kW, kH);
    if (!target_)
      return false;
    cmd_ = device_->createCommandBuffer();
    fence_ = device_->createFence(false);
    if (!cmd_ || !fence_)
      return false;

    globe_ = std::make_unique<hongtu::renderer::GlobeRenderer>(device_);
    globe_->setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
    if (!globe_->initialize(target_->getRenderPass()))
      return false;

    camera_.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
    ok_ = true;
    return true;
  }

  ~GeoSceneFixture() {
    // GlobeRenderer 必须在 device 之前销毁。
    globe_.reset();
    cmd_.reset();
    fence_.reset();
    target_.reset();
    if (factory_ && device_)
      factory_->destroyDevice(device_);
    if (factory_)
      HongTu::RHI::Backend::destroyFactory(factory_);
  }

  bool ok() const {
    return ok_;
  }

  void lookAtSite(double lat_deg, double lon_deg, double alt_m) {
    camera_.lookAt(hongtu::coordinate::Geodetic::fromDegrees(lat_deg, lon_deg, alt_m), alt_m, 0.0, -55.0);
    camera_.update(0.016f);
  }

  void render() {
    globe_->update(0.016f, camera_);
    cmd_->reset();
    cmd_->begin();
    HongTu::RHI::Rect2D area;
    area.offset = {0, 0};
    area.extent = {kW, kH};
    std::vector<std::array<float, 4>> clears = {{0.0f, 0.0f, 0.02f, 1.0f}, // space (会被地球覆盖)
                                                {1.0f, 0.0f, 0.0f, 0.0f}}; // depth = 1.0
    cmd_->beginRenderPass(target_->getRenderPass(), target_->getFramebuffer(), area, clears);
    HongTu::RHI::Viewport vp;
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(kW);
    vp.height = static_cast<float>(kH);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd_->setViewport(vp);
    cmd_->setScissor(area);
    globe_->render(cmd_, camera_);
    cmd_->endRenderPass();
    cmd_->end();
    device_->submit({cmd_}, {}, {}, fence_);
    fence_->wait();
    fence_->reset();
    device_->waitIdle();

    std::uint32_t w = 0, h = 0;
    target_->readbackToBuffer(pixels_, w, h);
  }

  // 取某像素 RGBA(0..255)。(0,0) 左上。
  glm::ivec4 at(int x, int y) const {
    std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
    if (i + 3 >= pixels_.size())
      return {0, 0, 0, 0};
    return {pixels_[i], pixels_[i + 1], pixels_[i + 2], pixels_[i + 3]};
  }

  hongtu::renderer::GlobeCamera& camera() {
    return camera_;
  }

  // 世界点 → 屏幕像素。与 GlobeCamera::pickWorld 同一套矩阵约定
  // (getProjectionMatrix 已含 Vulkan Y-flip)—— 与 test_globe_camera 的
  // projectToScreen 一致,故 pick→project 往返成立。
  glm::dvec2 project(const glm::dvec3& world) const {
    glm::dmat4 view = camera_.getViewMatrix();
    glm::dmat4 proj(camera_.getProjectionMatrix(static_cast<float>(kW) / static_cast<float>(kH)));
    glm::dvec4 clip = proj * view * glm::dvec4(world, 1.0);
    glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    return {(ndc.x * 0.5 + 0.5) * kW, (ndc.y * 0.5 + 0.5) * kH};
  }

private:
  bool ok_ = false;
  HongTu::RHI::RHIFactory* factory_ = nullptr;
  HongTu::RHI::RHIDevice* device_ = nullptr;
  HongTu::RHI::RHIOffscreenTargetPtr target_;
  HongTu::RHI::RHICommandBufferPtr cmd_;
  HongTu::RHI::RHIFencePtr fence_;
  std::unique_ptr<hongtu::renderer::GlobeRenderer> globe_;
  hongtu::renderer::GlobeCamera camera_;
  std::vector<std::uint8_t> pixels_;
};
