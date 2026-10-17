// offscreen_demo — verifies RHI OffscreenTarget end-to-end:
//   1. Create Vulkan device with no swapchain (no SDL window).
//   2. Allocate a 256×256 offscreen target.
//   3. Build a triangle pipeline against target->getRenderPass().
//   4. Record + submit one frame.
//   5. Readback to RGBA bytes → stb_image_write PNG.
//
// This is the cleanest way to verify A4 actually works without the heavy
// scaffolding of a real swapchain. The same pattern is used by cloud_server's
// future render path.
//
// Usage:
//   offscreen_demo --frames=1 --screenshot=/tmp/o.png

#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace HongTu::RHI;
using HongTu::Asset::ShaderCompiler;

namespace {

struct Cli {
  int         frames = 1;
  std::string screenshot;
  int         width  = 256;
  int         height = 256;
};

bool parseCli(int argc, char** argv, Cli& cli) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if      (a.rfind("--frames=",     0) == 0) cli.frames     = std::stoi(a.substr(9));
    else if (a.rfind("--screenshot=", 0) == 0) cli.screenshot = a.substr(13);
    else if (a.rfind("--width=",      0) == 0) cli.width      = std::stoi(a.substr(8));
    else if (a.rfind("--height=",     0) == 0) cli.height     = std::stoi(a.substr(9));
    else if (a == "--help") {
      std::cout << "offscreen_demo [--frames=N --screenshot=PATH] "
                   "[--width=W --height=H]\n";
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES",  icd, 0);
    }
  }
#endif

  Cli cli;
  if (!parseCli(argc, argv, cli)) return 0;

  // ---------------------------------------------------------------------------
  // 1. RHI device — no surface, no swapchain.
  // ---------------------------------------------------------------------------
  auto factory = Backend::createFactory(Backend::Type::Vulkan);
  if (!factory) { std::cerr << "createFactory failed\n"; return 1; }
  RHIDevice* device = factory->createDevice(false);
  if (!device) { std::cerr << "createDevice failed\n"; return 1; }

  // ---------------------------------------------------------------------------
  // 2. Offscreen target.
  // ---------------------------------------------------------------------------
  auto target = device->createOffscreenTarget(
      static_cast<uint32_t>(cli.width),
      static_cast<uint32_t>(cli.height));
  if (!target) { std::cerr << "createOffscreenTarget failed\n"; return 1; }

  // ---------------------------------------------------------------------------
  // 3. Minimal triangle pipeline against target's render pass.
  // ---------------------------------------------------------------------------
  ShaderCompiler compiler;
  const char* vsrc = R"glsl(
    vec2 positions[3] = vec2[](
      vec2( 0.0, -0.6),
      vec2(-0.6,  0.6),
      vec2( 0.6,  0.6)
    );
    vec3 colors[3] = vec3[](
      vec3(1.0, 0.3, 0.2),
      vec3(0.2, 1.0, 0.3),
      vec3(0.3, 0.4, 1.0)
    );
    layout(location = 0) out vec3 vColor;
    void main() {
      gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
      vColor      = colors[gl_VertexIndex];
    }
  )glsl";
  const char* fsrc = R"glsl(
    layout(location = 0) in  vec3 vColor;
    layout(location = 0) out vec4 outColor;
    void main() { outColor = vec4(vColor, 1.0); }
  )glsl";

  auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vs_r.success || !fs_r.success) {
    std::cerr << "shader compile failed vs='" << vs_r.errorMessage
              << "' fs='" << fs_r.errorMessage << "'\n";
    return 1;
  }
  ShaderDesc vsd; vsd.stage = ShaderStageFlagBits::Vertex;
  vsd.spirvCode = vs_r.spirvCode;
  auto vs = device->createShader(vsd);
  ShaderDesc fsd; fsd.stage = ShaderStageFlagBits::Fragment;
  fsd.spirvCode = fs_r.spirvCode;
  auto fs = device->createShader(fsd);
  if (!vs || !fs) { std::cerr << "createShader failed\n"; return 1; }

  PipelineLayoutDesc plDesc;
  auto pipeline_layout = device->createPipelineLayout(plDesc);
  if (!pipeline_layout) { std::cerr << "createPipelineLayout failed\n"; return 1; }

  GraphicsPipelineDesc pd;
  pd.shaders = {vs, fs};
  pd.primitiveTopology = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode = CullMode::None;
  pd.rasterization.fillMode = FillMode::Solid;
  pd.depthStencil.depthTestEnable = false;
  pd.depthStencil.depthWriteEnable = false;
  ColorBlendAttachmentState blend;
  blend.blendEnable = false;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = target->getRenderPass();
  pd.pipelineLayout = pipeline_layout;
  auto pipeline = device->createGraphicsPipeline(pd);
  if (!pipeline) { std::cerr << "createGraphicsPipeline failed\n"; return 1; }

  // ---------------------------------------------------------------------------
  // 4. Record + submit one frame.
  // ---------------------------------------------------------------------------
  auto cmd  = device->createCommandBuffer();
  auto fence = device->createFence(false);

  for (int f = 0; f < cli.frames; ++f) {
    cmd->reset();
    cmd->begin();
    Rect2D area; area.offset = {0, 0};
    area.extent = {static_cast<uint32_t>(cli.width),
                    static_cast<uint32_t>(cli.height)};
    std::vector<std::array<float, 4>> clears = {
        {0.10f, 0.12f, 0.18f, 1.0f},  // dark blue
        {1.0f, 0.0f, 0.0f, 0.0f}};     // depth = 1.0
    cmd->beginRenderPass(target->getRenderPass(), target->getFramebuffer(),
                          area, clears);
    Viewport vp;
    vp.x = 0; vp.y = 0;
    vp.width  = static_cast<float>(cli.width);
    vp.height = static_cast<float>(cli.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    cmd->setViewport(vp);
    cmd->setScissor(area);
    cmd->bindPipeline(pipeline);
    cmd->draw(3, 1, 0, 0);
    cmd->endRenderPass();
    cmd->end();
    device->submit({cmd}, {}, {}, fence);
    fence->wait();
    fence->reset();
  }
  device->waitIdle();

  // ---------------------------------------------------------------------------
  // 5. Readback + PNG write.
  // ---------------------------------------------------------------------------
  if (!cli.screenshot.empty()) {
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!target->readbackToBuffer(rgba, w, h)) {
      std::cerr << "readbackToBuffer failed\n";
      return 1;
    }
    if (stbi_write_png(cli.screenshot.c_str(),
                       static_cast<int>(w), static_cast<int>(h), 4,
                       rgba.data(), static_cast<int>(w) * 4) == 0) {
      std::cerr << "stbi_write_png failed\n";
      return 1;
    }
    std::cout << "[offscreen_demo] wrote " << cli.screenshot
              << " (" << w << "x" << h << ", " << rgba.size() << " bytes)\n";
  }

  target.reset();
  factory->destroyDevice(device);
  Backend::destroyFactory(factory);
  return 0;
}
