#pragma once

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace HongTu::Demo {

  // A first local sun-shadow map. It rasterizes the actual scene VBO/IBO;
  // transparent effects and camera-relative sky do not cast opaque shadows.
  class MeshSunShadow {
  public:
    static constexpr uint32_t kSide = 2048;
    struct Uniform {
      glm::mat4 lightViewProjection{1.0f};
      glm::vec4 options{0.0f}; // enabled, normalized depth bias, texel size, reserved
    };
    static_assert(sizeof(Uniform) == 80);

    bool initialize(RHI::RHIDevice* device, const RHI::VertexInputState& input) {
      using namespace RHI;
      TextureDesc color;
      color.extent = {kSide, kSide, 1};
      color.format = Format::R32_SFLOAT;
      color.usage = TextureUsageFlagBits::ColorAttachment | TextureUsageFlagBits::Sampled;
      color_ = device->createTexture(color);
      TextureDesc depth = color;
      depth.format = Format::D32_SFLOAT;
      depth.usage = static_cast<TextureUsageFlags>(TextureUsageFlagBits::DepthStencilAttachment);
      depth_ = device->createTexture(depth);
      if (!color_ || !depth_)
        return false;
      colorView_ = color_->createView();
      depthView_ = depth_->createView();
      if (!colorView_ || !depthView_)
        return false;
      RenderPassDesc pass;
      AttachmentDescription c;
      c.format = color.format;
      c.finalLayout = ImageLayout::ShaderReadOnlyOptimal;
      pass.attachments.push_back(c);
      AttachmentDescription d;
      d.format = depth.format;
      d.finalLayout = ImageLayout::DepthStencilAttachmentOptimal;
      d.storeOpStore = false;
      pass.attachments.push_back(d);
      SubpassDescription sub;
      sub.colorAttachments.push_back({0, ImageLayout::ColorAttachmentOptimal});
      sub.hasDepthStencil = true;
      sub.depthStencilAttachment = {1, ImageLayout::DepthStencilAttachmentOptimal};
      pass.subpasses.push_back(sub);
      SubpassDependency before;
      before.srcSubpass = UINT32_MAX;
      before.dstSubpass = 0;
      before.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
      before.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
      before.dstStageMask = PipelineStageFlagBits::ColorAttachmentOutput | PipelineStageFlagBits::EarlyFragmentTests;
      before.dstAccessMask = AccessFlagBits::ColorAttachmentWrite | AccessFlagBits::DepthStencilAttachmentWrite;
      pass.dependencies.push_back(before);
      SubpassDependency after;
      after.srcSubpass = 0;
      after.dstSubpass = UINT32_MAX;
      after.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
      after.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
      after.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::FragmentShader);
      after.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderRead);
      pass.dependencies.push_back(after);
      pass_ = device->createRenderPass(pass);
      if (!pass_)
        return false;
      framebuffer_ = device->createFramebuffer(pass_, {colorView_, depthView_}, {kSide, kSide}, 1);
      BufferDesc buffer;
      buffer.size = sizeof(Uniform);
      buffer.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Uniform);
      buffer.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      uniformBuffer_ = device->createBuffer(buffer);
      if (!framebuffer_ || !uniformBuffer_)
        return false;
      DescriptorSetLayoutDesc descriptors;
      descriptors.bindings.push_back(
          {0, 1, static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment), DescriptorType::CombinedImageSampler});
      descriptors.bindings.push_back(
          {1, 1, static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment), DescriptorType::UniformBuffer});
      descriptorLayout_ = device->createDescriptorSetLayout(descriptors);
      SamplerDesc sampler;
      sampler.magFilter = sampler.minFilter = SamplerFilter::Nearest;
      sampler.addressModeU = sampler.addressModeV = SamplerAddressMode::ClampToEdge;
      sampler_ = device->createSampler(sampler);
      if (!descriptorLayout_ || !sampler_)
        return false;
      DescriptorSetDesc set;
      DescriptorBinding image;
      image.binding = 0;
      image.type = DescriptorType::CombinedImageSampler;
      image.textureView = colorView_;
      image.sampler = sampler_;
      set.bindings.push_back(image);
      DescriptorBinding ubo;
      ubo.binding = 1;
      ubo.type = DescriptorType::UniformBuffer;
      ubo.buffer = uniformBuffer_;
      ubo.bufferRange = sizeof(Uniform);
      set.bindings.push_back(ubo);
      descriptorSet_ = device->createDescriptorSet(descriptorLayout_, set);
      PipelineLayoutDesc layout;
      layout.pushConstantRanges.push_back({static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex), 0, 64});
      pipelineLayout_ = device->createPipelineLayout(layout);
      if (!descriptorSet_ || !pipelineLayout_)
        return false;
      Asset::ShaderCompiler compiler;
      const auto vs = compiler.compileGLSLToSPIRV(R"glsl(
      layout(location=0) in vec3 position;
      layout(location=4) in float kind;
      layout(push_constant) uniform Light { mat4 vp; } light;
      void main() {
        gl_Position = kind < 2.5 ? light.vp * vec4(position, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
      }
    )glsl",
                                                  "main",
                                                  "vertex");
      const auto fs = compiler.compileGLSLToSPIRV(R"glsl(
      layout(location=0) out float lightDepth;
      void main() { lightDepth = gl_FragCoord.z; }
    )glsl",
                                                  "main",
                                                  "fragment");
      if (!vs.success || !fs.success) {
        std::cerr << "mesh shadow shader: " << vs.errorMessage << " " << fs.errorMessage << "\n";
        return false;
      }
      ShaderDesc vertex;
      vertex.stage = ShaderStageFlagBits::Vertex;
      vertex.spirvCode = vs.spirvCode;
      vertex_ = device->createShader(vertex);
      ShaderDesc fragment;
      fragment.stage = ShaderStageFlagBits::Fragment;
      fragment.spirvCode = fs.spirvCode;
      fragment_ = device->createShader(fragment);
      GraphicsPipelineDesc pipeline;
      pipeline.shaders = {vertex_, fragment_};
      pipeline.vertexInput = input;
      pipeline.rasterization.cullMode = CullMode::None;
      pipeline.depthStencil.depthTestEnable = true;
      pipeline.depthStencil.depthWriteEnable = true;
      pipeline.depthStencil.depthCompareOp = CompareOp::Less;
      pipeline.colorBlendAttachments.push_back(ColorBlendAttachmentState{});
      pipeline.renderPass = pass_;
      pipeline.pipelineLayout = pipelineLayout_;
      pipeline_ = device->createGraphicsPipeline(pipeline);
      return pass_ && framebuffer_ && uniformBuffer_ && descriptorLayout_ && descriptorSet_ && pipeline_;
    }

    void update(glm::vec3 focus, glm::vec3 camera, glm::vec3 sun, bool enabled) {
      halfExtent_ = std::clamp(glm::distance(focus, camera) * 0.65f, 70.0f, 700.0f);
      sun = glm::normalize(sun);
      const glm::vec3 up = std::abs(sun.z) > 0.96f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
      glm::mat4 view = glm::lookAt(focus + sun * 2000.0f, focus, up);
      // Snap the light-plane translation to texels to reduce camera-motion shimmer.
      const float texel = 2.0f * halfExtent_ / kSide;
      view[3][0] = std::round(view[3][0] / texel) * texel;
      view[3][1] = std::round(view[3][1] / texel) * texel;
      glm::mat4 projection = glm::orthoRH_ZO(-halfExtent_, halfExtent_, -halfExtent_, halfExtent_, 1.0f, 4000.0f);
      projection[1][1] *= -1.0f;
      uniform_.lightViewProjection = projection * view;
      uniform_.options = {enabled && sun.z > 0.01f ? 1.0f : 0.0f, 0.000035f, 1.0f / kSide, 0.0f};
      void* mapped = uniformBuffer_->map();
      if (!mapped)
        throw std::runtime_error("mesh shadow uniform map failed");
      std::memcpy(mapped, &uniform_, sizeof(uniform_));
      uniformBuffer_->unmap();
    }

    void render(const RHI::RHICommandBufferPtr& cmd,
                const RHI::RHIBufferPtr& vertices,
                const RHI::RHIBufferPtr& indices,
                uint32_t count) {
      using namespace RHI;
      Rect2D area;
      area.extent = {kSide, kSide};
      cmd->beginRenderPass(pass_, framebuffer_, area, {{1, 1, 1, 1}}, 1.0f, 0);
      Viewport viewport;
      viewport.width = viewport.height = static_cast<float>(kSide);
      viewport.maxDepth = 1.0f;
      cmd->setViewport(viewport);
      cmd->setScissor(area);
      cmd->bindPipeline(pipeline_);
      cmd->pushConstants(pipelineLayout_,
                         static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
                         0,
                         64,
                         &uniform_.lightViewProjection);
      cmd->bindVertexBuffer(0, vertices);
      cmd->bindIndexBuffer(indices);
      const uint32_t submitted = uniform_.options.x > 0.5f ? count : 0;
      cmd->drawIndexed(submitted);
      submittedIndices_ += submitted;
      cmd->endRenderPass();
      ++renderedPasses_;
    }

    const RHI::RHIDescriptorSetLayoutPtr& layout() const {
      return descriptorLayout_;
    }
    const RHI::RHIDescriptorSetPtr& descriptors() const {
      return descriptorSet_;
    }
    uint64_t renderedPasses() const {
      return renderedPasses_;
    }
    uint64_t submittedIndices() const {
      return submittedIndices_;
    }
    bool active() const {
      return uniform_.options.x > 0.5f;
    }
    float halfExtent() const {
      return halfExtent_;
    }
    void clear() {
      *this = MeshSunShadow{};
    }

  private:
    Uniform uniform_;
    float halfExtent_ = 0;
    uint64_t renderedPasses_ = 0;
    uint64_t submittedIndices_ = 0;
    RHI::RHITexturePtr color_, depth_;
    RHI::RHITextureViewPtr colorView_, depthView_;
    RHI::RHIBufferPtr uniformBuffer_;
    RHI::RHISamplerPtr sampler_;
    RHI::RHIDescriptorSetLayoutPtr descriptorLayout_;
    RHI::RHIDescriptorSetPtr descriptorSet_;
    RHI::RHIPipelineLayoutPtr pipelineLayout_;
    RHI::RHIRenderPassPtr pass_;
    RHI::RHIFramebufferPtr framebuffer_;
    RHI::RHIShaderPtr vertex_, fragment_;
    RHI::RHIPipelinePtr pipeline_;
  };
} // namespace HongTu::Demo
