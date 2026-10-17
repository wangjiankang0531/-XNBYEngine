#pragma once

#include "hongtu/rhi/RHIResources.h"
#include "hongtu/rhi/RHITypes.h"

#include <memory>
#include <vector>

namespace HongTu::RHI {

  // ============================================================================
  // Device Capabilities
  // ============================================================================

  struct DeviceProperties {
    uint32_t apiVersion = 0;
    uint32_t driverVersion = 0;
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    char deviceName[256] = {};
    bool discreteGPU = false;
    bool integratedGPU = false;
    bool virtualGPU = false;
    bool cpuGPU = false;

    // Queue families
    uint32_t graphicsQueueFamilyIndex = 0;
    uint32_t computeQueueFamilyIndex = 0;
    uint32_t transferQueueFamilyIndex = 0;

    // Limits
    uint32_t maxImageDimension2D = 0;
    uint32_t maxImageDimension3D = 0;
    uint32_t maxImageDimensionCube = 0;
    uint32_t maxFramebufferWidth = 0;
    uint32_t maxFramebufferHeight = 0;
    uint32_t maxColorAttachments = 0;
    uint32_t maxSamplerAllocationCount = 0;
    uint64_t maxMemoryAllocationCount = 0;
  };

  /// Driver-reported aggregate memory information for device-local Vulkan heaps.
  /// `budgetAvailable` is true only when the backend can provide the dynamic
  /// budget/usage values exposed by VK_EXT_memory_budget. Heap capacity remains
  /// useful on backends where the dynamic extension is unavailable.
  struct MemoryBudgetInfo {
    bool budgetAvailable = false;
    uint32_t deviceLocalHeapCount = 0;
    uint64_t deviceLocalHeapBytes = 0;
    uint64_t deviceLocalBudgetBytes = 0;
    uint64_t deviceLocalUsageBytes = 0;
    uint64_t allocatorAllocationBytes = 0;
    uint64_t allocatorBlockBytes = 0;
    uint32_t allocatorAllocationCount = 0;
  };

  // Resource objects must remain alive until their GPU submissions finish.
  struct DescriptorAllocationInfo {
    bool available = false;
    uint64_t allocatedSets = 0;
    uint64_t freedSets = 0;
    uint64_t liveSets = 0;
    uint64_t peakLiveSets = 0;
    uint64_t freeFailures = 0;
    uint32_t poolCount = 0;
  };

  struct SwapchainDesc {
    Extent2D extent;
    Format format = Format::R8G8B8A8_SRGB;
    uint32_t imageCount = 3;
    bool vsync = true;
    bool fullscreen = false;
  };

  // ============================================================================
  // RHI Device Interface
  // ============================================================================

  class RHIDevice {
  public:
    virtual ~RHIDevice() = default;

    // Device Information
    virtual const DeviceProperties& getProperties() const = 0;
    virtual void waitIdle() = 0;

    // Memory Management
    virtual uint32_t findMemoryType(uint32_t typeFilter, MemoryPropertyFlags properties) = 0;
    virtual MemoryBudgetInfo getMemoryBudgetInfo() const = 0;
    virtual DescriptorAllocationInfo getDescriptorAllocationInfo() const {
      return {};
    }

    // Buffer Creation
    virtual RHIBufferPtr createBuffer(const BufferDesc& desc) = 0;

    // Texture Creation
    virtual RHITexturePtr createTexture(const TextureDesc& desc) = 0;

    // Shader Creation
    virtual RHIShaderPtr createShader(const ShaderDesc& desc) = 0;

    // Render Pass
    virtual RHIRenderPassPtr createRenderPass(const RenderPassDesc& desc) = 0;

    // Framebuffer
    virtual RHIFramebufferPtr createFramebuffer(const RHIRenderPassPtr& renderPass,
                                                const std::vector<RHITextureViewPtr>& attachments,
                                                const Extent2D& extent,
                                                uint32_t layers = 1) = 0;

    // Sampler
    virtual RHISamplerPtr createSampler(const SamplerDesc& desc) = 0;

    // Descriptor Set Layout
    virtual RHIDescriptorSetLayoutPtr createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;

    // Descriptor Set
    virtual RHIDescriptorSetPtr createDescriptorSet(const RHIDescriptorSetLayoutPtr& layout,
                                                    const DescriptorSetDesc& desc) = 0;

    // Pipeline Layout
    virtual RHIPipelineLayoutPtr createPipelineLayout(const PipelineLayoutDesc& desc) = 0;

    // Pipeline
    virtual RHIPipelinePtr createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual RHIPipelinePtr createComputePipeline(const ComputePipelineDesc& desc) = 0;

    // Command Buffer
    virtual RHICommandBufferPtr createCommandBuffer(bool primary = true) = 0;

    // Synchronization Primitives
    virtual RHISemaphorePtr createSemaphore() = 0;
    virtual RHIFencePtr createFence(bool signaled = false) = 0;
    virtual RHIEventPtr createEvent() = 0;

    // Submission
    virtual void submit(const std::vector<RHICommandBufferPtr>& commandBuffers,
                        const std::vector<RHISemaphorePtr>& waitSemaphores = {},
                        const std::vector<RHISemaphorePtr>& signalSemaphores = {},
                        const RHIFencePtr& fence = nullptr) = 0;

    // Presentation (for swapchain-enabled devices)
    virtual void present(const RHISemaphorePtr& waitSemaphore = nullptr) = 0;
    virtual uint32_t acquireNextImage(const RHISemaphorePtr& semaphore = nullptr,
                                      const RHIFencePtr& fence = nullptr) = 0;
    virtual bool hasSwapchain() const = 0;
    virtual const Extent2D& getSwapchainExtent() const = 0;
    virtual Format getSwapchainFormat() const = 0;
    virtual uint32_t getSwapchainImageCount() const = 0;
    virtual RHITexturePtr getSwapchainImage(uint32_t index) = 0;
    /// Release dependent framebuffers/views first. Returns false if recreation is unsupported or fails.
    virtual bool resizeSwapchain(const Extent2D&) {
      return false;
    }

    /// Synchronous readback of a presented swapchain image into a host-side
    /// RGBA8 buffer (top-left origin, no row padding). Intended for diagnostic
    /// screenshots, not for high-frequency capture — the implementation
    /// allocates a staging buffer per call and blocks on queue completion.
    /// Output is RGBA regardless of swapchain format; the implementation
    /// performs the swizzle. Returns false on failure.
    virtual bool readbackSwapchainImage(uint32_t imageIndex,
                                        std::vector<uint8_t>& out_rgba,
                                        uint32_t& out_width,
                                        uint32_t& out_height) = 0;

    /// In-frame swapchain capture for HIGH-FREQUENCY readback (cloud render).
    ///
    /// `recordSwapchainCapture` records (into a frame command buffer, AFTER
    /// the last pass writing the image and BEFORE submit/present) a copy of
    /// the swapchain image into a persistent host-visible staging buffer.
    /// After the frame's GPU work completes (fence / waitIdle), fetch the
    /// pixels with `readCapturedFrame` — RGBA8, swizzled like
    /// `readbackSwapchainImage`.
    ///
    /// Why not just call readbackSwapchainImage every frame: post-present
    /// readback samples an image owned by the presentation engine — on
    /// MoltenVK this intermittently reads stale/compressed tiles (flickering
    /// black tile artifacts, found via the cloud-render stream). Capturing
    /// inside the frame, pre-present, is race-free by construction.
    virtual bool recordSwapchainCapture(const RHICommandBufferPtr& cmd, uint32_t imageIndex) = 0;
    virtual bool readCapturedFrame(std::vector<uint8_t>& out_rgba, uint32_t& out_width, uint32_t& out_height) = 0;

    /// Synchronous readback of an arbitrary 2D color texture into a host-side
    /// RGBA8 buffer (top-left origin, no row padding). `current_layout` is the
    /// texture's present layout (e.g. ShaderReadOnlyOptimal for a compose
    /// output) — the implementation transitions it to TransferSrc, copies, and
    /// restores the layout. The texture MUST have been created with the
    /// TransferSrc usage bit. Output is RGBA8 (R8G8B8A8_UNORM textures only).
    /// Blocks on queue completion; intended for editor/headless screenshot
    /// paths, not high-frequency capture. Returns false on failure.
    virtual bool readbackTexture(const RHITexturePtr& texture,
                                 ImageLayout current_layout,
                                 std::vector<uint8_t>& out_rgba,
                                 uint32_t& out_width,
                                 uint32_t& out_height) = 0;

    /// Create a swapchain-free offscreen render target with auto-allocated
    /// color (RGBA8 UNORM by default) + depth (D32) attachments. Used by
    /// cloud_server, editor viewport, and headless screenshot pipelines.
    /// Returns nullptr on allocation failure.
    virtual RHIOffscreenTargetPtr createOffscreenTarget(uint32_t width,
                                                        uint32_t height,
                                                        Format format = Format::R8G8B8A8_UNORM) = 0;

    // Queue Operations
    virtual void waitForQueue(uint32_t queueFamily) = 0;

    // Texture Upload
    // Per-mip data descriptor for uploadTextureData.
    // `depth` defaults to 1 for 2D textures; set to > 1 for 3D volume uploads.
    struct MipData {
      uint32_t width;
      uint32_t height;
      uint32_t depth = 1;
      const void* data;
      size_t dataSize;
    };

    // Upload compressed or uncompressed pixel data to an existing texture.
    // Must be called from the main/graphics thread (command pool is not thread-safe).
    // Blocks until the GPU upload completes (calls vkQueueWaitIdle internally).
    virtual void uploadTextureData(const RHITexturePtr& texture,
                                   const std::vector<MipData>& mips,
                                   uint32_t arrayLayer = 0) = 0;
  };

  // ============================================================================
  // Factory Function
  // ============================================================================

  class RHIFactory {
  public:
    virtual ~RHIFactory() = default;

    // Create physical and logical device
    virtual RHIDevice* createDevice(bool enableValidation = true,
                                    const std::vector<const char*>& requiredExtensions = {}) = 0;

    // Create device with swapchain support
    virtual RHIDevice* createDeviceWithSwapchain(void* windowHandle,
                                                 const SwapchainDesc& swapchainDesc,
                                                 bool enableValidation = true,
                                                 const std::vector<const char*>& requiredExtensions = {}) = 0;

    virtual RHIDevice* createDeviceWithWin32Swapchain(void* hinstance,
                                                      void* hwnd,
                                                      const SwapchainDesc& swapchainDesc,
                                                      bool enableValidation = true) {
      (void)hinstance;
      (void)hwnd;
      (void)swapchainDesc;
      (void)enableValidation;
      return nullptr;
    }

    // Destroy device
    virtual void destroyDevice(RHIDevice* device) = 0;
  };

  // ============================================================================
  // RHI Backend Selection
  // ============================================================================

  namespace Backend {

    enum class Type {
      Vulkan,
      DirectX12,
      Metal,
    };

    // Create a factory for the specified backend
    RHIFactory* createFactory(Type type);

    // Destroy factory
    void destroyFactory(RHIFactory* factory);

  } // namespace Backend

} // namespace HongTu::RHI
