#pragma once

#include "hongtu/rhi/RHITypes.h"
#include <memory>
#include <vector>

namespace HongTu::RHI {

// ============================================================================
// Forward Declarations
// ============================================================================

class RHIBuffer;
class RHITexture;
class RHITextureView;
class RHIShader;
class RHIPipeline;
class RHIRenderPass;
class RHIFramebuffer;
class RHICommandBuffer;
class RHISampler;
class RHIDescriptorSet;
class RHIDescriptorSetLayout;
class RHIPipelineLayout;
class RHISemaphore;
class RHIFence;
class RHIEvent;
class RHIOffscreenTarget;

// ============================================================================
// Shared Pointer Type Aliases
// ============================================================================

using RHIBufferPtr = std::shared_ptr<RHIBuffer>;
using RHITexturePtr = std::shared_ptr<RHITexture>;
using RHITextureViewPtr = std::shared_ptr<RHITextureView>;
using RHIShaderPtr = std::shared_ptr<RHIShader>;
using RHIPipelinePtr = std::shared_ptr<RHIPipeline>;
using RHIRenderPassPtr = std::shared_ptr<RHIRenderPass>;
using RHIFramebufferPtr = std::shared_ptr<RHIFramebuffer>;
using RHICommandBufferPtr = std::shared_ptr<RHICommandBuffer>;
using RHISamplerPtr = std::shared_ptr<RHISampler>;
using RHIDescriptorSetPtr = std::shared_ptr<RHIDescriptorSet>;
using RHIDescriptorSetLayoutPtr = std::shared_ptr<RHIDescriptorSetLayout>;
using RHIPipelineLayoutPtr = std::shared_ptr<RHIPipelineLayout>;
using RHISemaphorePtr = std::shared_ptr<RHISemaphore>;
using RHIFencePtr = std::shared_ptr<RHIFence>;
using RHIEventPtr = std::shared_ptr<RHIEvent>;
using RHIOffscreenTargetPtr = std::shared_ptr<RHIOffscreenTarget>;

// ============================================================================
// Base Resource Classes
// ============================================================================

class RHIResource {
public:
    virtual ~RHIResource() = default;
    virtual uint32_t getHandle() const = 0;
};

class RHIBuffer : public RHIResource {
public:
    virtual ~RHIBuffer() = default;
    virtual uint64_t getSize() const = 0;
    virtual BufferUsageFlags getUsage() const = 0;
    virtual void* map() = 0;
    virtual void unmap() = 0;
};

class RHITextureView : public RHIResource {
public:
    virtual ~RHITextureView() = default;
};

class RHITexture : public RHIResource {
public:
    virtual ~RHITexture() = default;
    virtual const Extent3D& getExtent() const = 0;
    virtual Format getFormat() const = 0;
    virtual TextureUsageFlags getUsage() const = 0;
    virtual uint32_t getMipLevels() const = 0;
    virtual uint32_t getArrayLayers() const = 0;
    virtual RHITextureViewPtr createView() = 0;
};

class RHIShader : public RHIResource {
public:
    virtual ~RHIShader() = default;
    virtual ShaderStageFlagBits getStage() const = 0;
};

class RHIPipeline : public RHIResource {
public:
    virtual ~RHIPipeline() = default;
    virtual PipelineBindPoint getBindPoint() const = 0;
};

class RHIRenderPass : public RHIResource {
public:
    virtual ~RHIRenderPass() = default;
    virtual uint32_t getAttachmentCount() const = 0;
};

class RHIFramebuffer : public RHIResource {
public:
    virtual ~RHIFramebuffer() = default;
    virtual const Extent2D& getExtent() const = 0;
    virtual uint32_t getLayerCount() const = 0;
};

class RHISampler : public RHIResource {
public:
    virtual ~RHISampler() = default;
};

class RHIDescriptorSet : public RHIResource {
public:
    virtual ~RHIDescriptorSet() = default;
};

class RHIDescriptorSetLayout : public RHIResource {
public:
    virtual ~RHIDescriptorSetLayout() = default;
};

class RHIPipelineLayout : public RHIResource {
public:
    virtual ~RHIPipelineLayout() = default;
};

class RHISemaphore : public RHIResource {
public:
    virtual ~RHISemaphore() = default;
};

class RHIFence : public RHIResource {
public:
    virtual ~RHIFence() = default;
    virtual bool isSignaled() const = 0;
    virtual void wait() = 0;
    virtual void reset() = 0;
};

class RHIEvent : public RHIResource {
public:
    virtual ~RHIEvent() = default;
    virtual bool isSignaled() const = 0;
    virtual void set() = 0;
    virtual void reset() = 0;
};

// ============================================================================
// Command Buffer Resource
// ============================================================================

class RHICommandBuffer : public RHIResource {
public:
    virtual ~RHICommandBuffer() = default;

    // Recording
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;

    // Render Pass
    virtual void beginRenderPass(
        const RHIRenderPassPtr& renderPass,
        const RHIFramebufferPtr& framebuffer,
        const Rect2D& renderArea,
        const std::vector<std::array<float, 4>>& clearColors,
        float clearDepth = 1.0f,
        uint32_t clearStencil = 0
    ) = 0;
    virtual void endRenderPass() = 0;

    // Pipeline
    virtual void bindPipeline(const RHIPipelinePtr& pipeline) = 0;
    virtual void bindDescriptorSet(
        uint32_t set,
        const RHIDescriptorSetPtr& descriptorSet,
        const RHIPipelineLayoutPtr& pipelineLayout,
        PipelineBindPoint bindPoint = PipelineBindPoint::Graphics
    ) = 0;

    // Vertex Input
    virtual void bindVertexBuffer(uint32_t binding, const RHIBufferPtr& buffer, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(const RHIBufferPtr& buffer, uint64_t offset = 0, Format indexType = Format::R32_UINT) = 0;

    // Drawing
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount = 1,
        uint32_t firstIndex = 0,
        int32_t vertexOffset = 0,
        uint32_t firstInstance = 0
    ) = 0;
    virtual void drawIndirect(const RHIBufferPtr& buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;

    // Compute
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    // Push Constants
    virtual void pushConstants(
        const RHIPipelineLayoutPtr& layout,
        ShaderStageFlags stageFlags,
        uint32_t offset,
        uint32_t size,
        const void* data
    ) = 0;

    // Dynamic State
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setViewports(const std::vector<Viewport>& viewports) = 0;
    virtual void setScissor(const Rect2D& scissor) = 0;
    virtual void setScissors(const std::vector<Rect2D>& scissors) = 0;
    virtual void setLineWidth(float lineWidth) = 0;
    virtual void setDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;
    virtual void setBlendConstants(const std::array<float, 4>& constants) = 0;
    virtual void setDepthBounds(float minDepthBounds, float maxDepthBounds) = 0;

    // Resource Transfers
    virtual void copyBuffer(
        const RHIBufferPtr& srcBuffer,
        const RHIBufferPtr& dstBuffer,
        uint64_t srcOffset,
        uint64_t dstOffset,
        uint64_t size
    ) = 0;

    virtual void copyTexture(
        const RHITexturePtr& srcTexture,
        const RHITexturePtr& dstTexture,
        uint32_t srcMipLevel = 0,
        uint32_t dstMipLevel = 0
    ) = 0;

    virtual void copyBufferToTexture(
        const RHIBufferPtr& srcBuffer,
        const RHITexturePtr& dstTexture,
        uint64_t bufferOffset,
        const Offset3D& imageOffset,
        const Extent3D& imageExtent,
        uint32_t mipLevel = 0,
        uint32_t arrayLayer = 0
    ) = 0;

    // Synchronization
    virtual void pipelineBarrier(
        PipelineStageFlags srcStageMask,
        PipelineStageFlags dstStageMask,
        const std::vector<ImageMemoryBarrier>& imageBarriers = {},
        const std::vector<BufferMemoryBarrier>& bufferBarriers = {}
    ) = 0;

    virtual void setEvent(const RHIEventPtr& event, PipelineStageFlags stageFlags) = 0;
    virtual void waitEvents(
        const std::vector<RHIEventPtr>& events,
        PipelineStageFlags srcStageMask,
        PipelineStageFlags dstStageMask
    ) = 0;

    // Queries
    virtual void resetQueryPool(uint32_t firstQuery, uint32_t queryCount) = 0;
    virtual void writeTimestamp(PipelineStageFlags pipelineStage, uint32_t queryPool, uint32_t query) = 0;
};

// ============================================================================
// Offscreen Render Target
//
// Bundles color + depth attachments, a render pass, and a framebuffer into
// a swapchain-free target. Used by cloud_server (stream offscreen render →
// video encoder) and the editor viewport (render → QImage). Add to a
// command buffer with `cmd->beginRenderPass(target->getRenderPass(),
// target->getFramebuffer(), ...)`.
// ============================================================================

class RHIOffscreenTarget {
public:
    virtual ~RHIOffscreenTarget() = default;

    virtual RHIRenderPassPtr  getRenderPass() const = 0;
    virtual RHIFramebufferPtr getFramebuffer() const = 0;
    virtual RHITextureViewPtr getColorView() const = 0;
    virtual Format            getFormat() const = 0;
    virtual uint32_t          getWidth() const = 0;
    virtual uint32_t          getHeight() const = 0;

    /// Copy current color attachment contents to host-visible bytes as RGBA8.
    /// Caller must have completed the rendering pipeline and submitted /
    /// waited the command buffer before calling. This blocks on a one-shot
    /// internal command + fence (not for high-frequency use; pair with
    /// double-buffered staging in the encoder).
    virtual bool readbackToBuffer(std::vector<uint8_t>& out_rgba,
                                  uint32_t& out_w, uint32_t& out_h) = 0;
};

} // namespace HongTu::RHI
