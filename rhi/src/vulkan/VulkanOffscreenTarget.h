#pragma once

#include <hongtu/rhi/RHIResources.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace HongTu::RHI::Vulkan {

class VulkanDevice;

/// Vulkan implementation of the abstract `RHIOffscreenTarget`. Owns:
///   - color image + view (R8G8B8A8_UNORM by default, usage
///     ColorAttachment | TransferSrc | Sampled)
///   - depth image + view (D32_SFLOAT, DepthStencilAttachment)
///   - render pass (final color layout = TransferSrcOptimal — ready for
///     readback without an extra barrier)
///   - framebuffer wrapping both views
///
/// All resources are released in the destructor in correct order.
class VulkanOffscreenTarget : public RHIOffscreenTarget {
public:
    static std::shared_ptr<VulkanOffscreenTarget> create(
        VulkanDevice* device,
        uint32_t width, uint32_t height, Format format);

    ~VulkanOffscreenTarget() override;

    RHIRenderPassPtr  getRenderPass() const override { return render_pass_; }
    RHIFramebufferPtr getFramebuffer() const override { return framebuffer_; }
    RHITextureViewPtr getColorView() const override { return color_view_; }
    Format            getFormat() const override { return format_; }
    uint32_t          getWidth() const override { return width_; }
    uint32_t          getHeight() const override { return height_; }

    bool readbackToBuffer(std::vector<uint8_t>& out_rgba,
                          uint32_t& out_w, uint32_t& out_h) override;

private:
    VulkanOffscreenTarget() = default;

    VulkanDevice* device_ = nullptr;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    Format   format_ = Format::R8G8B8A8_UNORM;

    VkImage        color_image_ = VK_NULL_HANDLE;
    VmaAllocation  color_alloc_ = nullptr;

    VkImage        depth_image_ = VK_NULL_HANDLE;
    VmaAllocation  depth_alloc_ = nullptr;

    RHITexturePtr     color_tex_;
    RHITextureViewPtr color_view_;
    RHITexturePtr     depth_tex_;
    RHITextureViewPtr depth_view_;
    RHIRenderPassPtr  render_pass_;
    RHIFramebufferPtr framebuffer_;
};

}  // namespace HongTu::RHI::Vulkan
