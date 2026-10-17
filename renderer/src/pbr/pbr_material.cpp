#include <hongtu/renderer/pbr/pbr_material.hpp>

#include <algorithm>
#include <vector>

namespace hongtu::renderer {

namespace {

/// Box-filter 2× downsample for one RGBA8 mip level. Cheap CPU pass; combined
/// with trilinear sampling this kills far-distance aliasing for assets whose
/// UVs tile heavily (city/architectural exports often do).
void downsample2x_rgba8(const uint8_t* src, int sw, int sh,
                         std::vector<uint8_t>& dst) {
  const int dw = std::max(sw / 2, 1);
  const int dh = std::max(sh / 2, 1);
  dst.resize(static_cast<size_t>(dw) * dh * 4);
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      const int sx = x * 2;
      const int sy = y * 2;
      for (int c = 0; c < 4; ++c) {
        int sum = 0, n = 0;
        for (int dy = 0; dy < 2 && sy + dy < sh; ++dy) {
          for (int dx = 0; dx < 2 && sx + dx < sw; ++dx) {
            sum += src[((sy + dy) * sw + (sx + dx)) * 4 + c];
            ++n;
          }
        }
        dst[(y * dw + x) * 4 + c] = static_cast<uint8_t>(sum / n);
      }
    }
  }
}

}  // namespace

bool PbrMaterialFactory::initialize(RHIDevice* device) {
  device_ = device;

  SamplerDesc sd;
  sd.magFilter        = SamplerFilter::Linear;
  sd.minFilter        = SamplerFilter::Linear;
  sd.mipmapMode       = SamplerMipmapMode::Linear;
  sd.addressModeU     = SamplerAddressMode::Repeat;
  sd.addressModeV     = SamplerAddressMode::Repeat;
  sd.addressModeW     = SamplerAddressMode::Repeat;
  sd.anisotropyEnable = true;
  sd.maxAnisotropy    = 16.0f;
  sd.minLod           = 0.0f;
  sd.maxLod           = 12.0f;
  sampler_ = device_->createSampler(sd);
  if (!sampler_) return false;

  DescriptorSetLayoutDesc dl;
  // binding 0:baseColor  1:normal  2:metallic-roughness  3:emissive
  for (uint32_t b = 0; b < 4u; ++b) {
    DescriptorSetLayoutBinding lb;
    lb.binding         = b;
    lb.descriptorCount = 1u;
    lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    lb.descriptorType  = DescriptorType::CombinedImageSampler;
    dl.bindings.push_back(lb);
  }
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  return desc_layout_ != nullptr;
}

bool PbrMaterialFactory::uploadTexture(const uint8_t* pixels, int w, int h,
                                        Format format,
                                        RHITexturePtr& out_tex,
                                        RHITextureViewPtr& out_view) {
  uint32_t mip_count = 1u;
  for (int s = std::max(w, h); s > 1; s >>= 1) ++mip_count;

  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
  td.mipLevels   = mip_count;
  td.arrayLayers = 1u;
  td.format      = format;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  out_tex = device_->createTexture(td);
  if (!out_tex) return false;

  std::vector<std::vector<uint8_t>> chain;
  chain.emplace_back(pixels, pixels + static_cast<size_t>(w) * h * 4);

  std::vector<RHIDevice::MipData> mips;
  mips.reserve(mip_count);
  RHIDevice::MipData m0;
  m0.width    = static_cast<uint32_t>(w);
  m0.height   = static_cast<uint32_t>(h);
  m0.data     = chain[0].data();
  m0.dataSize = chain[0].size();
  mips.push_back(m0);

  int cw = w, ch = h;
  for (uint32_t i = 1; i < mip_count; ++i) {
    std::vector<uint8_t> next;
    downsample2x_rgba8(chain.back().data(), cw, ch, next);
    cw = std::max(cw / 2, 1);
    ch = std::max(ch / 2, 1);
    chain.push_back(std::move(next));
    RHIDevice::MipData md;
    md.width    = static_cast<uint32_t>(cw);
    md.height   = static_cast<uint32_t>(ch);
    md.data     = chain.back().data();
    md.dataSize = chain.back().size();
    mips.push_back(md);
  }
  device_->uploadTextureData(out_tex, mips, 0u);
  out_view = out_tex->createView();
  return out_view != nullptr;
}

bool PbrMaterialFactory::buildDescriptorSet(PbrMaterial& m) {
  DescriptorSetDesc dsd;
  auto add = [&](uint32_t binding, const RHITextureViewPtr& v) {
    DescriptorBinding db;
    db.binding     = binding;
    db.type        = DescriptorType::CombinedImageSampler;
    db.textureView = v;
    db.sampler     = sampler_;
    dsd.bindings.push_back(db);
  };
  add(0u, m.base_view);
  add(1u, m.normal_view);
  add(2u, m.mr_view);
  add(3u, m.emissive_view);
  m.desc_set = device_->createDescriptorSet(desc_layout_, dsd);
  return m.desc_set != nullptr;
}

std::optional<PbrMaterial> PbrMaterialFactory::create(
    const uint8_t* base_pixels,   int base_w,   int base_h,
    const uint8_t* normal_pixels, int normal_w, int normal_h,
    const uint8_t* mr_pixels,     int mr_w,     int mr_h,
    const glm::vec4& base_color_factor,
    float metallic_factor, float roughness_factor,
    const uint8_t* emissive_pixels, int em_w, int em_h,
    const glm::vec3& emissive_factor) {
  const uint8_t white[4] = {255, 255, 255, 255};
  const uint8_t flatN[4] = {128, 128, 255, 255};
  const uint8_t black[4] = {0,   0,   0,   255};

  PbrMaterial m;
  m.base_color   = base_color_factor;
  m.metallic     = metallic_factor;
  m.roughness    = roughness_factor;
  m.emissive     = emissive_factor;
  m.has_emissive = (emissive_pixels != nullptr);   // 真 emissive 贴图 → 夜窗门控

  const uint8_t* b_p = base_pixels   ? base_pixels   : white;
  int b_w_ = base_pixels   ? base_w   : 1;
  int b_h_ = base_pixels   ? base_h   : 1;
  if (!uploadTexture(b_p, b_w_, b_h_, Format::R8G8B8A8_SRGB,
                       m.base_tex, m.base_view)) return std::nullopt;

  const uint8_t* n_p = normal_pixels ? normal_pixels : flatN;
  int n_w_ = normal_pixels ? normal_w : 1;
  int n_h_ = normal_pixels ? normal_h : 1;
  if (!uploadTexture(n_p, n_w_, n_h_, Format::R8G8B8A8_UNORM,
                       m.normal_tex, m.normal_view)) return std::nullopt;

  const uint8_t* mr_p = mr_pixels    ? mr_pixels    : white;
  int mr_w_ = mr_pixels   ? mr_w     : 1;
  int mr_h_ = mr_pixels   ? mr_h     : 1;
  if (!uploadTexture(mr_p, mr_w_, mr_h_, Format::R8G8B8A8_UNORM,
                       m.mr_tex, m.mr_view)) return std::nullopt;

  // emissive 纹理缺省为 1×1 黑色(sRGB 与 base 同格式)。
  const uint8_t* e_p = emissive_pixels ? emissive_pixels : black;
  int e_w = emissive_pixels ? em_w : 1;
  int e_h = emissive_pixels ? em_h : 1;
  if (!uploadTexture(e_p, e_w, e_h, Format::R8G8B8A8_SRGB,
                       m.emissive_tex, m.emissive_view)) return std::nullopt;

  if (!buildDescriptorSet(m)) return std::nullopt;
  return m;
}

std::optional<PbrMaterial> PbrMaterialFactory::createFallback() {
  return create(nullptr, 0, 0, nullptr, 0, 0, nullptr, 0, 0,
                 glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 1.0f);
}

}  // namespace hongtu::renderer
