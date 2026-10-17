#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/renderer/terrain/terrain_system.hpp>

using namespace HongTu::RHI;
using namespace HongTu::Asset;
using namespace hongtu::renderer;

// ============================================================================
// Terrain Vertex: position + color (height-mapped) + UV (for imagery)
// ============================================================================

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 uv;

    static VertexInputBindingDescription getBindingDescription() {
        VertexInputBindingDescription b;
        b.binding = 0;
        b.stride = sizeof(TerrainVertex);
        b.perInstance = false;
        return b;
    }

    static std::vector<VertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VertexInputAttributeDescription> attrs(3);
        attrs[0] = {0, 0, Format::R32G32B32_SFLOAT, offsetof(TerrainVertex, position)};
        attrs[1] = {1, 0, Format::R32G32B32_SFLOAT, offsetof(TerrainVertex, color)};
        attrs[2] = {2, 0, Format::R32G32_SFLOAT, offsetof(TerrainVertex, uv)};
        return attrs;
    }
};

// ============================================================================
// Height-to-color mapping (terrain hypsometric tints)
// ============================================================================

static glm::vec3 heightToColor(float h, float min_h, float max_h) {
    float t = (max_h > min_h) ? (h - min_h) / (max_h - min_h) : 0.5f;
    t = glm::clamp(t, 0.0f, 1.0f);

    // Color ramp: deep blue -> green -> yellow -> brown -> white
    if (t < 0.0f) return {0.0f, 0.0f, 0.5f};           // Below sea: deep blue
    if (t < 0.15f) return glm::mix(glm::vec3(0.0f, 0.3f, 0.1f), glm::vec3(0.1f, 0.6f, 0.2f), t / 0.15f);
    if (t < 0.4f) return glm::mix(glm::vec3(0.1f, 0.6f, 0.2f), glm::vec3(0.8f, 0.8f, 0.2f), (t - 0.15f) / 0.25f);
    if (t < 0.7f) return glm::mix(glm::vec3(0.8f, 0.8f, 0.2f), glm::vec3(0.6f, 0.3f, 0.1f), (t - 0.4f) / 0.3f);
    return glm::mix(glm::vec3(0.6f, 0.3f, 0.1f), glm::vec3(1.0f, 1.0f, 1.0f), (t - 0.7f) / 0.3f);
}

// Simulated satellite imagery color (checkerboard pattern for visualization)
static glm::vec3 imageryColor(float u, float v) {
    int cu = static_cast<int>(u * 20) % 2;
    int cv = static_cast<int>(v * 20) % 2;
    float checker = (cu ^ cv) ? 0.85f : 0.75f;
    return {checker * 0.6f, checker * 0.7f, checker * 0.5f};
}

// ============================================================================
// Build terrain mesh from DEM with CDLOD LOD selection
// ============================================================================

struct TerrainMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t tile_count = 0;
};

static TerrainMesh buildTerrainMesh(const DEMData& dem, const glm::vec3& camera_pos,
                                     float fov_y, float screen_height,
                                     bool overlay_imagery) {
    TerrainMesh mesh;

    double gx0, gy0, gx1, gy1;
    dem.getBounds(gx0, gy0, gx1, gy1);
    float terrain_w = static_cast<float>(gx1 - gx0);
    float terrain_h = static_cast<float>(gy1 - gy0);

    // Use CDLOD to select tiles
    TerrainConfig cfg;
    cfg.max_lod_levels = 6;
    cfg.base_cell_size = std::max(terrain_w, terrain_h) / 64.0f;
    cfg.max_pixel_error = 4.0f;
    TerrainLODSystem lod_sys(cfg);

    auto selections = lod_sys.selectTiles(camera_pos, fov_y, screen_height,
                                          gx0, gy0, gx1, gy1);
    mesh.tile_count = static_cast<uint32_t>(selections.size());

    // For each selected tile, generate a mesh patch
    for (const auto& sel : selections) {
        float cell_size = lod_sys.cellSizeAtLOD(sel.lod_level);
        float tile_x = static_cast<float>(gx0) + sel.tile_x * cell_size;
        float tile_y = static_cast<float>(gy0) + sel.tile_y * cell_size;

        // Clamp tile to terrain bounds
        float tx0 = std::max(tile_x, static_cast<float>(gx0));
        float ty0 = std::max(tile_y, static_cast<float>(gy0));
        float tx1 = std::min(tile_x + cell_size, static_cast<float>(gx1));
        float ty1 = std::min(tile_y + cell_size, static_cast<float>(gy1));

        if (tx1 <= tx0 || ty1 <= ty0) continue;

        // Grid resolution per tile (fewer vertices at higher LOD)
        int res = std::max(2, 8 >> static_cast<int>(sel.lod_level));

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        // Generate vertices
        for (int iy = 0; iy <= res; ++iy) {
            for (int ix = 0; ix <= res; ++ix) {
                float fx = static_cast<float>(ix) / static_cast<float>(res);
                float fy = static_cast<float>(iy) / static_cast<float>(res);

                float wx = tx0 + fx * (tx1 - tx0);
                float wy = ty0 + fy * (ty1 - ty0);
                float wz = dem.sampleHeight(wx, wy) * 0.01f; // Scale height for visibility

                // UV for imagery
                float u = (wx - static_cast<float>(gx0)) / terrain_w;
                float v = (wy - static_cast<float>(gy0)) / terrain_h;

                // Color: blend height tint with simulated imagery
                glm::vec3 hc = heightToColor(dem.sampleHeight(wx, wy), dem.min_height, dem.max_height);
                glm::vec3 ic = imageryColor(u, v);
                glm::vec3 color = overlay_imagery ? glm::mix(hc, ic, 0.4f) : hc;

                // LOD-based wireframe tint (for debugging)
                float lod_tint = static_cast<float>(sel.lod_level) / 6.0f;
                color = glm::mix(color, glm::vec3(0.3f, 0.3f, 1.0f), lod_tint * 0.2f);

                mesh.vertices.push_back({{wx, wy, wz}, color, {u, v}});
            }
        }

        // Generate triangle indices
        for (int iy = 0; iy < res; ++iy) {
            for (int ix = 0; ix < res; ++ix) {
                uint32_t a = base + static_cast<uint32_t>(iy * (res + 1) + ix);
                uint32_t b = a + 1;
                uint32_t c = a + static_cast<uint32_t>(res + 1);
                uint32_t d = c + 1;
                mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(b);
                mesh.indices.push_back(b); mesh.indices.push_back(c); mesh.indices.push_back(d);
            }
        }
    }

    return mesh;
}

// ============================================================================
// Terrain Demo Application
// ============================================================================

class TerrainDemoApp {
 public:
    bool initialize(const std::string& dem_path);
    void run();
    void shutdown();

 private:
    SDL_Window* window = nullptr;
    int windowWidth = 1280, windowHeight = 720;

    RHIDevice* device = nullptr;
    RHIShaderPtr vertexShader, fragmentShader;
    RHIBufferPtr vertexBuffer, indexBuffer;
    RHIPipelinePtr solidPipeline, wirePipeline;
    RHIPipelineLayoutPtr pipelineLayout;
    RHIRenderPassPtr renderPass;
    std::vector<RHIFramebufferPtr> framebuffers;
    RHICommandBufferPtr commandBuffer;
    RHISemaphorePtr imageAvailSema, renderDoneSema;
    RHIFencePtr fence;

    DEMData dem;
    uint32_t indexCount = 0;
    uint32_t tileCount = 0;

    // Camera
    glm::vec3 camPos = {0, 0, 30};
    float camYaw = 0.0f, camPitch = -0.5f;
    float moveSpeed = 50.0f;
    bool wireframe = false;
    bool imagery = false;
    bool dragging = false;
    glm::vec2 lastMouse;

    // Timing
    std::chrono::high_resolution_clock::time_point lastTime;
    uint64_t frameCount = 0;
    double fpsTimer = 0;

    // Push constants: MVP + camera info
    struct PushData {
        glm::mat4 mvp;
    };

    bool loadDEM(const std::string& path);
    void rebuildMesh();
    bool initWindow();
    bool initVulkan();
    bool createShaders();
    bool createRenderPass();
    bool createFramebuffers();
    bool createPipelines();
    bool createSync();
    void handleInput(const SDL_Event& e, float dt);
    void renderFrame();
};

bool TerrainDemoApp::loadDEM(const std::string& path) {
    if (path == "--demo" || path.empty()) {
        std::cout << "Generating procedural terrain (256x256)..." << std::endl;
        dem = generateTestDEM(256, 256, 0.0, 0.0, 1.0, 500.0f);
    } else {
        std::ifstream file(path);
        if (!file) { std::cerr << "Cannot open: " << path << std::endl; return false; }
        std::ostringstream ss;
        ss << file.rdbuf();
        if (!loadDEMAscii(ss.str(), dem)) {
            std::cerr << "Failed to parse DEM file" << std::endl;
            return false;
        }
    }
    std::cout << "DEM: " << dem.width << "x" << dem.height
              << ", height range: [" << dem.min_height << ", " << dem.max_height << "]" << std::endl;
    return true;
}

void TerrainDemoApp::rebuildMesh() {
    auto mesh = buildTerrainMesh(dem, camPos, glm::radians(60.0f),
                                  static_cast<float>(windowHeight), imagery);

    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    tileCount = mesh.tile_count;
    indexCount = static_cast<uint32_t>(mesh.indices.size());

    // Recreate buffers
    auto makeBuffer = [&](const auto& data, BufferUsageFlagBits usage) -> RHIBufferPtr {
        BufferDesc desc;
        desc.size = data.size() * sizeof(data[0]);
        desc.usage = static_cast<BufferUsageFlags>(usage);
        desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
        auto buf = device->createBuffer(desc);
        if (buf) { void* p = buf->map(); std::memcpy(p, data.data(), desc.size); buf->unmap(); }
        return buf;
    };

    vertexBuffer = makeBuffer(mesh.vertices, BufferUsageFlagBits::Vertex);
    indexBuffer = makeBuffer(mesh.indices, BufferUsageFlagBits::Index);
}

bool TerrainDemoApp::initialize(const std::string& dem_path) {
    if (!loadDEM(dem_path)) return false;
    if (!initWindow()) return false;
    if (!initVulkan()) { SDL_DestroyWindow(window); SDL_Quit(); return false; }
    if (!createShaders() || !createRenderPass() || !createFramebuffers() ||
        !createPipelines() || !createSync()) { shutdown(); return false; }

    // Set initial camera above terrain center
    double cx, cy, cx1, cy1;
    dem.getBounds(cx, cy, cx1, cy1);
    camPos = {static_cast<float>((cx + cx1) * 0.5), static_cast<float>((cy + cy1) * 0.5),
              static_cast<float>(dem.max_height * 0.02f + 30.0f)};

    rebuildMesh();
    lastTime = std::chrono::high_resolution_clock::now();
    std::cout << "\n=== Terrain Demo Controls ===" << std::endl;
    std::cout << "WASD      - Move camera" << std::endl;
    std::cout << "Mouse     - Look around (hold right button)" << std::endl;
    std::cout << "Scroll    - Move up/down" << std::endl;
    std::cout << "F         - Toggle wireframe" << std::endl;
    std::cout << "I         - Toggle imagery overlay" << std::endl;
    std::cout << "Space     - Reset camera" << std::endl;
    std::cout << "Escape    - Quit" << std::endl;
    return true;
}

bool TerrainDemoApp::initWindow() {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("XNBY Terrain Demo",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              windowWidth,
                              windowHeight,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    return window != nullptr;
}

bool TerrainDemoApp::initVulkan() {
    auto factory = Backend::createFactory(Backend::Type::Vulkan);
    if (!factory) return false;
    unsigned int ec = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &ec, nullptr);
    std::vector<const char*> exts(ec);
    SDL_Vulkan_GetInstanceExtensions(window, &ec, exts.data());
    SwapchainDesc sc;
    sc.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
    sc.format = Format::R8G8B8A8_SRGB;
    sc.imageCount = 3; sc.vsync = true;
    device = factory->createDeviceWithSwapchain(window, sc, false, exts);
    Backend::destroyFactory(factory);
    if (!device) return false;
    std::cout << "GPU: " << device->getProperties().deviceName << std::endl;
    return true;
}

bool TerrainDemoApp::createShaders() {
    ShaderCompiler compiler;
    const char* vs = R"glsl(
        layout(location = 0) in vec3 inPos;
        layout(location = 1) in vec3 inColor;
        layout(location = 2) in vec2 inUV;
        layout(location = 0) out vec3 outColor;
        layout(location = 1) out vec2 outUV;
        layout(push_constant) uniform PC { mat4 mvp; } pc;
        void main() {
            gl_Position = pc.mvp * vec4(inPos, 1.0);
            outColor = inColor;
            outUV = inUV;
        }
    )glsl";
    const char* fs = R"glsl(
        layout(location = 0) in vec3 inColor;
        layout(location = 1) in vec2 inUV;
        layout(location = 0) out vec4 outColor;
        void main() {
            outColor = vec4(inColor, 1.0);
        }
    )glsl";

    auto vr = compiler.compileGLSLToSPIRV(vs, "main", "vertex");
    auto fr = compiler.compileGLSLToSPIRV(fs, "main", "fragment");
    if (!vr.success || !fr.success) {
        std::cerr << "Shader error: " << vr.errorMessage << fr.errorMessage << std::endl;
        return false;
    }
    ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex; vd.spirvCode = vr.spirvCode;
    ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
    vertexShader = device->createShader(vd);
    fragmentShader = device->createShader(fd);
    return vertexShader && fragmentShader;
}

bool TerrainDemoApp::createRenderPass() {
    RenderPassDesc rpd;
    AttachmentDescription ca;
    ca.format = device->getSwapchainFormat();
    ca.sampleCount = 1;
    ca.initialLayout = ImageLayout::Undefined;
    ca.finalLayout = ImageLayout::PresentSrc;
    ca.loadOpClear = true; ca.storeOpStore = true;
    rpd.attachments.push_back(ca);

    SubpassDescription sp;
    AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
    sp.colorAttachments.push_back(cr);
    rpd.subpasses.push_back(sp);

    SubpassDependency dep;
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dep.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
    rpd.dependencies.push_back(dep);

    renderPass = device->createRenderPass(rpd);
    return renderPass != nullptr;
}

bool TerrainDemoApp::createFramebuffers() {
    for (uint32_t i = 0; i < device->getSwapchainImageCount(); ++i) {
        auto img = device->getSwapchainImage(i);
        auto view = img->createView();
        auto fb = device->createFramebuffer(renderPass, {view}, device->getSwapchainExtent(), 1);
        if (!fb) return false;
        framebuffers.push_back(fb);
    }
    return true;
}

bool TerrainDemoApp::createPipelines() {
    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
    pcr.offset = 0; pcr.size = sizeof(PushData);
    ld.pushConstantRanges.push_back(pcr);
    pipelineLayout = device->createPipelineLayout(ld);

    auto bindDesc = TerrainVertex::getBindingDescription();
    auto attrDesc = TerrainVertex::getAttributeDescriptions();

    // Solid fill pipeline
    {
        GraphicsPipelineDesc pd;
        pd.shaders = {vertexShader, fragmentShader};
        pd.vertexInput.bindings = {bindDesc};
        pd.vertexInput.attributes = attrDesc;
        pd.primitiveTopology = PrimitiveTopology::TriangleList;
        pd.rasterization.cullMode = CullMode::None;
        pd.rasterization.fillMode = FillMode::Solid;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.renderPass = renderPass;
        pd.pipelineLayout = pipelineLayout;
        solidPipeline = device->createGraphicsPipeline(pd);
    }

    // Wireframe pipeline
    {
        GraphicsPipelineDesc pd;
        pd.shaders = {vertexShader, fragmentShader};
        pd.vertexInput.bindings = {bindDesc};
        pd.vertexInput.attributes = attrDesc;
        pd.primitiveTopology = PrimitiveTopology::TriangleList;
        pd.rasterization.cullMode = CullMode::None;
        pd.rasterization.fillMode = FillMode::Wireframe;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.renderPass = renderPass;
        pd.pipelineLayout = pipelineLayout;
        wirePipeline = device->createGraphicsPipeline(pd);
    }

    commandBuffer = device->createCommandBuffer();
    return solidPipeline && wirePipeline && commandBuffer;
}

bool TerrainDemoApp::createSync() {
    imageAvailSema = device->createSemaphore();
    renderDoneSema = device->createSemaphore();
    fence = device->createFence(true);
    return imageAvailSema && renderDoneSema && fence;
}

void TerrainDemoApp::handleInput(const SDL_Event& e, float dt) {
    switch (e.type) {
        case SDL_MOUSEWHEEL:
            camPos.z += static_cast<float>(e.wheel.y) * moveSpeed * 0.3f;
            camPos.z = std::max(camPos.z, 1.0f);
            rebuildMesh(); // LOD changes with altitude
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_RIGHT) {
                dragging = true;
                lastMouse = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_RIGHT) dragging = false;
            break;
        case SDL_MOUSEMOTION:
            if (dragging) {
                glm::vec2 cur = {static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)};
                glm::vec2 delta = cur - lastMouse;
                camYaw -= delta.x * 0.003f;
                camPitch -= delta.y * 0.003f;
                camPitch = glm::clamp(camPitch, -1.5f, -0.1f);
                lastMouse = cur;
            }
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
                case SDLK_f:
                    wireframe = !wireframe;
                    std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << std::endl;
                    break;
                case SDLK_i:
                    imagery = !imagery;
                    rebuildMesh();
                    std::cout << "Imagery overlay: " << (imagery ? "ON" : "OFF") << std::endl;
                    break;
                case SDLK_SPACE: {
                    double cx, cy, cx1, cy1;
                    dem.getBounds(cx, cy, cx1, cy1);
                    camPos = {static_cast<float>((cx+cx1)*0.5), static_cast<float>((cy+cy1)*0.5),
                              static_cast<float>(dem.max_height * 0.02f + 30.0f)};
                    camYaw = 0; camPitch = -0.5f;
                    rebuildMesh();
                    break;
                }
                default: break;
            }
            break;
    }
}

void TerrainDemoApp::renderFrame() {
    fence->wait();
    fence->reset();
    uint32_t imgIdx = device->acquireNextImage(imageAvailSema);

    // WASD movement
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    float spd = moveSpeed * 0.016f;
    glm::vec3 forward = {std::sin(camYaw), std::cos(camYaw), 0};
    glm::vec3 right = {std::cos(camYaw), -std::sin(camYaw), 0};
    bool moved = false;
    if (keys[SDL_SCANCODE_W]) { camPos += forward * spd; moved = true; }
    if (keys[SDL_SCANCODE_S]) { camPos -= forward * spd; moved = true; }
    if (keys[SDL_SCANCODE_A]) { camPos -= right * spd; moved = true; }
    if (keys[SDL_SCANCODE_D]) { camPos += right * spd; moved = true; }
    if (keys[SDL_SCANCODE_Q]) { camPos.z += spd; moved = true; }
    if (keys[SDL_SCANCODE_E]) { camPos.z -= spd; moved = true; }
    if (moved) rebuildMesh();

    // Build MVP
    glm::vec3 lookDir = {std::sin(camYaw) * std::cos(camPitch),
                          std::cos(camYaw) * std::cos(camPitch),
                          std::sin(camPitch)};
    auto view = glm::lookAt(camPos, camPos + lookDir, {0, 0, 1});
    auto proj = glm::perspective(glm::radians(60.0f),
                                  static_cast<float>(windowWidth) / static_cast<float>(windowHeight),
                                  0.1f, 10000.0f);
    PushData pd;
    pd.mvp = proj * view;

    commandBuffer->reset();
    commandBuffer->begin();

    Rect2D area;
    area.offset = {0, 0};
    area.extent = device->getSwapchainExtent();
    std::vector<std::array<float, 4>> clearColors = {{0.4f, 0.6f, 0.9f, 1.0f}}; // Sky blue
    commandBuffer->beginRenderPass(renderPass, framebuffers[imgIdx], area, clearColors);

    Viewport vp;
    vp.x = 0; vp.y = 0;
    vp.width = static_cast<float>(area.extent.width);
    vp.height = static_cast<float>(area.extent.height);
    vp.minDepth = 0; vp.maxDepth = 1;
    commandBuffer->setViewport(vp);
    commandBuffer->setScissor(area);

    if (indexCount > 0 && vertexBuffer && indexBuffer) {
        commandBuffer->bindPipeline(wireframe ? wirePipeline : solidPipeline);
        commandBuffer->pushConstants(pipelineLayout,
            static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex), 0, sizeof(pd), &pd);
        commandBuffer->bindVertexBuffer(0, vertexBuffer);
        commandBuffer->bindIndexBuffer(indexBuffer);
        commandBuffer->drawIndexed(indexCount);
    }

    commandBuffer->endRenderPass();
    commandBuffer->end();

    device->submit({commandBuffer}, {imageAvailSema}, {renderDoneSema}, fence);
    device->present(renderDoneSema);
}

void TerrainDemoApp::run() {
    bool running = true;
    SDL_Event event;
    while (running) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { running = false; break; }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
            handleInput(event, dt);
        }
        if (!running) break;
        renderFrame();
        frameCount++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            double fps = frameCount / fpsTimer;
            std::string title = "XNBY Terrain | " + std::to_string(tileCount) + " tiles | " +
                                std::to_string(indexCount / 3) + " tris | " + std::to_string(static_cast<int>(fps)) +
                                " FPS" + (wireframe ? " [Wire]" : "") + (imagery ? " [Imagery]" : "");
            SDL_SetWindowTitle(window, title.c_str());
            frameCount = 0; fpsTimer = 0;
        }
    }
    device->waitIdle();
}

void TerrainDemoApp::shutdown() {
    if (device) {
        device->waitIdle();
        commandBuffer.reset(); fence.reset();
        imageAvailSema.reset(); renderDoneSema.reset();
        solidPipeline.reset(); wirePipeline.reset();
        pipelineLayout.reset(); framebuffers.clear();
        renderPass.reset();
        vertexBuffer.reset(); indexBuffer.reset();
        vertexShader.reset(); fragmentShader.reset();
        delete device; device = nullptr;
    }
    if (window) { SDL_DestroyWindow(window); SDL_Quit(); }
}

// ============================================================================
int main(int argc, char* argv[]) {
    std::string path = "--demo";
    if (argc > 1) path = argv[1];

    if (path == "--help" || path == "-h") {
      std::cout << "XNBY Terrain Rendering Demo\n\n"
                << "Usage: terrain_demo [dem_file.asc|--demo]\n\n"
                << "Features:\n"
                << "  - CDLOD multi-level terrain LOD\n"
                << "  - DEM data import (ESRI ASCII Grid)\n"
                << "  - Height-based color mapping (hypsometric tints)\n"
                << "  - Simulated satellite imagery overlay\n"
                << "  - Wireframe visualization\n\n"
                << "Controls:\n"
                << "  WASD/QE     - Move camera\n"
                << "  Right mouse - Look around\n"
                << "  Scroll      - Move up/down\n"
                << "  F           - Toggle wireframe\n"
                << "  I           - Toggle imagery overlay\n"
                << "  Space       - Reset camera\n"
                << "  Escape      - Quit\n";
      return 0;
    }

    TerrainDemoApp app;
    if (!app.initialize(path)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
