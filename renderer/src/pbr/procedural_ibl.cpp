#include <hongtu/renderer/pbr/procedural_ibl.hpp>

#include <stb_image.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace hongtu::renderer {

// ===========================================================================
// CPU bake math — lifted VERBATIM from the editor's hdr_loader.cpp so the
// procedural IBL is byte-identical to what the editor used to produce inline.
// ===========================================================================

namespace {
inline glm::vec3 mix3(const glm::vec3& a, const glm::vec3& b, float t) {
  return a + (b - a) * t;
}
inline float smooth(float a, float b, float v) {
  float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Cubemap face → world-space direction at uv (u, v in [0,1]).
// Vulkan / Khronos convention.
glm::vec3 faceDirection(int face, float u, float v) {
  float a = 2.0f * u - 1.0f;   //  -1..+1
  float b = 2.0f * v - 1.0f;
  switch (face) {
    case 0: return glm::normalize(glm::vec3( 1.0f, -b, -a));  // +X
    case 1: return glm::normalize(glm::vec3(-1.0f, -b,  a));  // -X
    case 2: return glm::normalize(glm::vec3( a,    1.0f, b)); // +Y
    case 3: return glm::normalize(glm::vec3( a,   -1.0f,-b)); // -Y
    case 4: return glm::normalize(glm::vec3( a,   -b,    1.0f)); // +Z
    default:return glm::normalize(glm::vec3(-a,   -b,   -1.0f)); // -Z
  }
}

// Sample equirect bilinearly with a direction.
glm::vec3 sampleEquirect(const float* px, int w, int h, glm::vec3 dir) {
  float lon = std::atan2(dir.z, dir.x);            // -π..π
  float lat = std::asin(std::clamp(dir.y, -1.0f, 1.0f));   // -π/2..π/2
  float u = (lon / (2.0f * glm::pi<float>())) + 0.5f;
  float v = 0.5f - lat / glm::pi<float>();
  u = u - std::floor(u);
  v = std::clamp(v, 0.0f, 0.9999f);
  float fx = u * (w - 1);
  float fy = v * (h - 1);
  int ix = int(fx), iy = int(fy);
  float tx = fx - ix, ty = fy - iy;
  int ix1 = std::min(ix + 1, w - 1);
  int iy1 = std::min(iy + 1, h - 1);
  auto P = [&](int x, int y) {
    return glm::vec3(px[(y * w + x) * 3 + 0],
                     px[(y * w + x) * 3 + 1],
                     px[(y * w + x) * 3 + 2]);
  };
  glm::vec3 a = glm::mix(P(ix, iy), P(ix1, iy), tx);
  glm::vec3 b = glm::mix(P(ix, iy1), P(ix1, iy1), tx);
  return glm::mix(a, b, ty);
}

// Sample a cube (6 faces × N²) by direction. Returns linear bilinear sample.
glm::vec3 sampleCube(const ProceduralIbl::CubeFaces& cube, glm::vec3 dir) {
  // Pick dominant axis.
  float ax = std::abs(dir.x), ay = std::abs(dir.y), az = std::abs(dir.z);
  int face;
  float sc, tc, ma;
  if (ax >= ay && ax >= az) {
    if (dir.x > 0) { face = 0; sc = -dir.z; tc = -dir.y; ma = ax; }
    else           { face = 1; sc =  dir.z; tc = -dir.y; ma = ax; }
  } else if (ay >= ax && ay >= az) {
    if (dir.y > 0) { face = 2; sc =  dir.x; tc =  dir.z; ma = ay; }
    else           { face = 3; sc =  dir.x; tc = -dir.z; ma = ay; }
  } else {
    if (dir.z > 0) { face = 4; sc =  dir.x; tc = -dir.y; ma = az; }
    else           { face = 5; sc = -dir.x; tc = -dir.y; ma = az; }
  }
  float u = 0.5f * (sc / ma + 1.0f);
  float v = 0.5f * (tc / ma + 1.0f);
  int N = cube.face_size;
  float fx = u * (N - 1);
  float fy = v * (N - 1);
  int ix = int(fx), iy = int(fy);
  float tx = fx - ix, ty = fy - iy;
  int ix1 = std::min(ix + 1, N - 1);
  int iy1 = std::min(iy + 1, N - 1);
  const float* p = cube.faces[face].data();
  auto P = [&](int x, int y) {
    int idx = (y * N + x) * 4;
    return glm::vec3(p[idx], p[idx + 1], p[idx + 2]);
  };
  glm::vec3 a = glm::mix(P(ix, iy), P(ix1, iy), tx);
  glm::vec3 b = glm::mix(P(ix, iy1), P(ix1, iy1), tx);
  return glm::mix(a, b, ty);
}

// Hammersley low-discrepancy sequence for hemisphere integration.
float radicalInverseVdC(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}
}  // namespace

void ProceduralIbl::generateProceduralHdr(Preset preset, std::vector<float>& out_pixels,
                                          int& out_w, int& out_h) {
  out_w = 256;
  out_h = 128;
  out_pixels.resize(static_cast<size_t>(out_w) * out_h * 3);

  for (int y = 0; y < out_h; ++y) {
    // theta: 0 at top row (zenith), π at bottom row (nadir)
    float theta = (float(y) + 0.5f) / out_h * glm::pi<float>();
    float v = std::cos(theta);                  //  1 zenith, 0 horizon, -1 nadir
    for (int x = 0; x < out_w; ++x) {
      glm::vec3 c{0.5f};
      switch (preset) {
        case Preset::Sunset: {
          // Deep blue zenith → orange horizon → near-black ground.
          glm::vec3 zenith{0.04f, 0.07f, 0.20f};
          glm::vec3 horiz{2.50f, 0.80f, 0.30f};
          glm::vec3 ground{0.03f, 0.025f, 0.02f};
          if (v >= 0.0f) c = mix3(horiz, zenith, smooth(0.0f, 0.7f, v));
          else           c = mix3(horiz, ground, smooth(0.0f, 0.4f, -v));
          break;
        }
        case Preset::BlueSky: {
          // Bright cool zenith → light horizon → brown earth.
          glm::vec3 zenith{1.20f, 1.80f, 3.20f};
          glm::vec3 horiz{1.60f, 1.50f, 1.40f};
          glm::vec3 ground{0.32f, 0.25f, 0.16f};
          if (v >= 0.0f) c = mix3(horiz, zenith, smooth(0.0f, 0.7f, v));
          else           c = mix3(horiz, ground, smooth(0.0f, 0.5f, -v));
          break;
        }
        case Preset::Studio: {
          // Soft white softbox — very slight gradient, no ground.
          glm::vec3 zenith{1.50f, 1.50f, 1.55f};
          glm::vec3 horiz{1.10f, 1.10f, 1.12f};
          glm::vec3 floor_{0.90f, 0.90f, 0.92f};
          if (v >= 0.0f) c = mix3(horiz, zenith, smooth(-0.2f, 0.8f, v));
          else           c = mix3(horiz, floor_, smooth(-0.2f, 0.8f, -v));
          break;
        }
        case Preset::Overcast: {
          // Flat gray sky, almost no directionality.
          glm::vec3 sky{0.95f, 0.97f, 1.00f};
          glm::vec3 ground{0.30f, 0.30f, 0.30f};
          c = (v >= 0.0f) ? sky : mix3(sky, ground, smooth(0.0f, 0.5f, -v));
          break;
        }
      }
      float* p = &out_pixels[(y * out_w + x) * 3];
      p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
  }
}

ProceduralIbl::CubeFaces ProceduralIbl::buildCubemapFromEquirect(
    const float* eq_pixels, int eq_w, int eq_h, int face_size, float rotation_rad) {
  CubeFaces out;
  out.face_size = face_size;
  out.faces.resize(6);
  // 环境旋转:绕竖轴(Y,equirect 极轴)转采样方向 → 转动反射里环境的朝向。
  const float cs = std::cos(rotation_rad), sn = std::sin(rotation_rad);
  for (int f = 0; f < 6; ++f) {
    out.faces[f].resize(face_size * face_size * 4);
    float* dst = out.faces[f].data();
    for (int y = 0; y < face_size; ++y) {
      float v = (y + 0.5f) / face_size;
      for (int x = 0; x < face_size; ++x) {
        float u = (x + 0.5f) / face_size;
        glm::vec3 dir = faceDirection(f, u, v);
        if (rotation_rad != 0.0f)
          dir = glm::vec3(dir.x * cs - dir.z * sn, dir.y, dir.x * sn + dir.z * cs);
        glm::vec3 c = sampleEquirect(eq_pixels, eq_w, eq_h, dir);
        int idx = (y * face_size + x) * 4;
        dst[idx + 0] = c.r;
        dst[idx + 1] = c.g;
        dst[idx + 2] = c.b;
        dst[idx + 3] = 1.0f;
      }
    }
  }
  return out;
}

ProceduralIbl::CubeFaces ProceduralIbl::buildIrradianceFromCube(
    const CubeFaces& env_cube, int out_size, int sample_count) {
  CubeFaces out;
  out.face_size = out_size;
  out.faces.resize(6);
  for (int f = 0; f < 6; ++f) {
    out.faces[f].resize(out_size * out_size * 4);
    float* dst = out.faces[f].data();
    for (int y = 0; y < out_size; ++y) {
      float v = (y + 0.5f) / out_size;
      for (int x = 0; x < out_size; ++x) {
        float u = (x + 0.5f) / out_size;
        glm::vec3 N = faceDirection(f, u, v);
        // Build TBN
        glm::vec3 up = std::abs(N.y) < 0.99f ? glm::vec3(0, 1, 0)
                                              : glm::vec3(1, 0, 0);
        glm::vec3 T = glm::normalize(glm::cross(up, N));
        glm::vec3 B = glm::cross(N, T);
        glm::vec3 acc{0.0f};
        for (int s = 0; s < sample_count; ++s) {
          float u1 = float(s) / sample_count;
          float u2 = radicalInverseVdC(uint32_t(s));
          float cosTheta = std::sqrt(1.0f - u1);  // cosine-weighted
          float sinTheta = std::sqrt(u1);
          float phi      = 2.0f * glm::pi<float>() * u2;
          glm::vec3 L_local{sinTheta * std::cos(phi),
                            sinTheta * std::sin(phi),
                            cosTheta};
          glm::vec3 L = L_local.x * T + L_local.y * B + L_local.z * N;
          acc += sampleCube(env_cube, L);
        }
        acc *= (1.0f / sample_count);
        int idx = (y * out_size + x) * 4;
        dst[idx + 0] = acc.r;
        dst[idx + 1] = acc.g;
        dst[idx + 2] = acc.b;
        dst[idx + 3] = 1.0f;
      }
    }
  }
  return out;
}

std::vector<ProceduralIbl::CubeFaces> ProceduralIbl::buildCubeMipChain(
    const CubeFaces& base, int target_mips) {
  std::vector<CubeFaces> chain;
  chain.push_back(base);
  int N = base.face_size;
  for (int m = 1; m < target_mips && N > 1; ++m) {
    int Nh = std::max(1, N / 2);
    CubeFaces mip;
    mip.face_size = Nh;
    mip.faces.resize(6);
    const CubeFaces& prev = chain.back();
    int Np = prev.face_size;
    for (int f = 0; f < 6; ++f) {
      mip.faces[f].resize(Nh * Nh * 4);
      float* dst = mip.faces[f].data();
      const float* src = prev.faces[f].data();
      for (int y = 0; y < Nh; ++y) {
        for (int x = 0; x < Nh; ++x) {
          // 2×2 box average
          int sx = std::min(x * 2,     Np - 1);
          int sy = std::min(y * 2,     Np - 1);
          int sx1 = std::min(sx + 1, Np - 1);
          int sy1 = std::min(sy + 1, Np - 1);
          glm::vec3 sum{0.0f};
          for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
              int xx = (dx == 0) ? sx : sx1;
              int yy = (dy == 0) ? sy : sy1;
              int idx = (yy * Np + xx) * 4;
              sum += glm::vec3(src[idx], src[idx+1], src[idx+2]);
            }
          }
          sum *= 0.25f;
          int didx = (y * Nh + x) * 4;
          dst[didx + 0] = sum.r;
          dst[didx + 1] = sum.g;
          dst[didx + 2] = sum.b;
          dst[didx + 3] = 1.0f;
        }
      }
    }
    chain.push_back(std::move(mip));
    N = Nh;
  }
  return chain;
}

// ===========================================================================
// GPU side — lifted VERBATIM from the editor's buildIblInfrastructure /
// uploadIblCubes (viewport_renderer.cpp).
// ===========================================================================

bool ProceduralIbl::buildLayout(RHIDevice* device) {
  if (!device) return false;
  device_ = device;
  if (desc_layout_ && sampler_) return true;  // idempotent

  // Sampler — linear, cube-clamp to avoid seams at face edges.
  SamplerDesc sd;
  sd.magFilter    = SamplerFilter::Linear;
  sd.minFilter    = SamplerFilter::Linear;
  sd.mipmapMode   = SamplerMipmapMode::Linear;
  sd.addressModeU = SamplerAddressMode::ClampToEdge;
  sd.addressModeV = SamplerAddressMode::ClampToEdge;
  sd.addressModeW = SamplerAddressMode::ClampToEdge;
  sd.minLod = 0.0f;
  sd.maxLod = 16.0f;
  sampler_ = device_->createSampler(sd);
  if (!sampler_) return false;

  // Descriptor set layout: 2 cube samplers.
  DescriptorSetLayoutDesc dl;
  for (uint32_t i = 0; i < 2; ++i) {
    DescriptorSetLayoutBinding lb;
    lb.binding         = i;
    lb.descriptorCount = 1u;
    lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    lb.descriptorType  = DescriptorType::CombinedImageSampler;
    dl.bindings.push_back(lb);
  }
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  return desc_layout_ != nullptr;
}

bool ProceduralIbl::uploadCubes(
    const std::vector<std::vector<std::vector<float>>>& spec_mips,
    int spec_face_size,
    const std::vector<std::vector<float>>& irradiance_faces,
    int irr_face_size) {
  if (!device_ || spec_mips.empty() || irradiance_faces.size() != 6) {
    return false;
  }
  // ---- Diffuse irradiance cube (single mip) ----
  {
    TextureDesc td;
    td.type        = TextureType::TextureCube;
    td.extent      = {static_cast<uint32_t>(irr_face_size),
                       static_cast<uint32_t>(irr_face_size), 1u};
    td.mipLevels   = 1u;
    td.arrayLayers = 6u;
    td.format      = Format::R32G32B32A32_SFLOAT;
    td.usage       = TextureUsageFlagBits::Sampled
                   | TextureUsageFlagBits::TransferDst;
    td.sampleCount = 1u;
    diffuse_cube_tex_ = device_->createTexture(td);
    if (!diffuse_cube_tex_) return false;
    diffuse_cube_view_ = diffuse_cube_tex_->createView();
    for (uint32_t face = 0; face < 6; ++face) {
      const auto& fdata = irradiance_faces[face];
      RHIDevice::MipData mip;
      mip.width    = irr_face_size;
      mip.height   = irr_face_size;
      mip.depth    = 1;
      mip.data     = fdata.data();
      mip.dataSize = fdata.size() * sizeof(float);
      device_->uploadTextureData(diffuse_cube_tex_, {mip}, face);
    }
  }
  // ---- Specular cube (full mip chain) ----
  {
    const uint32_t mip_count = static_cast<uint32_t>(spec_mips.size());
    TextureDesc td;
    td.type        = TextureType::TextureCube;
    td.extent      = {static_cast<uint32_t>(spec_face_size),
                       static_cast<uint32_t>(spec_face_size), 1u};
    td.mipLevels   = mip_count;
    td.arrayLayers = 6u;
    td.format      = Format::R32G32B32A32_SFLOAT;
    td.usage       = TextureUsageFlagBits::Sampled
                   | TextureUsageFlagBits::TransferDst;
    td.sampleCount = 1u;
    spec_cube_tex_ = device_->createTexture(td);
    if (!spec_cube_tex_) return false;
    spec_cube_view_ = spec_cube_tex_->createView();
    // Per-face: pack all mips into the mips vector.
    for (uint32_t face = 0; face < 6; ++face) {
      std::vector<RHIDevice::MipData> mips;
      mips.reserve(mip_count);
      uint32_t w = spec_face_size, h = spec_face_size;
      for (uint32_t m = 0; m < mip_count; ++m) {
        const auto& fdata = spec_mips[m][face];
        RHIDevice::MipData md;
        md.width    = w;
        md.height   = h;
        md.depth    = 1;
        md.data     = fdata.data();
        md.dataSize = fdata.size() * sizeof(float);
        mips.push_back(md);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
      }
      device_->uploadTextureData(spec_cube_tex_, mips, face);
    }
    spec_face_size_ = spec_face_size;
    spec_mip_count_ = mip_count;
  }
  // ---- (Re)allocate descriptor set ----
  DescriptorSetDesc dsd;
  {
    DescriptorBinding b;
    b.binding     = 0;
    b.type        = DescriptorType::CombinedImageSampler;
    b.textureView = diffuse_cube_view_;
    b.sampler     = sampler_;
    dsd.bindings.push_back(b);
  }
  {
    DescriptorBinding b;
    b.binding     = 1;
    b.type        = DescriptorType::CombinedImageSampler;
    b.textureView = spec_cube_view_;
    b.sampler     = sampler_;
    dsd.bindings.push_back(b);
  }
  desc_set_ = device_->createDescriptorSet(desc_layout_, dsd);
  return desc_set_ != nullptr;
}

bool ProceduralIbl::buildProcedural(RHIDevice* device, int preset_index, float rotation_rad) {
  if (!buildLayout(device)) return false;
  std::vector<float> eq_px;
  int eq_w = 0, eq_h = 0;
  auto preset = static_cast<Preset>(std::clamp(preset_index, 0, 3));
  generateProceduralHdr(preset, eq_px, eq_w, eq_h);
  // Build cube + mips + irradiance.
  CubeFaces base_cube = buildCubemapFromEquirect(eq_px.data(), eq_w, eq_h, 128, rotation_rad);
  auto mip_chain = buildCubeMipChain(base_cube, 6);
  CubeFaces irr_cube = buildIrradianceFromCube(base_cube, 32, 128);
  // Pack into vec<vec<vec<float>>>
  std::vector<std::vector<std::vector<float>>> spec_mips;
  spec_mips.reserve(mip_chain.size());
  for (auto& m : mip_chain) {
    spec_mips.push_back(std::move(m.faces));
  }
  return uploadCubes(spec_mips, base_cube.face_size,
                     irr_cube.faces, irr_cube.face_size);
}

bool ProceduralIbl::buildFromHdrFile(RHIDevice* device, const std::string& abs_path,
                                     float rotation_rad) {
  if (!buildLayout(device)) return false;
  int w = 0, h = 0, ch = 0;
  float* px = stbi_loadf(abs_path.c_str(), &w, &h, &ch, 3);
  if (!px) {
    spdlog::warn("[procedural_ibl] HDR load failed '{}': {}", abs_path,
                  stbi_failure_reason() ? stbi_failure_reason() : "?");
    return false;
  }
  CubeFaces base_cube = buildCubemapFromEquirect(px, w, h, 256, rotation_rad);
  stbi_image_free(px);
  auto mip_chain = buildCubeMipChain(base_cube, 7);
  CubeFaces irr_cube = buildIrradianceFromCube(base_cube, 32, 256);
  std::vector<std::vector<std::vector<float>>> spec_mips;
  spec_mips.reserve(mip_chain.size());
  for (auto& m : mip_chain) spec_mips.push_back(std::move(m.faces));
  spdlog::info("[procedural_ibl] HDR cubemap built: '{}' diffuse irradiance 32×32×6 "
                "+ specular {}×{}×6 × {} mips",
                abs_path, base_cube.face_size, base_cube.face_size, mip_chain.size());
  return uploadCubes(spec_mips, base_cube.face_size,
                     irr_cube.faces, irr_cube.face_size);
}

}  // namespace hongtu::renderer
