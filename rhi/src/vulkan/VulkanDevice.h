#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "hongtu/rhi/RHIDevice.h"

#include <array>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

namespace HongTu::RHI::Vulkan {

  // ============================================================================
  // Vulkan Device Implementation
  // ============================================================================

  class VulkanBuffer : public RHIBuffer {
  public:
    VulkanBuffer(
        VkBuffer buffer, VmaAllocation allocation, VmaAllocator allocator, uint64_t size, BufferUsageFlags usage)
        : buffer(buffer), allocation(allocation), allocator(allocator), size(size), usage(usage) {
    }
    virtual ~VulkanBuffer() {
      if (buffer && allocator) {
        vmaDestroyBuffer(allocator, buffer, allocation);
      }
    }

    VkBuffer getVkBuffer() const {
      return buffer;
    }
    VmaAllocation getAllocation() const {
      return allocation;
    }
    uint64_t getSize() const override {
      return size;
    }
    BufferUsageFlags getUsage() const override {
      return usage;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
    }

    void* map() override;
    void unmap() override;

  private:
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocator allocator = nullptr;
    uint64_t size = 0;
    BufferUsageFlags usage = 0;
    void* mappedData = nullptr;
  };

  class VulkanTexture : public RHITexture {
  public:
    VulkanTexture(VkImage image,
                  VkImageView imageView,
                  VmaAllocation allocation,
                  const Extent3D& extent,
                  Format format,
                  TextureUsageFlags usage,
                  uint32_t mipLevels,
                  uint32_t arrayLayers,
                  VkDevice device,
                  VmaAllocator allocator);
    virtual ~VulkanTexture();

    VkImage getVkImage() const {
      return image;
    }
    VkImageView getVkImageView() const {
      return imageView;
    }
    VmaAllocation getAllocation() const {
      return allocation;
    }

    const Extent3D& getExtent() const override {
      return extent;
    }
    Format getFormat() const override {
      return format;
    }
    TextureUsageFlags getUsage() const override {
      return usage;
    }
    uint32_t getMipLevels() const override {
      return mipLevels;
    }
    uint32_t getArrayLayers() const override {
      return arrayLayers;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image));
    }

    RHITextureViewPtr createView() override;

  private:
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    Extent3D extent;
    Format format = Format::Undefined;
    TextureUsageFlags usage = 0;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
  };

  class VulkanTextureView : public RHITextureView {
  public:
    VulkanTextureView(VkImageView imageView) : imageView(imageView) {
    }
    virtual ~VulkanTextureView() = default;

    VkImageView getVkImageView() const {
      return imageView;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(imageView));
    }

  private:
    VkImageView imageView = VK_NULL_HANDLE;
  };

  class VulkanShader : public RHIShader {
  public:
    VulkanShader(VkShaderModule shaderModule, ShaderStageFlagBits stage) : shaderModule(shaderModule), stage(stage) {
    }
    virtual ~VulkanShader() = default;

    VkShaderModule getVkShaderModule() const {
      return shaderModule;
    }
    ShaderStageFlagBits getStage() const override {
      return stage;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shaderModule));
    }

  private:
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    ShaderStageFlagBits stage = ShaderStageFlagBits::Vertex;
  };

  class VulkanRenderPass : public RHIRenderPass {
  public:
    VulkanRenderPass(VkRenderPass renderPass, uint32_t attachmentCount)
        : renderPass(renderPass), attachmentCount(attachmentCount) {
    }
    virtual ~VulkanRenderPass() = default;

    VkRenderPass getVkRenderPass() const {
      return renderPass;
    }
    uint32_t getAttachmentCount() const override {
      return attachmentCount;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(renderPass));
    }

  private:
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t attachmentCount = 0;
  };

  class VulkanFramebuffer : public RHIFramebuffer {
  public:
    VulkanFramebuffer(VkFramebuffer framebuffer, const Extent2D& extent, uint32_t layerCount)
        : framebuffer(framebuffer), extent(extent), layerCount(layerCount) {
    }
    virtual ~VulkanFramebuffer() = default;

    VkFramebuffer getVkFramebuffer() const {
      return framebuffer;
    }
    const Extent2D& getExtent() const override {
      return extent;
    }
    uint32_t getLayerCount() const override {
      return layerCount;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(framebuffer));
    }

  private:
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    Extent2D extent;
    uint32_t layerCount = 1;
  };

  class VulkanSampler : public RHISampler {
  public:
    VulkanSampler(VkSampler sampler) : sampler(sampler) {
    }
    virtual ~VulkanSampler() = default;

    VkSampler getVkSampler() const {
      return sampler;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sampler));
    }

  private:
    VkSampler sampler = VK_NULL_HANDLE;
  };

  class VulkanPipeline : public RHIPipeline {
  public:
    VulkanPipeline(VkPipeline pipeline, PipelineBindPoint bindPoint) : pipeline(pipeline), bindPoint(bindPoint) {
    }
    virtual ~VulkanPipeline() = default;

    VkPipeline getVkPipeline() const {
      return pipeline;
    }
    PipelineBindPoint getBindPoint() const override {
      return bindPoint;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pipeline));
    }

  private:
    VkPipeline pipeline = VK_NULL_HANDLE;
    PipelineBindPoint bindPoint = PipelineBindPoint::Graphics;
  };

  class VulkanPipelineLayout : public RHIPipelineLayout {
  public:
    VulkanPipelineLayout(VkPipelineLayout layout) : layout(layout) {
    }
    virtual ~VulkanPipelineLayout() = default;

    VkPipelineLayout getVkLayout() const {
      return layout;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(layout));
    }

  private:
    VkPipelineLayout layout = VK_NULL_HANDLE;
  };

  using DescriptorCounts = std::array<uint32_t, 3>; // Uniform, combined sampler, storage buffer.
  constexpr DescriptorCounts kDescriptorPoolCapacities = {1024, 16384, 256};
  constexpr uint32_t kDescriptorPoolSetCapacity = 8192;

  class VulkanDescriptorSetLayout : public RHIDescriptorSetLayout {
  public:
    VulkanDescriptorSetLayout(VkDescriptorSetLayout layout, DescriptorCounts counts) : layout(layout), counts(counts) {
    }
    virtual ~VulkanDescriptorSetLayout() = default;

    VkDescriptorSetLayout getVkLayout() const {
      return layout;
    }
    const DescriptorCounts& getCounts() const {
      return counts;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(layout));
    }

  private:
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    DescriptorCounts counts{};
  };

  // Shared only for synchronized pool lifetime/accounting, not ownership of the
  // Vulkan device. Device teardown invalidates it after waiting for submissions.
  struct DescriptorPoolLifetime {
    mutable std::mutex mutex;
    VkDevice device = VK_NULL_HANDLE;
    DescriptorAllocationInfo info;
    struct Usage {
      uint32_t sets = 0;
      DescriptorCounts descriptors{};
    };
    std::unordered_map<VkDescriptorPool, Usage> usage;
  };

  class VulkanDescriptorSet : public RHIDescriptorSet {
  public:
    VulkanDescriptorSet(VkDescriptorSet descriptorSet,
                        VkDescriptorPool pool,
                        DescriptorCounts counts,
                        std::shared_ptr<DescriptorPoolLifetime> lifetime)
        : descriptorSet(descriptorSet), pool(pool), counts(counts), lifetime(std::move(lifetime)) {
    }
    VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;
    ~VulkanDescriptorSet() override;

    VkDescriptorSet getVkDescriptorSet() const {
      return descriptorSet;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(descriptorSet));
    }

  private:
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    DescriptorCounts counts{};
    std::shared_ptr<DescriptorPoolLifetime> lifetime;
  };

  class VulkanSemaphore : public RHISemaphore {
  public:
    VulkanSemaphore(VkSemaphore semaphore) : semaphore(semaphore) {
    }
    virtual ~VulkanSemaphore() = default;

    VkSemaphore getVkSemaphore() const {
      return semaphore;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(semaphore));
    }

  private:
    VkSemaphore semaphore = VK_NULL_HANDLE;
  };

  class VulkanFence : public RHIFence {
  public:
    VulkanFence(VkFence fence, VkDevice device) : fence(fence), device(device) {
    }
    virtual ~VulkanFence() = default;

    VkFence getVkFence() const {
      return fence;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fence));
    }
    bool isSignaled() const override;
    void wait() override;
    void reset() override;

  private:
    VkFence fence = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
  };

  class VulkanEvent : public RHIEvent {
  public:
    VulkanEvent(VkEvent event, VkDevice device) : event(event), device(device) {
    }
    virtual ~VulkanEvent() = default;

    VkEvent getVkEvent() const {
      return event;
    }
    uint32_t getHandle() const override {
      return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(event));
    }
    bool isSignaled() const override;
    void set() override;
    void reset() override;

  private:
    VkEvent event = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
  };

  class VulkanDevice : public RHIDevice {
  public:
    VulkanDevice() = default;
    virtual ~VulkanDevice();

    // Initialization
    bool initializeVulkan(bool enableValidation = true,
                          const std::vector<const char*>& requiredExtensions = {},
                          void* windowHandle = nullptr,
                          const SwapchainDesc* swapchainDesc = nullptr);
    bool initializeVulkanWin32(bool enableValidation, void* hinstance, void* hwnd, const SwapchainDesc* swapchainDesc);

    // Device Information
    const DeviceProperties& getProperties() const override {
      return properties;
    }
    void waitIdle() override;
    uint32_t findMemoryType(uint32_t typeFilter, MemoryPropertyFlags properties) override;
    MemoryBudgetInfo getMemoryBudgetInfo() const override;
    DescriptorAllocationInfo getDescriptorAllocationInfo() const override;

    // Resource Creation
    RHIBufferPtr createBuffer(const BufferDesc& desc) override;
    RHITexturePtr createTexture(const TextureDesc& desc) override;
    RHIShaderPtr createShader(const ShaderDesc& desc) override;
    RHIRenderPassPtr createRenderPass(const RenderPassDesc& desc) override;
    RHIFramebufferPtr createFramebuffer(const RHIRenderPassPtr& renderPass,
                                        const std::vector<RHITextureViewPtr>& attachments,
                                        const Extent2D& extent,
                                        uint32_t layers = 1) override;
    RHISamplerPtr createSampler(const SamplerDesc& desc) override;
    RHIDescriptorSetLayoutPtr createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    RHIDescriptorSetPtr createDescriptorSet(const RHIDescriptorSetLayoutPtr& layout,
                                            const DescriptorSetDesc& desc) override;
    RHIPipelineLayoutPtr createPipelineLayout(const PipelineLayoutDesc& desc) override;
    RHIPipelinePtr createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    RHIPipelinePtr createComputePipeline(const ComputePipelineDesc& desc) override;
    RHICommandBufferPtr createCommandBuffer(bool primary = true) override;
    RHISemaphorePtr createSemaphore() override;
    RHIFencePtr createFence(bool signaled = false) override;
    RHIEventPtr createEvent() override;

    // Submission
    void submit(const std::vector<RHICommandBufferPtr>& commandBuffers,
                const std::vector<RHISemaphorePtr>& waitSemaphores = {},
                const std::vector<RHISemaphorePtr>& signalSemaphores = {},
                const RHIFencePtr& fence = nullptr) override;

    // Swapchain
    void present(const RHISemaphorePtr& waitSemaphore = nullptr) override;
    uint32_t acquireNextImage(const RHISemaphorePtr& semaphore = nullptr, const RHIFencePtr& fence = nullptr) override;
    bool hasSwapchain() const override {
      return swapchain != VK_NULL_HANDLE;
    }
    const Extent2D& getSwapchainExtent() const override {
      return swapchainExtent;
    }
    Format getSwapchainFormat() const override {
      return swapchainFormat;
    }
    uint32_t getSwapchainImageCount() const override {
      return swapchainImages.size();
    }
    RHITexturePtr getSwapchainImage(uint32_t index) override;
    bool readbackSwapchainImage(uint32_t imageIndex,
                                std::vector<uint8_t>& out_rgba,
                                uint32_t& out_width,
                                uint32_t& out_height) override;
    bool recordSwapchainCapture(const RHICommandBufferPtr& cmd, uint32_t imageIndex) override;
    bool readCapturedFrame(std::vector<uint8_t>& out_rgba, uint32_t& out_width, uint32_t& out_height) override;
    bool readbackTexture(const RHITexturePtr& texture,
                         ImageLayout current_layout,
                         std::vector<uint8_t>& out_rgba,
                         uint32_t& out_width,
                         uint32_t& out_height) override;
    RHIOffscreenTargetPtr createOffscreenTarget(uint32_t width,
                                                uint32_t height,
                                                Format format = Format::R8G8B8A8_UNORM) override;
    bool resizeSwapchain(const Extent2D& newExtent) override;
    void recreateSwapchain(const Extent2D& newExtent);

    // Queue Operations
    void waitForQueue(uint32_t queueFamily) override;

    // Texture Upload
    void uploadTextureData(const RHITexturePtr& texture,
                           const std::vector<MipData>& mips,
                           uint32_t arrayLayer = 0) override;

    // Internal Getters
    VkInstance getVkInstance() const {
      return instance;
    }
    VkPhysicalDevice getVkPhysicalDevice() const {
      return physicalDevice;
    }
    VkDevice getVkDevice() const {
      return device;
    }
    VmaAllocator getAllocator() const {
      return allocator;
    }
    VkQueue getGraphicsQueue() const {
      return graphicsQueue;
    }
    VkQueue getComputeQueue() const {
      return computeQueue;
    }
    VkQueue getTransferQueue() const {
      return transferQueue;
    }
    VkCommandPool getCommandPool() const {
      return commandPool;
    }

  private:
    // Vulkan Core Objects
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Queues
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Swapchain
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    Extent2D swapchainExtent;
    Format swapchainFormat = Format::Undefined;
    VkFormat swapchainVkFormat = VK_FORMAT_UNDEFINED; // raw — needed for readback swizzle

    // In-frame capture staging (recordSwapchainCapture / readCapturedFrame).
    VkBuffer captureBuffer = VK_NULL_HANDLE;
    VmaAllocation captureAllocation = nullptr;
    void* captureMapped = nullptr;
    VkDeviceSize captureSize = 0;
    Extent2D captureExtent{};
    std::vector<RHITexturePtr> swapchainImages;
    uint32_t currentSwapchainImageIndex = 0;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool vsyncEnabled = true;

    // Descriptor Pool
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::shared_ptr<DescriptorPoolLifetime> descriptorLifetime = std::make_shared<DescriptorPoolLifetime>();
    // 池链:主池耗尽时按需开新池(大场景 5k+ 材质级别)。析构统一销毁。
    std::vector<VkDescriptorPool> overflowPools;
    VkDescriptorPool allocateOverflowPool();

    // Device Properties
    DeviceProperties properties;
    VkPhysicalDeviceProperties vkProperties;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    bool memoryBudgetSupported = false;

    // Initialization Helpers
    bool createInstance(bool enableValidation, const std::vector<const char*>& requiredExtensions);
    void setupDebugMessenger();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    bool createDescriptorPool();
    bool createSurface(void* windowHandle);
    bool createWin32Surface(void* hinstance, void* hwnd);
    bool createSwapchain(const SwapchainDesc& desc);
    void cleanupSwapchain();
    void updateDeviceProperties();

    // Conversion Helpers
    VkFormat toVkFormat(Format format) const;
    Format fromVkFormat(VkFormat format) const;
    VkImageUsageFlags toVkImageUsage(TextureUsageFlags usage) const;
    VkBufferUsageFlags toVkBufferUsage(BufferUsageFlags usage) const;
    VkShaderStageFlagBits toVkShaderStage(ShaderStageFlagBits stage) const;
    VkPrimitiveTopology toVkPrimitiveTopology(PrimitiveTopology topology) const;
    VkCompareOp toVkCompareOp(CompareOp op) const;
    VkBlendFactor toVkBlendFactor(BlendFactor factor) const;
    VkBlendOp toVkBlendOp(BlendOp op) const;
    VkCullModeFlagBits toVkCullMode(CullMode mode) const;
    VkPolygonMode toVkFillMode(FillMode mode) const;
    VkImageLayout toVkImageLayout(ImageLayout layout) const;
    VkAccessFlags toVkAccessFlags(AccessFlags flags) const;
    VkPipelineStageFlags toVkPipelineStageFlags(PipelineStageFlags flags) const;
    VkSamplerAddressMode toVkAddressMode(SamplerAddressMode mode) const;
    VkFilter toVkFilter(SamplerFilter filter) const;
    VkSamplerMipmapMode toVkMipmapMode(SamplerMipmapMode mode) const;
    VkMemoryPropertyFlags toVkMemoryPropertyFlags(MemoryPropertyFlags flags) const;
  };

  class VulkanFactory : public RHIFactory {
  public:
    RHIDevice* createDevice(bool enableValidation = true,
                            const std::vector<const char*>& requiredExtensions = {}) override;

    RHIDevice* createDeviceWithSwapchain(void* windowHandle,
                                         const SwapchainDesc& swapchainDesc,
                                         bool enableValidation = true,
                                         const std::vector<const char*>& requiredExtensions = {}) override;

    RHIDevice* createDeviceWithWin32Swapchain(void* hinstance,
                                              void* hwnd,
                                              const SwapchainDesc& swapchainDesc,
                                              bool enableValidation = true) override;

    void destroyDevice(RHIDevice* device) override;
  };

} // namespace HongTu::RHI::Vulkan
