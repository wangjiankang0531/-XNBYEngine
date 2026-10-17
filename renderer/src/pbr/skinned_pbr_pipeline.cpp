#include <hongtu/renderer/pbr/skinned_pbr_pipeline.hpp>

#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

namespace hongtu::renderer {

using HongTu::Asset::ShaderCompiler;

bool SkinnedPbrPipeline::initialize(RHIDevice* device,
                                     RHIRenderPassPtr scene_render_pass) {
  device_ = device;
  if (!device_) return false;
  return createPipeline(scene_render_pass);
}

bool SkinnedPbrPipeline::createPipeline(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  // Vertex shader:
  //   - skin matrix = Σ weight_i × joint_matrices[joint_index_i] (4-weight LBS)
  //   - apply skin, then model, then view_proj
  //   - normal transformed by skin (assume uniform scale + rotation; OK for P0)
  const char* vsrc = R"glsl(
    layout(location = 0) in vec3  inPos;
    layout(location = 1) in vec3  inNormal;
    layout(location = 2) in vec2  inUV;
    layout(location = 3) in vec4  inTangent;
    layout(location = 4) in uvec4 inJointIndices;
    layout(location = 5) in vec4  inJointWeights;

    layout(set = 0, binding = 0) readonly buffer Joints {
      mat4 joint_matrices[];   // skin matrices = world * inverse_bind
    };

    layout(push_constant) uniform PC {
      mat4 view_proj;
      mat4 model;
      vec4 light_dir;
      vec4 light_color;
      vec4 base_color;
    } pc;

    layout(location = 0) out vec3 vNormalWorld;
    layout(location = 1) out vec2 vUV;

    void main() {
      mat4 skin = inJointWeights.x * joint_matrices[inJointIndices.x]
                + inJointWeights.y * joint_matrices[inJointIndices.y]
                + inJointWeights.z * joint_matrices[inJointIndices.z]
                + inJointWeights.w * joint_matrices[inJointIndices.w];
      vec4 pos_local = skin * vec4(inPos, 1.0);
      vec4 pos_world = pc.model * pos_local;
      gl_Position    = pc.view_proj * pos_world;
      // Normal: drop translation, take rotation/scale only (mat3).
      mat3 skin3  = mat3(skin);
      mat3 model3 = mat3(pc.model);
      vNormalWorld = normalize(model3 * skin3 * inNormal);
      vUV         = inUV;
    }
  )glsl";

  // Fragment shader: half-Lambert + ambient. Keeps the rest of the PBR
  // mixin (IBL / spec) out of P0 scope — agents look reasonable as simple
  // colored shapes.
  const char* fsrc = R"glsl(
    layout(location = 0) in  vec3 vNormalWorld;
    layout(location = 1) in  vec2 vUV;
    layout(location = 0) out vec4 outColor;

    layout(set = 0, binding = 1) uniform sampler2D albedoTex;

    layout(push_constant) uniform PC {
      mat4 view_proj;
      mat4 model;
      vec4 light_dir;
      vec4 light_color;
      vec4 base_color;
    } pc;

    void main() {
      // Albedo = base_color × texture. Procedural agents bind a 1×1 white
      // texture so albedo == base_color; textured glTF chars pass white
      // base_color to show the texture verbatim.
      vec4 tex   = texture(albedoTex, vUV);
      vec3 albedo = pc.base_color.rgb * tex.rgb;
      vec3 N = normalize(vNormalWorld);
      vec3 L = normalize(pc.light_dir.xyz);
      float ndl    = max(0.0, dot(N, L));
      // Half-Lambert wrap so shadow side doesn't go pure black.
      float wrap   = ndl * 0.5 + 0.5;
      vec3 diffuse = albedo * pc.light_color.rgb * wrap;
      vec3 ambient = albedo * 0.2;
      outColor = vec4(ambient + diffuse, pc.base_color.a * tex.a);
    }
  )glsl";

  auto vs_r = compiler.compileGLSLToSPIRV(vsrc, "main", "vertex");
  auto fs_r = compiler.compileGLSLToSPIRV(fsrc, "main", "fragment");
  if (!vs_r.success || !fs_r.success) {
    spdlog::error("SkinnedPbrPipeline: shader compile failed vs='{}' fs='{}'",
                  vs_r.errorMessage, fs_r.errorMessage);
    return false;
  }

  ShaderDesc vsd;
  vsd.stage = ShaderStageFlagBits::Vertex;
  vsd.spirvCode = vs_r.spirvCode;
  vs_ = device_->createShader(vsd);
  ShaderDesc fsd;
  fsd.stage = ShaderStageFlagBits::Fragment;
  fsd.spirvCode = fs_r.spirvCode;
  fs_ = device_->createShader(fsd);
  if (!vs_ || !fs_) return false;

  // Descriptor set layout — binding 0: joint-matrix SSBO (vertex);
  // binding 1: albedo combined-image-sampler (fragment).
  DescriptorSetLayoutDesc dsld;
  DescriptorSetLayoutBinding b0;
  b0.binding = 0;
  b0.descriptorType  = DescriptorType::StorageBuffer;
  b0.descriptorCount = 1;
  b0.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
  dsld.bindings.push_back(b0);
  DescriptorSetLayoutBinding b1;
  b1.binding = 1;
  b1.descriptorType  = DescriptorType::CombinedImageSampler;
  b1.descriptorCount = 1;
  b1.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  dsld.bindings.push_back(b1);
  desc_layout_ = device_->createDescriptorSetLayout(dsld);
  if (!desc_layout_) return false;

  PipelineLayoutDesc ld;
  ld.setLayouts.push_back(desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                 | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
  pcr.offset = 0;
  pcr.size = sizeof(PushConstants);
  ld.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(ld);
  if (!pipeline_layout_) return false;

  // Vertex input — single binding, 6 attributes.
  VertexInputBindingDescription bind;
  bind.binding = 0;
  bind.stride  = sizeof(SkinnedMeshVertex);
  bind.perInstance = false;

  auto attr = [](uint32_t loc, uint32_t off, Format f) {
    VertexInputAttributeDescription a;
    a.location = loc; a.binding = 0; a.format = f; a.offset = off;
    return a;
  };

  GraphicsPipelineDesc pd;
  pd.shaders = {vs_, fs_};
  pd.vertexInput.bindings = {bind};
  pd.vertexInput.attributes = {
      attr(0, offsetof(SkinnedMeshVertex, position),      Format::R32G32B32_SFLOAT),
      attr(1, offsetof(SkinnedMeshVertex, normal),        Format::R32G32B32_SFLOAT),
      attr(2, offsetof(SkinnedMeshVertex, uv),            Format::R32G32_SFLOAT),
      attr(3, offsetof(SkinnedMeshVertex, tangent),       Format::R32G32B32A32_SFLOAT),
      attr(4, offsetof(SkinnedMeshVertex, joint_indices), Format::R32G32B32A32_UINT),
      attr(5, offsetof(SkinnedMeshVertex, joint_weights), Format::R32G32B32A32_SFLOAT),
  };
  pd.primitiveTopology = PrimitiveTopology::TriangleList;
  pd.rasterization.cullMode = CullMode::Back;
  pd.rasterization.fillMode = FillMode::Solid;
  pd.depthStencil.depthTestEnable = true;
  pd.depthStencil.depthWriteEnable = true;
  pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
  ColorBlendAttachmentState blend;
  blend.blendEnable = false;
  pd.colorBlendAttachments.push_back(blend);
  pd.renderPass = render_pass;
  pd.pipelineLayout = pipeline_layout_;

  pipeline_ = device_->createGraphicsPipeline(pd);
  if (!pipeline_) return false;
  return createDefaultWhiteTexture();
}

bool SkinnedPbrPipeline::createDefaultWhiteTexture() {
  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {1u, 1u, 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R8G8B8A8_UNORM;
  td.usage       = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst;
  td.sampleCount = 1u;
  white_tex_ = device_->createTexture(td);
  if (!white_tex_) return false;
  white_view_ = white_tex_->createView();
  const uint8_t white[4] = {255, 255, 255, 255};
  RHIDevice::MipData mip;
  mip.width = 1; mip.height = 1; mip.depth = 1;
  mip.data = white; mip.dataSize = sizeof(white);
  device_->uploadTextureData(white_tex_, {mip}, 0);

  SamplerDesc sd;
  sd.magFilter    = SamplerFilter::Linear;
  sd.minFilter    = SamplerFilter::Linear;
  sd.addressModeU = SamplerAddressMode::Repeat;
  sd.addressModeV = SamplerAddressMode::Repeat;
  sd.addressModeW = SamplerAddressMode::Repeat;
  white_sampler_ = device_->createSampler(sd);
  return white_view_ != nullptr && white_sampler_ != nullptr;
}

RHIDescriptorSetPtr
SkinnedPbrPipeline::createDescriptorSet(const RHIBufferPtr& joint_matrices_ssbo,
                                         uint32_t buffer_range_bytes) {
  return createDescriptorSet(joint_matrices_ssbo, buffer_range_bytes, nullptr, nullptr);
}

RHIDescriptorSetPtr
SkinnedPbrPipeline::createDescriptorSet(const RHIBufferPtr& joint_matrices_ssbo,
                                         uint32_t buffer_range_bytes,
                                         const RHITextureViewPtr& albedo_view,
                                         const RHISamplerPtr& albedo_sampler) {
  if (!desc_layout_ || !joint_matrices_ssbo) return nullptr;
  DescriptorSetDesc dsd;
  DescriptorBinding b0;
  b0.binding = 0;
  b0.type    = DescriptorType::StorageBuffer;
  b0.buffer  = joint_matrices_ssbo;
  b0.bufferOffset = 0;
  b0.bufferRange  = buffer_range_bytes;
  dsd.bindings.push_back(b0);

  DescriptorBinding b1;
  b1.binding     = 1;
  b1.type        = DescriptorType::CombinedImageSampler;
  b1.textureView = albedo_view  ? albedo_view  : white_view_;
  b1.sampler     = albedo_sampler ? albedo_sampler : white_sampler_;
  dsd.bindings.push_back(b1);
  return device_->createDescriptorSet(desc_layout_, dsd);
}

}  // namespace hongtu::renderer
