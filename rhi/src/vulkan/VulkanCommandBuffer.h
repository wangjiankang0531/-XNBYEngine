#pragma once

#include "hongtu/rhi/RHIResources.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace HongTu::RHI::Vulkan {

class VulkanCommandBuffer : public RHICommandBuffer {
public:
    VulkanCommandBuffer(VkCommandBuffer cmdBuffer, VkDevice device)
        : cmdBuffer(cmdBuffer), device(device) {}

    virtual ~VulkanCommandBuffer() = default;

    // Recording
    void begin() override;
    void end() override;
    void reset() override;

    // Render Pass
    void beginRenderPass(
        const RHIRenderPassPtr& renderPass,
        const RHIFramebufferPtr& framebuffer,
        const Rect2D& renderArea,
        const std::vector<std::array<float, 4>>& clearColors,
        float clearDepth = 1.0f,
        uint32_t clearStencil = 0
    ) override;
    void endRenderPass() override;

    // Pipeline
    void bindPipeline(const RHIPipelinePtr& pipeline) override;
    void bindDescriptorSet(
        uint32_t set,
        const RHIDescriptorSetPtr& descriptorSet,
        const RHIPipelineLayoutPtr& pipelineLayout,
        PipelineBindPoint bindPoint = PipelineBindPoint::Graphics
    ) override;

    // Vertex Input
    void bindVertexBuffer(uint32_t binding, const RHIBufferPtr& buffer, uint64_t offset = 0) override;
    void bindIndexBuffer(const RHIBufferPtr& buffer, uint64_t offset = 0, Format indexType = Format::R32_UINT) override;

    // Drawing
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount = 1,
        uint32_t firstIndex = 0,
        int32_t vertexOffset = 0,
        uint32_t firstInstance = 0
    ) override;
    void drawIndirect(const RHIBufferPtr& buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) override;

    // Compute
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    // Push Constants
    void pushConstants(
        const RHIPipelineLayoutPtr& layout,
        ShaderStageFlags stageFlags,
        uint32_t offset,
        uint32_t size,
        const void* data
    ) override;

    // Dynamic State
    void setViewport(const Viewport& viewport) override;
    void setViewports(const std::vector<Viewport>& viewports) override;
    void setScissor(const Rect2D& scissor) override;
    void setScissors(const std::vector<Rect2D>& scissors) override;
    void setLineWidth(float lineWidth) override;
    void setDepthBias(float constantFactor, float clamp, float slopeFactor) override;
    void setBlendConstants(const std::array<float, 4>& constants) override;
    void setDepthBounds(float minDepthBounds, float maxDepthBounds) override;

    // Resource Transfers
    void copyBuffer(
        const RHIBufferPtr& srcBuffer,
        const RHIBufferPtr& dstBuffer,
        uint64_t srcOffset,
        uint64_t dstOffset,
        uint64_t size
    ) override;

    void copyTexture(
        const RHITexturePtr& srcTexture,
        const RHITexturePtr& dstTexture,
        uint32_t srcMipLevel = 0,
        uint32_t dstMipLevel = 0
    ) override;

    void copyBufferToTexture(
        const RHIBufferPtr& srcBuffer,
        const RHITexturePtr& dstTexture,
        uint64_t bufferOffset,
        const Offset3D& imageOffset,
        const Extent3D& imageExtent,
        uint32_t mipLevel = 0,
        uint32_t arrayLayer = 0
    ) override;

    // Synchronization
    void pipelineBarrier(
        PipelineStageFlags srcStageMask,
        PipelineStageFlags dstStageMask,
        const std::vector<ImageMemoryBarrier>& imageBarriers = {},
        const std::vector<BufferMemoryBarrier>& bufferBarriers = {}
    ) override;

    void setEvent(const RHIEventPtr& event, PipelineStageFlags stageFlags) override;
    void waitEvents(
        const std::vector<RHIEventPtr>& events,
        PipelineStageFlags srcStageMask,
        PipelineStageFlags dstStageMask
    ) override;

    // Queries
    void resetQueryPool(uint32_t firstQuery, uint32_t queryCount) override;
    void writeTimestamp(PipelineStageFlags pipelineStage, uint32_t queryPool, uint32_t query) override;

    // Accessors
    VkCommandBuffer getVkCommandBuffer() const { return cmdBuffer; }
    uint32_t getHandle() const override { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cmdBuffer)); }

private:
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
};

} // namespace HongTu::RHI::Vulkan
