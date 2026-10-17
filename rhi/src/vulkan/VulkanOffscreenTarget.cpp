#include "VulkanOffscreenTarget.h"
#include "VulkanDevice.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace HongTu::RHI::Vulkan {

namespace {

VkFormat toVk(Format f) {
  switch (f) {
    case Format::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
    default:                     return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

}  // namespace

std::shared_ptr<VulkanOffscreenTarget>
VulkanOffscreenTarget::create(VulkanDevice* device,
                               uint32_t width, uint32_t height, Format format) {
  if (!device || width == 0 || height == 0) return nullptr;
  auto self = std::shared_ptr<VulkanOffscreenTarget>(new VulkanOffscreenTarget());
  self->device_ = device;
  self->width_  = width;
  self->height_ = height;
  self->format_ = format;

  VkDevice vkdev = device->getVkDevice();
  VmaAllocator alloc = device->getAllocator();

  // ---------------------------------------------------------------------------
  // Color attachment image
  // ---------------------------------------------------------------------------
  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = toVk(format);
    ci.extent        = {width, height, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                     | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                     | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(alloc, &ci, &ai, &self->color_image_,
                        &self->color_alloc_, nullptr) != VK_SUCCESS) {
      spdlog::error("VulkanOffscreenTarget: color image alloc failed");
      return nullptr;
    }
  }
  VkImageView color_view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = self->color_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = toVk(format);
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &color_view) != VK_SUCCESS) {
      spdlog::error("VulkanOffscreenTarget: color view create failed");
      return nullptr;
    }
  }
  self->color_view_ = std::make_shared<VulkanTextureView>(color_view);

  // ---------------------------------------------------------------------------
  // Depth attachment image
  // ---------------------------------------------------------------------------
  {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_D32_SFLOAT;
    ci.extent        = {width, height, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(alloc, &ci, &ai, &self->depth_image_,
                        &self->depth_alloc_, nullptr) != VK_SUCCESS) {
      spdlog::error("VulkanOffscreenTarget: depth image alloc failed");
      return nullptr;
    }
  }
  VkImageView depth_view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = self->depth_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vkdev, &vi, nullptr, &depth_view) != VK_SUCCESS) {
      spdlog::error("VulkanOffscreenTarget: depth view create failed");
      return nullptr;
    }
  }
  self->depth_view_ = std::make_shared<VulkanTextureView>(depth_view);

  // ---------------------------------------------------------------------------
  // Render pass — color + depth, final layout TransferSrcOptimal for readback
  // ---------------------------------------------------------------------------
  VkAttachmentDescription attachments[2] = {};
  attachments[0].format         = toVk(format);
  attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  attachments[1].format         = VK_FORMAT_D32_SFLOAT;
  attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_ref{};
  depth_ref.attachment = 1;
  depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount    = 1;
  subpass.pColorAttachments       = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  VkRenderPassCreateInfo rpci{};
  rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = 2;
  rpci.pAttachments    = attachments;
  rpci.subpassCount    = 1;
  rpci.pSubpasses      = &subpass;

  VkRenderPass vk_render_pass = VK_NULL_HANDLE;
  if (vkCreateRenderPass(vkdev, &rpci, nullptr, &vk_render_pass) != VK_SUCCESS) {
    spdlog::error("VulkanOffscreenTarget: render pass create failed");
    return nullptr;
  }
  self->render_pass_ = std::make_shared<VulkanRenderPass>(vk_render_pass, 2);

  // ---------------------------------------------------------------------------
  // Framebuffer
  // ---------------------------------------------------------------------------
  VkImageView fb_attach[2] = { color_view, depth_view };
  VkFramebufferCreateInfo fbci{};
  fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbci.renderPass      = vk_render_pass;
  fbci.attachmentCount = 2;
  fbci.pAttachments    = fb_attach;
  fbci.width           = width;
  fbci.height          = height;
  fbci.layers          = 1;

  VkFramebuffer vk_fb = VK_NULL_HANDLE;
  if (vkCreateFramebuffer(vkdev, &fbci, nullptr, &vk_fb) != VK_SUCCESS) {
    spdlog::error("VulkanOffscreenTarget: framebuffer create failed");
    return nullptr;
  }
  self->framebuffer_ = std::make_shared<VulkanFramebuffer>(
      vk_fb, Extent2D{width, height}, 1);

  return self;
}

VulkanOffscreenTarget::~VulkanOffscreenTarget() {
  if (!device_) return;
  VkDevice vkdev = device_->getVkDevice();
  VmaAllocator alloc = device_->getAllocator();

  // Drop framebuffer + render pass + views first (caller's references are
  // gone since we own the shared_ptrs in this object).
  if (framebuffer_) {
    auto* fb = static_cast<VulkanFramebuffer*>(framebuffer_.get());
    vkDestroyFramebuffer(vkdev, fb->getVkFramebuffer(), nullptr);
    framebuffer_.reset();
  }
  if (render_pass_) {
    auto* rp = static_cast<VulkanRenderPass*>(render_pass_.get());
    vkDestroyRenderPass(vkdev, rp->getVkRenderPass(), nullptr);
    render_pass_.reset();
  }
  if (color_view_) {
    auto* v = static_cast<VulkanTextureView*>(color_view_.get());
    vkDestroyImageView(vkdev, v->getVkImageView(), nullptr);
    color_view_.reset();
  }
  if (depth_view_) {
    auto* v = static_cast<VulkanTextureView*>(depth_view_.get());
    vkDestroyImageView(vkdev, v->getVkImageView(), nullptr);
    depth_view_.reset();
  }
  if (color_image_) vmaDestroyImage(alloc, color_image_, color_alloc_);
  if (depth_image_) vmaDestroyImage(alloc, depth_image_, depth_alloc_);
}

bool VulkanOffscreenTarget::readbackToBuffer(std::vector<uint8_t>& out_rgba,
                                              uint32_t& out_w, uint32_t& out_h) {
  if (!device_) return false;
  VkDevice vkdev = device_->getVkDevice();
  VmaAllocator alloc = device_->getAllocator();

  const VkDeviceSize byte_size = static_cast<VkDeviceSize>(width_) * height_ * 4;

  // 1. Host-visible staging buffer
  VkBuffer staging = VK_NULL_HANDLE;
  VmaAllocation staging_alloc{};
  VmaAllocationInfo staging_info{};
  {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = byte_size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
             | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(alloc, &bi, &ai, &staging, &staging_alloc, &staging_info) != VK_SUCCESS) {
      return false;
    }
  }

  // 2. One-shot command buffer
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  {
    VkCommandBufferAllocateInfo cbi{};
    cbi.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbi.commandPool        = device_->getCommandPool();
    cbi.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vkdev, &cbi, &cmd) != VK_SUCCESS) {
      vmaDestroyBuffer(alloc, staging, staging_alloc);
      return false;
    }
  }
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  // Color attachment was left in TRANSFER_SRC_OPTIMAL by the render pass's
  // finalLayout, so no extra barrier needed before copy. Just submit the
  // copy and a barrier back to TRANSFER_SRC for any next pass.
  VkBufferImageCopy region{};
  region.bufferOffset             = 0;
  region.bufferRowLength          = 0;
  region.bufferImageHeight        = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel   = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width_, height_, 1};
  vkCmdCopyImageToBuffer(cmd, color_image_,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          staging, 1, &region);

  vkEndCommandBuffer(cmd);

  // 3. Submit + wait via temp fence
  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  vkCreateFence(vkdev, &fci, nullptr, &fence);

  VkSubmitInfo si{};
  si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers    = &cmd;
  vkQueueSubmit(device_->getGraphicsQueue(), 1, &si, fence);
  vkWaitForFences(vkdev, 1, &fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(vkdev, fence, nullptr);
  vkFreeCommandBuffers(vkdev, device_->getCommandPool(), 1, &cmd);

  // 4. Copy staging → host vector
  out_rgba.resize(static_cast<size_t>(byte_size));
  std::memcpy(out_rgba.data(), staging_info.pMappedData, byte_size);
  out_w = width_;
  out_h = height_;

  vmaDestroyBuffer(alloc, staging, staging_alloc);
  return true;
}

}  // namespace HongTu::RHI::Vulkan
