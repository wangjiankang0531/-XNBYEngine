#include "VulkanCommandBuffer.h"
#include "VulkanDevice.h"
#include <iostream>

namespace HongTu::RHI::Vulkan {

void VulkanCommandBuffer::begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
}

void VulkanCommandBuffer::end() {
    vkEndCommandBuffer(cmdBuffer);
}

void VulkanCommandBuffer::reset() {
    vkResetCommandBuffer(cmdBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
}

void VulkanCommandBuffer::beginRenderPass(
    const RHIRenderPassPtr& renderPass,
    const RHIFramebufferPtr& framebuffer,
    const Rect2D& renderArea,
    const std::vector<std::array<float, 4>>& clearColors,
    float clearDepth,
    uint32_t clearStencil
) {
    auto vkRenderPass = std::static_pointer_cast<VulkanRenderPass>(renderPass);
    auto vkFramebuffer = std::static_pointer_cast<VulkanFramebuffer>(framebuffer);

    std::vector<VkClearValue> clearValues;
    for (const auto& color : clearColors) {
        VkClearValue clearValue{};
        clearValue.color = {color[0], color[1], color[2], color[3]};
        clearValues.push_back(clearValue);
    }

    VkClearValue depthClear{};
    depthClear.depthStencil = {clearDepth, clearStencil};
    if (clearColors.size() < 2) {
        clearValues.push_back(depthClear);
    }

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = vkRenderPass->getVkRenderPass();
    renderPassBeginInfo.framebuffer = vkFramebuffer->getVkFramebuffer();
    renderPassBeginInfo.renderArea.offset = {renderArea.offset.x, renderArea.offset.y};
    renderPassBeginInfo.renderArea.extent = {renderArea.extent.width, renderArea.extent.height};
    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(cmdBuffer);
}

void VulkanCommandBuffer::bindPipeline(const RHIPipelinePtr& pipeline) {
    auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(pipeline);
    VkPipelineBindPoint bindPoint = vkPipeline->getBindPoint() == PipelineBindPoint::Graphics
        ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    vkCmdBindPipeline(cmdBuffer, bindPoint, vkPipeline->getVkPipeline());
}

void VulkanCommandBuffer::bindDescriptorSet(
    uint32_t set,
    const RHIDescriptorSetPtr& descriptorSet,
    const RHIPipelineLayoutPtr& pipelineLayout,
    PipelineBindPoint bindPoint
) {
    if (!descriptorSet) {
        // 空集绑定:上游材质创建失败未被拦住。跳过并打日志,比把垃圾
        // 句柄交给 MoltenVK(encode 段错误)好得多。
        static int warn_count = 0;
        if (warn_count++ < 8) {
            std::cerr << "bindDescriptorSet: null set at slot " << set
                      << " — skipped" << std::endl;
        }
        return;
    }

    if (!descriptorSet || !pipelineLayout) {
        std::cerr << "VulkanCommandBuffer::bindDescriptorSet: null descriptorSet or pipelineLayout" << std::endl;
        return;
    }
    auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
    auto vkLayout = std::static_pointer_cast<VulkanPipelineLayout>(pipelineLayout);
    VkPipelineBindPoint vkBindPoint = bindPoint == PipelineBindPoint::Graphics
        ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    VkDescriptorSet vkDescSet = vkSet->getVkDescriptorSet();
    vkCmdBindDescriptorSets(cmdBuffer, vkBindPoint, vkLayout->getVkLayout(), set, 1, &vkDescSet, 0, nullptr);
}

void VulkanCommandBuffer::bindVertexBuffer(uint32_t binding, const RHIBufferPtr& buffer, uint64_t offset) {
    auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
    VkBuffer vkBuf = vkBuffer->getVkBuffer();
    vkCmdBindVertexBuffers(cmdBuffer, binding, 1, &vkBuf, &offset);
}

void VulkanCommandBuffer::bindIndexBuffer(const RHIBufferPtr& buffer, uint64_t offset, Format indexType) {
    auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
    VkIndexType vkIndexType = indexType == Format::R16_UINT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmdBuffer, vkBuffer->getVkBuffer(), offset, vkIndexType);
}

void VulkanCommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::drawIndexed(
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    int32_t vertexOffset,
    uint32_t firstInstance
) {
    vkCmdDrawIndexed(cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::drawIndirect(const RHIBufferPtr& buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
    auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
    vkCmdDrawIndirect(cmdBuffer, vkBuffer->getVkBuffer(), offset, drawCount, stride);
}

void VulkanCommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandBuffer::pushConstants(
    const RHIPipelineLayoutPtr& layout,
    ShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* data
) {
    auto vkLayout = std::static_pointer_cast<VulkanPipelineLayout>(layout);
    VkShaderStageFlags vkStageFlags = 0;
    if (stageFlags & ShaderStageFlagBits::Vertex) vkStageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stageFlags & ShaderStageFlagBits::Fragment) vkStageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stageFlags & ShaderStageFlagBits::Compute) vkStageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;

    vkCmdPushConstants(cmdBuffer, vkLayout->getVkLayout(), vkStageFlags, offset, size, data);
}

void VulkanCommandBuffer::setViewport(const Viewport& viewport) {
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;
    vkCmdSetViewport(cmdBuffer, 0, 1, &vkViewport);
}

void VulkanCommandBuffer::setViewports(const std::vector<Viewport>& viewports) {
    std::vector<VkViewport> vkViewports;
    for (const auto& vp : viewports) {
        VkViewport vkVp{};
        vkVp.x = vp.x;
        vkVp.y = vp.y;
        vkVp.width = vp.width;
        vkVp.height = vp.height;
        vkVp.minDepth = vp.minDepth;
        vkVp.maxDepth = vp.maxDepth;
        vkViewports.push_back(vkVp);
    }
    vkCmdSetViewport(cmdBuffer, 0, vkViewports.size(), vkViewports.data());
}

void VulkanCommandBuffer::setScissor(const Rect2D& scissor) {
    VkRect2D vkScissor{};
    vkScissor.offset = {scissor.offset.x, scissor.offset.y};
    vkScissor.extent = {scissor.extent.width, scissor.extent.height};
    vkCmdSetScissor(cmdBuffer, 0, 1, &vkScissor);
}

void VulkanCommandBuffer::setScissors(const std::vector<Rect2D>& scissors) {
    std::vector<VkRect2D> vkScissors;
    for (const auto& sc : scissors) {
        VkRect2D vkSc{};
        vkSc.offset = {sc.offset.x, sc.offset.y};
        vkSc.extent = {sc.extent.width, sc.extent.height};
        vkScissors.push_back(vkSc);
    }
    vkCmdSetScissor(cmdBuffer, 0, vkScissors.size(), vkScissors.data());
}

void VulkanCommandBuffer::setLineWidth(float lineWidth) {
    vkCmdSetLineWidth(cmdBuffer, lineWidth);
}

void VulkanCommandBuffer::setDepthBias(float constantFactor, float clamp, float slopeFactor) {
    vkCmdSetDepthBias(cmdBuffer, constantFactor, clamp, slopeFactor);
}

void VulkanCommandBuffer::setBlendConstants(const std::array<float, 4>& constants) {
    vkCmdSetBlendConstants(cmdBuffer, constants.data());
}

void VulkanCommandBuffer::setDepthBounds(float minDepthBounds, float maxDepthBounds) {
    vkCmdSetDepthBounds(cmdBuffer, minDepthBounds, maxDepthBounds);
}

void VulkanCommandBuffer::copyBuffer(
    const RHIBufferPtr& srcBuffer,
    const RHIBufferPtr& dstBuffer,
    uint64_t srcOffset,
    uint64_t dstOffset,
    uint64_t size
) {
    auto vkSrcBuffer = std::static_pointer_cast<VulkanBuffer>(srcBuffer);
    auto vkDstBuffer = std::static_pointer_cast<VulkanBuffer>(dstBuffer);

    VkBufferCopy region{};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = size;

    vkCmdCopyBuffer(cmdBuffer, vkSrcBuffer->getVkBuffer(), vkDstBuffer->getVkBuffer(), 1, &region);
}

void VulkanCommandBuffer::copyTexture(
    const RHITexturePtr& srcTexture,
    const RHITexturePtr& dstTexture,
    uint32_t srcMipLevel,
    uint32_t dstMipLevel
) {
    auto vkSrcTexture = std::static_pointer_cast<VulkanTexture>(srcTexture);
    auto vkDstTexture = std::static_pointer_cast<VulkanTexture>(dstTexture);

    const auto& extent = vkSrcTexture->getExtent();
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, srcMipLevel, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, dstMipLevel, 0, 1};
    region.extent = {extent.width, extent.height, extent.depth};

    vkCmdCopyImage(cmdBuffer, vkSrcTexture->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vkDstTexture->getVkImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandBuffer::copyBufferToTexture(
    const RHIBufferPtr& srcBuffer,
    const RHITexturePtr& dstTexture,
    uint64_t bufferOffset,
    const Offset3D& imageOffset,
    const Extent3D& imageExtent,
    uint32_t mipLevel,
    uint32_t arrayLayer
) {
    auto vkSrcBuffer = std::static_pointer_cast<VulkanBuffer>(srcBuffer);
    auto vkDstTexture = std::static_pointer_cast<VulkanTexture>(dstTexture);

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, arrayLayer, 1};
    region.imageOffset = {imageOffset.x, imageOffset.y, imageOffset.z};
    region.imageExtent = {imageExtent.width, imageExtent.height, imageExtent.depth};

    vkCmdCopyBufferToImage(cmdBuffer, vkSrcBuffer->getVkBuffer(), vkDstTexture->getVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandBuffer::pipelineBarrier(
    PipelineStageFlags srcStageMask,
    PipelineStageFlags dstStageMask,
    const std::vector<ImageMemoryBarrier>& imageBarriers,
    const std::vector<BufferMemoryBarrier>& bufferBarriers
) {
    // 转 buffer memory barriers — compute → vertex / fragment 路径常用
    std::vector<VkBufferMemoryBarrier> vkBufBarriers;
    vkBufBarriers.reserve(bufferBarriers.size());
    for (const auto& b : bufferBarriers) {
        VkBufferMemoryBarrier bb{};
        bb.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bb.srcAccessMask       = static_cast<VkAccessFlags>(b.srcAccessMask);
        bb.dstAccessMask       = static_cast<VkAccessFlags>(b.dstAccessMask);
        bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        if (b.buffer) {
            auto vkBuf = std::static_pointer_cast<VulkanBuffer>(b.buffer);
            bb.buffer = vkBuf->getVkBuffer();
        }
        bb.offset = b.offset;
        bb.size   = b.size == 0 ? VK_WHOLE_SIZE : b.size;
        vkBufBarriers.push_back(bb);
    }
    // Image memory barriers — translate ImageMemoryBarrier{texture,...}. The
    // texture must be supplied via the `texture` smart-ptr field (the legacy
    // uint32 `image` handle path is not resolvable here).
    auto toVkLayout = [](ImageLayout l) -> VkImageLayout {
        switch (l) {
            case ImageLayout::Undefined:                     return VK_IMAGE_LAYOUT_UNDEFINED;
            case ImageLayout::General:                       return VK_IMAGE_LAYOUT_GENERAL;
            case ImageLayout::ColorAttachmentOptimal:        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case ImageLayout::DepthStencilAttachmentOptimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case ImageLayout::DepthStencilReadOnlyOptimal:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case ImageLayout::ShaderReadOnlyOptimal:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case ImageLayout::TransferSrcOptimal:            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case ImageLayout::TransferDstOptimal:            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case ImageLayout::PresentSrc:                    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            default:                                         return VK_IMAGE_LAYOUT_GENERAL;
        }
    };
    std::vector<VkImageMemoryBarrier> vkImgBarriers;
    vkImgBarriers.reserve(imageBarriers.size());
    for (const auto& b : imageBarriers) {
        if (!b.texture) continue;  // legacy handle path unsupported
        auto vkTex = std::static_pointer_cast<VulkanTexture>(b.texture);
        VkImageMemoryBarrier ib{};
        ib.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ib.srcAccessMask       = static_cast<VkAccessFlags>(b.srcAccessMask);
        ib.dstAccessMask       = static_cast<VkAccessFlags>(b.dstAccessMask);
        ib.oldLayout           = toVkLayout(b.oldLayout);
        ib.newLayout           = toVkLayout(b.newLayout);
        ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image               = vkTex->getVkImage();
        ib.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ib.subresourceRange.baseMipLevel   = b.baseMipLevel;
        ib.subresourceRange.levelCount     = b.levelCount;
        ib.subresourceRange.baseArrayLayer = b.baseArrayLayer;
        ib.subresourceRange.layerCount     = b.layerCount;
        vkImgBarriers.push_back(ib);
    }
    vkCmdPipelineBarrier(cmdBuffer,
        static_cast<VkPipelineStageFlags>(srcStageMask),
        static_cast<VkPipelineStageFlags>(dstStageMask),
        0,
        0, nullptr,
        static_cast<uint32_t>(vkBufBarriers.size()),
        vkBufBarriers.empty() ? nullptr : vkBufBarriers.data(),
        static_cast<uint32_t>(vkImgBarriers.size()),
        vkImgBarriers.empty() ? nullptr : vkImgBarriers.data());
}

void VulkanCommandBuffer::setEvent(const RHIEventPtr& event, PipelineStageFlags stageFlags) {
    auto vkEvent = std::static_pointer_cast<VulkanEvent>(event);
    vkCmdSetEvent(cmdBuffer, vkEvent->getVkEvent(), static_cast<VkPipelineStageFlags>(stageFlags));
}

void VulkanCommandBuffer::waitEvents(
    const std::vector<RHIEventPtr>& events,
    PipelineStageFlags srcStageMask,
    PipelineStageFlags dstStageMask
) {
    std::vector<VkEvent> vkEvents;
    for (const auto& event : events) {
        auto vkEvent = std::static_pointer_cast<VulkanEvent>(event);
        vkEvents.push_back(vkEvent->getVkEvent());
    }

    vkCmdWaitEvents(cmdBuffer, vkEvents.size(), vkEvents.data(),
                    static_cast<VkPipelineStageFlags>(srcStageMask),
                    static_cast<VkPipelineStageFlags>(dstStageMask), 0, nullptr, 0, nullptr, 0, nullptr);
}

void VulkanCommandBuffer::resetQueryPool(uint32_t firstQuery, uint32_t queryCount) {
    // Implementation deferred
}

void VulkanCommandBuffer::writeTimestamp(PipelineStageFlags pipelineStage, uint32_t queryPool, uint32_t query) {
    // Implementation deferred
}

} // namespace HongTu::RHI::Vulkan
