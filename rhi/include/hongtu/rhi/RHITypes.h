#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <memory>
#include <string>

namespace HongTu::RHI {

// ============================================================================
// Enumerations
// ============================================================================

enum class Format : uint32_t {
    Undefined = 0,
    R8_UNORM,
    R8_SNORM,
    R8_UINT,
    R8_SINT,
    R8G8_UNORM,
    R8G8_SNORM,
    R8G8_UINT,
    R8G8_SINT,
    R8G8B8_UNORM,
    R8G8B8_SNORM,
    R8G8B8_UINT,
    R8G8B8_SINT,
    R8G8B8A8_UNORM,
    R8G8B8A8_SNORM,
    R8G8B8A8_UINT,
    R8G8B8A8_SINT,
    R8G8B8A8_SRGB,
    R16_UNORM,
    R16_SNORM,
    R16_UINT,
    R16_SINT,
    R16_SFLOAT,
    R16G16_UNORM,
    R16G16_SNORM,
    R16G16_UINT,
    R16G16_SINT,
    R16G16_SFLOAT,
    R16G16B16A16_UNORM,
    R16G16B16A16_SNORM,
    R16G16B16A16_UINT,
    R16G16B16A16_SINT,
    R16G16B16A16_SFLOAT,
    R32_UINT,
    R32_SINT,
    R32_SFLOAT,
    R32G32_UINT,
    R32G32_SINT,
    R32G32_SFLOAT,
    R32G32B32_UINT,
    R32G32B32_SINT,
    R32G32B32_SFLOAT,
    R32G32B32A32_UINT,
    R32G32B32A32_SINT,
    R32G32B32A32_SFLOAT,
    D32_SFLOAT,
    D32_SFLOAT_S8_UINT,
    D24_UNORM_S8_UINT,
    BC2_UNORM_BLOCK, // DXT3 compressed
    // Append formats to preserve existing serialized enum values.
    BC1_RGB_UNORM_BLOCK,
    BC7_UNORM_BLOCK,
    BC7_SRGB_BLOCK,
    Count
};

enum class TextureType {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
};

enum class TextureUsageFlagBits : uint32_t {
    TransferDst = 0x0001,
    TransferSrc = 0x0002,
    Sampled = 0x0004,
    Storage = 0x0008,
    ColorAttachment = 0x0010,
    DepthStencilAttachment = 0x0020,
    TransientAttachment = 0x0040,
    InputAttachment = 0x0080,
};
using TextureUsageFlags = uint32_t;

inline TextureUsageFlags operator|(TextureUsageFlagBits a, TextureUsageFlagBits b) {
    return static_cast<TextureUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline TextureUsageFlags operator&(TextureUsageFlags a, TextureUsageFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class BufferUsageFlagBits : uint32_t {
    TransferDst = 0x0001,
    TransferSrc = 0x0002,
    UniformTexel = 0x0004,
    StorageTexel = 0x0008,
    Uniform = 0x0010,
    Storage = 0x0020,
    Index = 0x0040,
    Vertex = 0x0080,
    Indirect = 0x0100,
};
using BufferUsageFlags = uint32_t;

inline BufferUsageFlags operator|(BufferUsageFlagBits a, BufferUsageFlagBits b) {
    return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class ShaderStageFlagBits : uint32_t {
    Vertex = 0x0001,
    Fragment = 0x0010,
    Compute = 0x0020,
    Geometry = 0x0008,
    TessellationControl = 0x0002,
    TessellationEvaluation = 0x0004,
};
using ShaderStageFlags = uint32_t;

inline ShaderStageFlags operator|(ShaderStageFlagBits a, ShaderStageFlagBits b) {
    return static_cast<ShaderStageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ShaderStageFlags operator&(ShaderStageFlags a, ShaderStageFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
    LineListWithAdjacency,
    LineStripWithAdjacency,
    TriangleListWithAdjacency,
    TriangleStripWithAdjacency,
    PatchList,
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class CullMode {
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class FillMode {
    Solid,
    Wireframe,
    Point,
};

enum class ImageLayout {
    Undefined,
    General,
    ColorAttachmentOptimal,
    DepthStencilAttachmentOptimal,
    DepthStencilReadOnlyOptimal,
    ShaderReadOnlyOptimal,
    TransferSrcOptimal,
    TransferDstOptimal,
    Preinitialized,
    PresentSrc,
};

enum class PipelineBindPoint {
    Graphics,
    Compute,
};

enum class AccessFlagBits : uint32_t {
    IndirectCommandRead = 0x00000001,
    IndexRead = 0x00000002,
    VertexAttributeRead = 0x00000004,
    UniformRead = 0x00000008,
    InputAttachmentRead = 0x00000010,
    ShaderRead = 0x00000020,
    ShaderWrite = 0x00000040,
    ColorAttachmentRead = 0x00000080,
    ColorAttachmentWrite = 0x00000100,
    DepthStencilAttachmentRead = 0x00000200,
    DepthStencilAttachmentWrite = 0x00000400,
    TransferRead = 0x00000800,
    TransferWrite = 0x00001000,
    HostRead = 0x00002000,
    HostWrite = 0x00004000,
    MemoryRead = 0x00008000,
    MemoryWrite = 0x00010000,
};
using AccessFlags = uint32_t;

inline AccessFlags operator|(AccessFlagBits a, AccessFlagBits b) {
    return static_cast<AccessFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline AccessFlags operator&(AccessFlags a, AccessFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class PipelineStageFlagBits : uint32_t {
    TopOfPipe = 0x00000001,
    DrawIndirectBuffer = 0x00000002,
    VertexInput = 0x00000004,
    VertexShader = 0x00000008,
    TessellationControlShader = 0x00000010,
    TessellationEvaluationShader = 0x00000020,
    GeometryShader = 0x00000040,
    FragmentShader = 0x00000080,
    EarlyFragmentTests = 0x00000100,
    LateFragmentTests = 0x00000200,
    ColorAttachmentOutput = 0x00000400,
    ComputeShader = 0x00000800,
    Transfer = 0x00001000,
    BottomOfPipe = 0x00002000,
    Host = 0x00004000,
    AllGraphics = 0x00008000,
    AllCommands = 0x00010000,
};
using PipelineStageFlags = uint32_t;

inline PipelineStageFlags operator|(PipelineStageFlagBits a, PipelineStageFlagBits b) {
    return static_cast<PipelineStageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline PipelineStageFlags operator&(PipelineStageFlags a, PipelineStageFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class DescriptorType {
    UniformBuffer,
    CombinedImageSampler,
    StorageBuffer,        // SSBO,compute / 大数据 read-write
};

enum class SamplerAddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge,
};

enum class SamplerMipmapMode {
    Nearest,
    Linear,
};

enum class SamplerFilter {
    Nearest,
    Linear,
};

enum class MemoryPropertyFlagBits : uint32_t {
    DeviceLocal = 0x0001,
    HostVisible = 0x0002,
    HostCoherent = 0x0004,
    HostCached = 0x0008,
    LazilyAllocated = 0x0010,
};
using MemoryPropertyFlags = uint32_t;

inline MemoryPropertyFlags operator|(MemoryPropertyFlagBits a, MemoryPropertyFlagBits b) {
    return static_cast<MemoryPropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline MemoryPropertyFlags operator&(MemoryPropertyFlags a, MemoryPropertyFlagBits b) {
    return a & static_cast<uint32_t>(b);
}

enum class QueueFlagBits : uint32_t {
    Graphics = 0x0001,
    Compute = 0x0002,
    Transfer = 0x0004,
    SparseBinding = 0x0008,
};
using QueueFlags = uint32_t;

// ============================================================================
// Structures
// ============================================================================

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Extent3D {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
};

struct Offset2D {
    int32_t x = 0;
    int32_t y = 0;
};

struct Offset3D {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

struct Rect2D {
    Offset2D offset;
    Extent2D extent;
};

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct SamplerDesc {
    SamplerFilter magFilter = SamplerFilter::Linear;
    SamplerFilter minFilter = SamplerFilter::Linear;
    SamplerMipmapMode mipmapMode = SamplerMipmapMode::Linear;
    SamplerAddressMode addressModeU = SamplerAddressMode::Repeat;
    SamplerAddressMode addressModeV = SamplerAddressMode::Repeat;
    SamplerAddressMode addressModeW = SamplerAddressMode::Repeat;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = true;
    float maxAnisotropy = 16.0f;
    bool compareEnable = false;
    CompareOp compareOp = CompareOp::Always;
    float minLod = 0.0f;
    float maxLod = 12.0f;
    uint32_t borderColor = 0;
};

struct ColorBlendAttachmentState {
    bool blendEnable = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
    uint32_t colorWriteMask = 0x0F; // RGBA
};

struct RasterizationState {
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    FillMode fillMode = FillMode::Solid;
    CullMode cullMode = CullMode::Back;
    bool frontCounterClockwise = false;
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};

struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
};

struct VertexInputBindingDescription {
    uint32_t binding = 0;
    uint32_t stride = 0;
    bool perInstance = false;
};

struct VertexInputAttributeDescription {
    uint32_t location = 0;
    uint32_t binding = 0;
    Format format = Format::Undefined;
    uint32_t offset = 0;
};

struct VertexInputState {
    std::vector<VertexInputBindingDescription> bindings;
    std::vector<VertexInputAttributeDescription> attributes;
};

struct BufferDesc {
    uint64_t size = 0;
    BufferUsageFlags usage = 0;
    MemoryPropertyFlags memoryProperties = 0;
};

struct TextureDesc {
    TextureType type = TextureType::Texture2D;
    Extent3D extent;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    Format format = Format::R8G8B8A8_UNORM;
    TextureUsageFlags usage = 0;
    uint32_t sampleCount = 1;
};

struct AttachmentDescription {
    Format format = Format::Undefined;
    uint32_t sampleCount = 1;
    ImageLayout initialLayout = ImageLayout::Undefined;
    ImageLayout finalLayout = ImageLayout::ColorAttachmentOptimal;
    bool loadOpClear = true;
    bool storeOpStore = true;
};

struct AttachmentReference {
    uint32_t attachment = 0;
    ImageLayout layout = ImageLayout::ColorAttachmentOptimal;
};

struct SubpassDescription {
    std::vector<AttachmentReference> colorAttachments;
    AttachmentReference depthStencilAttachment;
    bool hasDepthStencil = false;
};

struct SubpassDependency {
    uint32_t srcSubpass = 0;
    uint32_t dstSubpass = 0;
    PipelineStageFlags srcStageMask = PipelineStageFlags(0);
    PipelineStageFlags dstStageMask = PipelineStageFlags(0);
    AccessFlags srcAccessMask = AccessFlags(0);
    AccessFlags dstAccessMask = AccessFlags(0);
    bool byRegion = false;
};

struct RenderPassDesc {
    std::vector<AttachmentDescription> attachments;
    std::vector<SubpassDescription> subpasses;
    std::vector<SubpassDependency> dependencies;
};

struct FramebufferDesc {
    uint32_t renderPass = 0; // Handle to RenderPass
    std::vector<uint32_t> attachments; // Handles to TextureViews
    Extent2D extent;
    uint32_t layers = 1;
};

struct PushConstantRange {
    ShaderStageFlags stageFlags = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct DescriptorSetLayoutBinding {
    uint32_t binding = 0;
    uint32_t descriptorCount = 1;
    ShaderStageFlags stageFlags = 0;
    DescriptorType descriptorType = DescriptorType::UniformBuffer;
};

struct DescriptorSetLayoutDesc {
    std::vector<DescriptorSetLayoutBinding> bindings;
};

// Forward declarations for pipeline desc
class RHIShader;
class RHIRenderPass;
class RHIPipelineLayout;
class RHIDescriptorSetLayout;
class RHITextureView;
class RHITexture;
class RHISampler;
class RHIBuffer;

struct DescriptorBinding {
    uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    std::shared_ptr<RHITextureView> textureView;
    std::shared_ptr<RHISampler> sampler;
    // StorageBuffer / UniformBuffer 用 — 绑定的 RHI buffer + 范围
    std::shared_ptr<RHIBuffer> buffer;
    uint64_t bufferOffset = 0;
    uint64_t bufferRange  = 0;     // 0 = whole buffer (VK_WHOLE_SIZE)
};

struct DescriptorSetDesc {
    std::vector<DescriptorBinding> bindings;
};

struct PipelineLayoutDesc {
    std::vector<std::shared_ptr<RHIDescriptorSetLayout>> setLayouts;
    std::vector<PushConstantRange> pushConstantRanges;
};

struct GraphicsPipelineDesc {
    std::vector<std::shared_ptr<RHIShader>> shaders;
    VertexInputState vertexInput;
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    bool primitiveRestartEnable = false;
    RasterizationState rasterization;
    DepthStencilState depthStencil;
    std::vector<ColorBlendAttachmentState> colorBlendAttachments;
    std::shared_ptr<RHIRenderPass> renderPass;
    uint32_t subpass = 0;
    std::shared_ptr<RHIPipelineLayout> pipelineLayout;
    std::vector<Viewport> dynamicViewports;   // empty = static
    std::vector<Rect2D> dynamicScissors;      // empty = static
};

struct ComputePipelineDesc {
    std::shared_ptr<RHIShader>         shader;
    std::shared_ptr<RHIPipelineLayout> pipelineLayout;
};

struct ShaderDesc {
    ShaderStageFlagBits stage = ShaderStageFlagBits::Vertex;
    std::vector<uint32_t> spirvCode;
    std::string entryPoint = "main";
};

struct ImageMemoryBarrier {
    AccessFlags srcAccessMask = 0;
    AccessFlags dstAccessMask = 0;
    ImageLayout oldLayout = ImageLayout::Undefined;
    ImageLayout newLayout = ImageLayout::Undefined;
    uint32_t image = 0; // Handle to Texture (legacy; prefer `texture`)
    std::shared_ptr<RHITexture> texture;  // target texture (smart ptr)
    uint32_t baseMipLevel = 0;
    uint32_t levelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;
};

struct BufferMemoryBarrier {
    AccessFlags                 srcAccessMask = 0;
    AccessFlags                 dstAccessMask = 0;
    std::shared_ptr<RHIBuffer>  buffer;            // 目标 buffer(智能指针)
    uint64_t                    offset = 0;
    uint64_t                    size = 0;          // 0 = VK_WHOLE_SIZE
};

} // namespace HongTu::RHI
