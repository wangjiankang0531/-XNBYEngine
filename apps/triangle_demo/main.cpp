#include <iostream>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

// RHI Headers
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/asset/ShaderCompiler.h>

using namespace HongTu::RHI;
using namespace HongTu::Asset;

// ============================================================================
// Vertex Definition
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;

    static VertexInputBindingDescription getBindingDescription() {
        VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.perInstance = false;
        return binding;
    }

    static std::vector<VertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VertexInputAttributeDescription> attributes(2);

        // Position
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = Format::R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);

        // Color
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = Format::R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, color);

        return attributes;
    }
};

// ============================================================================
// Triangle Demo Application
// ============================================================================

class TriangleDemoApp {
public:
    bool initialize();
    void run();
    void shutdown();

private:
    // SDL
    SDL_Window* window = nullptr;
    int windowWidth = 1280;
    int windowHeight = 720;

    // Vulkan/RHI
    RHIDevice* device = nullptr;
    RHIShaderPtr vertexShader;
    RHIShaderPtr fragmentShader;
    RHIBufferPtr vertexBuffer;
    RHIBufferPtr indexBuffer;
    RHIPipelinePtr pipeline;
    RHIPipelineLayoutPtr pipelineLayout;
    RHIRenderPassPtr renderPass;
    std::vector<RHIFramebufferPtr> framebuffers;
    RHICommandBufferPtr commandBuffer;
    RHISemaphorePtr imageAvailableSemaphore;
    RHISemaphorePtr renderFinishedSemaphore;
    RHIFencePtr inFlightFence;

    // Transform state
    glm::vec2 position = {0.0f, 0.0f};   // translation
    float rotation = 0.0f;                // rotation angle (radians)
    float scale = 1.0f;                   // uniform scale
    bool autoRotate = true;               // auto-rotate toggle

    // Mouse drag state
    bool dragging = false;
    glm::vec2 lastMouse = {0.0f, 0.0f};

    // Timing
    std::chrono::high_resolution_clock::time_point startTime;
    uint64_t frameCount = 0;
    double totalTime = 0.0;
    double deltaTime = 0.0;

    // Helper methods
    void handleInput(const SDL_Event& event);
    bool initializeWindow();
    bool initializeVulkan();
    bool createShaders();
    bool createBuffers();
    bool createRenderPass();
    bool createFramebuffers();
    bool createPipeline();
    bool createSynchronizationPrimitives();
    void recordCommandBuffers();
    void updateFrame();
    void renderFrame();
    void cleanupWindow();
    void cleanupVulkan();
};

bool TriangleDemoApp::initialize() {
  std::cout << "Initializing XNBY Triangle Demo..." << std::endl;

  if (!initializeWindow()) {
    std::cerr << "Failed to initialize window" << std::endl;
    return false;
  }

    if (!initializeVulkan()) {
        std::cerr << "Failed to initialize Vulkan" << std::endl;
        cleanupWindow();
        return false;
    }

    if (!createShaders()) {
        std::cerr << "Failed to create shaders" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    if (!createBuffers()) {
        std::cerr << "Failed to create buffers" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    if (!createRenderPass()) {
        std::cerr << "Failed to create render pass" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    if (!createFramebuffers()) {
        std::cerr << "Failed to create framebuffers" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    if (!createPipeline()) {
        std::cerr << "Failed to create pipeline" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    if (!createSynchronizationPrimitives()) {
        std::cerr << "Failed to create synchronization primitives" << std::endl;
        cleanupVulkan();
        cleanupWindow();
        return false;
    }

    startTime = std::chrono::high_resolution_clock::now();
    std::cout << "Initialization complete!" << std::endl;
    return true;
}

bool TriangleDemoApp::initializeWindow() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow("XNBY Engine - Triangle Demo",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              windowWidth,
                              windowHeight,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    return true;
}

bool TriangleDemoApp::initializeVulkan() {
    // Create Vulkan factory
    auto factory = Backend::createFactory(Backend::Type::Vulkan);
    if (!factory) {
        std::cerr << "Failed to create Vulkan factory" << std::endl;
        return false;
    }

    // Get required extensions from SDL
    unsigned int extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
        std::cerr << "Failed to get Vulkan extensions: " << SDL_GetError() << std::endl;
        Backend::destroyFactory(factory);
        return false;
    }

    std::vector<const char*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data())) {
        std::cerr << "Failed to get Vulkan extensions: " << SDL_GetError() << std::endl;
        Backend::destroyFactory(factory);
        return false;
    }

    // Create device with swapchain
    SwapchainDesc swapchainDesc;
    swapchainDesc.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
    swapchainDesc.format = Format::R8G8B8A8_SRGB;
    swapchainDesc.imageCount = 3;
    swapchainDesc.vsync = true;

    device = factory->createDeviceWithSwapchain(window, swapchainDesc, false, extensions);

    Backend::destroyFactory(factory);

    if (!device) {
        std::cerr << "Failed to create Vulkan device" << std::endl;
        return false;
    }

    std::cout << "Vulkan device created successfully" << std::endl;
    const auto& props = device->getProperties();
    std::cout << "GPU: " << props.deviceName << std::endl;

    return true;
}

bool TriangleDemoApp::createShaders() {
    ShaderCompiler compiler;

    // Vertex shader
    const char* vertexShaderSource = R"glsl(
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inColor;

        layout(location = 0) out vec3 outColor;

        layout(push_constant) uniform PushConstants {
            mat4 transform;
        } pc;

        void main() {
            gl_Position = pc.transform * vec4(inPosition, 1.0);
            outColor = inColor;
        }
    )glsl";

    // Fragment shader
    const char* fragmentShaderSource = R"glsl(
        layout(location = 0) in vec3 inColor;
        layout(location = 0) out vec4 outColor;

        void main() {
            outColor = vec4(inColor, 1.0);
        }
    )glsl";

    // Compile vertex shader
    auto vertexResult = compiler.compileGLSLToSPIRV(vertexShaderSource, "main", "vertex");
    if (!vertexResult.success) {
        std::cerr << "Failed to compile vertex shader: " << vertexResult.errorMessage << std::endl;
        return false;
    }

    // Compile fragment shader
    auto fragmentResult = compiler.compileGLSLToSPIRV(fragmentShaderSource, "main", "fragment");
    if (!fragmentResult.success) {
        std::cerr << "Failed to compile fragment shader: " << fragmentResult.errorMessage << std::endl;
        return false;
    }

    // Create shader modules
    ShaderDesc vertDesc;
    vertDesc.stage = ShaderStageFlagBits::Vertex;
    vertDesc.spirvCode = vertexResult.spirvCode;
    vertexShader = device->createShader(vertDesc);

    ShaderDesc fragDesc;
    fragDesc.stage = ShaderStageFlagBits::Fragment;
    fragDesc.spirvCode = fragmentResult.spirvCode;
    fragmentShader = device->createShader(fragDesc);

    if (!vertexShader || !fragmentShader) {
        std::cerr << "Failed to create shader modules" << std::endl;
        return false;
    }

    std::cout << "Shaders compiled successfully" << std::endl;
    std::cout << "Vertex shader: " << vertexResult.spirvCode.size() * 4 << " bytes, "
              << vertexResult.compilationTimeMs << "ms" << std::endl;
    std::cout << "Fragment shader: " << fragmentResult.spirvCode.size() * 4 << " bytes, "
              << fragmentResult.compilationTimeMs << "ms" << std::endl;

    return true;
}

bool TriangleDemoApp::createBuffers() {
    // Create colored triangle vertices
    std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // Red
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},   // Green
        {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},    // Blue
    };

    std::vector<uint32_t> indices = {0, 1, 2};

    // Create vertex buffer
    BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = vertices.size() * sizeof(Vertex);
    vertexBufferDesc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    vertexBufferDesc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;

    vertexBuffer = device->createBuffer(vertexBufferDesc);
    if (!vertexBuffer) {
        std::cerr << "Failed to create vertex buffer" << std::endl;
        return false;
    }

    // Copy vertex data
    void* vbData = vertexBuffer->map();
    std::memcpy(vbData, vertices.data(), vertexBufferDesc.size);
    vertexBuffer->unmap();

    // Create index buffer
    BufferDesc indexBufferDesc;
    indexBufferDesc.size = indices.size() * sizeof(uint32_t);
    indexBufferDesc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    indexBufferDesc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;

    indexBuffer = device->createBuffer(indexBufferDesc);
    if (!indexBuffer) {
        std::cerr << "Failed to create index buffer" << std::endl;
        return false;
    }

    // Copy index data
    void* ibData = indexBuffer->map();
    std::memcpy(ibData, indices.data(), indexBufferDesc.size);
    indexBuffer->unmap();

    std::cout << "Buffers created successfully" << std::endl;
    return true;
}

bool TriangleDemoApp::createRenderPass() {
    RenderPassDesc renderPassDesc;

    // Color attachment
    AttachmentDescription colorAttachment;
    colorAttachment.format = device->getSwapchainFormat();
    colorAttachment.sampleCount = 1;
    colorAttachment.initialLayout = ImageLayout::Undefined;
    colorAttachment.finalLayout = ImageLayout::PresentSrc;
    colorAttachment.loadOpClear = true;
    colorAttachment.storeOpStore = true;
    renderPassDesc.attachments.push_back(colorAttachment);

    // Subpass
    SubpassDescription subpass;
    AttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = ImageLayout::ColorAttachmentOptimal;
    subpass.colorAttachments.push_back(colorRef);
    renderPassDesc.subpasses.push_back(subpass);

    // Dependency
    SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dependency.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dependency.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
    renderPassDesc.dependencies.push_back(dependency);

    renderPass = device->createRenderPass(renderPassDesc);
    if (!renderPass) {
        std::cerr << "Failed to create render pass" << std::endl;
        return false;
    }

    std::cout << "Render pass created successfully" << std::endl;
    return true;
}

bool TriangleDemoApp::createFramebuffers() {
    uint32_t imageCount = device->getSwapchainImageCount();

    for (uint32_t i = 0; i < imageCount; ++i) {
        auto swapchainImage = device->getSwapchainImage(i);
        if (!swapchainImage) {
            std::cerr << "Failed to get swapchain image " << i << std::endl;
            return false;
        }

        auto imageView = swapchainImage->createView();

        FramebufferDesc fbDesc;
        fbDesc.renderPass = renderPass->getHandle();
        fbDesc.attachments.push_back(imageView->getHandle());
        fbDesc.extent = device->getSwapchainExtent();
        fbDesc.layers = 1;

        auto framebuffer = device->createFramebuffer(renderPass, {imageView}, device->getSwapchainExtent(), 1);
        if (!framebuffer) {
            std::cerr << "Failed to create framebuffer " << i << std::endl;
            return false;
        }

        framebuffers.push_back(framebuffer);
    }

    std::cout << "Framebuffers created successfully (" << framebuffers.size() << " framebuffers)" << std::endl;
    return true;
}

bool TriangleDemoApp::createPipeline() {
    // Create pipeline layout with push constants for transform matrix
    PipelineLayoutDesc layoutDesc;
    PushConstantRange pushConstant;
    pushConstant.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::mat4);
    layoutDesc.pushConstantRanges.push_back(pushConstant);
    pipelineLayout = device->createPipelineLayout(layoutDesc);
    if (!pipelineLayout) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        return false;
    }

    // Create graphics pipeline
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shaders = {vertexShader, fragmentShader};
    pipelineDesc.vertexInput.bindings = {Vertex::getBindingDescription()};
    pipelineDesc.vertexInput.attributes = Vertex::getAttributeDescriptions();
    pipelineDesc.primitiveTopology = PrimitiveTopology::TriangleList;
    pipelineDesc.rasterization.cullMode = CullMode::None;  // No backface culling for demo
    pipelineDesc.rasterization.fillMode = FillMode::Solid;
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    pipelineDesc.renderPass = renderPass;
    pipelineDesc.pipelineLayout = pipelineLayout;

    pipeline = device->createGraphicsPipeline(pipelineDesc);
    if (!pipeline) {
        std::cerr << "Failed to create graphics pipeline" << std::endl;
        return false;
    }

    // Create command buffer
    commandBuffer = device->createCommandBuffer();
    if (!commandBuffer) {
        std::cerr << "Failed to create command buffer" << std::endl;
        return false;
    }

    std::cout << "Pipeline created successfully" << std::endl;
    return true;
}

bool TriangleDemoApp::createSynchronizationPrimitives() {
    imageAvailableSemaphore = device->createSemaphore();
    if (!imageAvailableSemaphore) {
        std::cerr << "Failed to create image available semaphore" << std::endl;
        return false;
    }

    renderFinishedSemaphore = device->createSemaphore();
    if (!renderFinishedSemaphore) {
        std::cerr << "Failed to create render finished semaphore" << std::endl;
        return false;
    }

    inFlightFence = device->createFence(true);
    if (!inFlightFence) {
        std::cerr << "Failed to create in-flight fence" << std::endl;
        return false;
    }

    std::cout << "Synchronization primitives created successfully" << std::endl;
    return true;
}

void TriangleDemoApp::recordCommandBuffers() {
    // Placeholder for command buffer recording
}

void TriangleDemoApp::handleInput(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEWHEEL:
            // Scroll to zoom
            scale *= (event.wheel.y > 0) ? 1.1f : 0.9f;
            scale = glm::clamp(scale, 0.1f, 10.0f);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                lastMouse = {static_cast<float>(event.button.x), static_cast<float>(event.button.y)};
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            }
            break;

        case SDL_MOUSEMOTION:
            if (dragging) {
                glm::vec2 cur = {static_cast<float>(event.motion.x), static_cast<float>(event.motion.y)};
                glm::vec2 delta = cur - lastMouse;

                if (SDL_GetModState() & KMOD_SHIFT) {
                    // Shift+drag = rotate
                    rotation += delta.x * 0.01f;
                } else {
                    // Drag = translate (normalized to [-1,1])
                    position.x += delta.x / static_cast<float>(windowWidth) * 2.0f;
                    position.y += delta.y / static_cast<float>(windowHeight) * 2.0f;
                }
                lastMouse = cur;
            }
            break;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_r: autoRotate = !autoRotate; break;           // R = toggle auto-rotate
                case SDLK_SPACE: position = {0,0}; rotation = 0; scale = 1; break; // Space = reset
                case SDLK_LEFT:  position.x -= 0.05f; break;
                case SDLK_RIGHT: position.x += 0.05f; break;
                case SDLK_UP:    position.y -= 0.05f; break;
                case SDLK_DOWN:  position.y += 0.05f; break;
                default: break;
            }
            break;
    }
}

void TriangleDemoApp::updateFrame() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    deltaTime = std::chrono::duration<double>(currentTime - startTime).count();
    startTime = currentTime;
    totalTime += deltaTime;
    frameCount++;

    // Auto-rotate
    if (autoRotate) {
        rotation += static_cast<float>(deltaTime) * 1.0f;
    }

    if (frameCount % 120 == 0) {
        double fps = frameCount / totalTime;
        std::cout << "FPS: " << fps << std::endl;
    }
}

void TriangleDemoApp::renderFrame() {
    // Wait for previous frame
    inFlightFence->wait();
    inFlightFence->reset();

    // Acquire swapchain image
    uint32_t imageIndex = device->acquireNextImage(imageAvailableSemaphore);

    // Record command buffer
    commandBuffer->reset();
    commandBuffer->begin();

    // Begin render pass
    Rect2D renderArea;
    renderArea.offset = {0, 0};
    renderArea.extent = device->getSwapchainExtent();
    std::vector<std::array<float, 4>> clearColors = {{0.1f, 0.1f, 0.15f, 1.0f}};
    commandBuffer->beginRenderPass(renderPass, framebuffers[imageIndex], renderArea, clearColors);

    // Bind pipeline
    commandBuffer->bindPipeline(pipeline);

    // Set dynamic viewport and scissor
    Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(renderArea.extent.width);
    viewport.height = static_cast<float>(renderArea.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer->setViewport(viewport);
    commandBuffer->setScissor(renderArea);

    // Build transform: translate -> rotate -> scale
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, glm::vec3(position, 0.0f));
    transform = glm::rotate(transform, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, glm::vec3(scale));
    commandBuffer->pushConstants(pipelineLayout,
        static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
        0, sizeof(glm::mat4), &transform);

    // Bind vertex and index buffers
    commandBuffer->bindVertexBuffer(0, vertexBuffer);
    commandBuffer->bindIndexBuffer(indexBuffer);

    // Draw
    commandBuffer->drawIndexed(3);

    // End render pass
    commandBuffer->endRenderPass();
    commandBuffer->end();

    // Submit
    device->submit({commandBuffer}, {imageAvailableSemaphore}, {renderFinishedSemaphore}, inFlightFence);

    // Present
    device->present(renderFinishedSemaphore);
}

void TriangleDemoApp::run() {
    std::cout << "Starting main loop..." << std::endl;

    startTime = std::chrono::high_resolution_clock::now();
    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { running = false; break; }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) { running = false; break; }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
            handleInput(event);
        }
        if (!running) break;

        updateFrame();
        renderFrame();
    }

    device->waitIdle();
    std::cout << "Main loop finished" << std::endl;
}

void TriangleDemoApp::shutdown() {
    std::cout << "Shutting down..." << std::endl;
    cleanupVulkan();
    cleanupWindow();
    std::cout << "Total frames rendered: " << frameCount << std::endl;
    std::cout << "Total time: " << totalTime << "s" << std::endl;
}

void TriangleDemoApp::cleanupVulkan() {
    if (device) {
        device->waitIdle();

        // Release all RHI resources before destroying device
        commandBuffer.reset();
        inFlightFence.reset();
        imageAvailableSemaphore.reset();
        renderFinishedSemaphore.reset();
        pipeline.reset();
        pipelineLayout.reset();
        framebuffers.clear();
        renderPass.reset();
        indexBuffer.reset();
        vertexBuffer.reset();
        vertexShader.reset();
        fragmentShader.reset();

        delete device;
        device = nullptr;
    }
}

void TriangleDemoApp::cleanupWindow() {
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    // Auto-configure MoltenVK ICD path (homebrew install). Without this the
    // Vulkan loader can't find the ICD and SDL_CreateWindow(SDL_WINDOW_VULKAN)
    // fails with "Installed Vulkan Portability library doesn't implement
    // VK_KHR_surface". Mirrors the same fix in local_demo / earth_demo /
    // park_demo / disaster_demo / scenario_demo / evacuation_demo.
    if (!std::getenv("VK_ICD_FILENAMES")) {
        const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
        if (std::ifstream(icd).good()) {
            setenv("VK_ICD_FILENAMES", icd, 0);
            setenv("VK_DRIVER_FILES",  icd, 0);
        }
    }
#endif

    std::cout << "XNBY Engine - Triangle Demo" << std::endl;
    std::cout << "==============================" << std::endl;

    TriangleDemoApp app;

    if (!app.initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        app.shutdown();
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
