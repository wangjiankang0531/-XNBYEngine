// drill_avatar.hpp — 演练人员的骨骼蒙皮渲染层(P3)。
//
// 人员要素用 rigged glb(CesiumMan 兜底)渲染,而非 VegetationLayer 的静态实例。
// 支持 idle / walk / run 三态运动:每个人员按其移动速度选 clip,移动快→run、
// 走动→walk、停下→idle。每态共享一具骨架 + 同一姿态(lockstep),每帧每个在用的
// clip 采样一次 → 蒙皮矩阵 → 上传该态自己的 SSBO,再按态分组、对每个人员用各自的
// model 矩阵(pos+yaw)绘制。
//
// 命名 clip:glb 里名字含 walk/run/idle 的动画分别用作三态;缺 run → 用 walk 加速
// 播放(腿更快),缺 idle → 用绑定姿态(站立)。这样单 clip 的 CesiumMan 也能三态可用。
//
// 加载/归一化/绘制路径移植自 hongtu-editor ViewportRenderer 的 roam character
// (已验证 CesiumMan 蒙皮 + 贴图 + Y-up→Z-up + 落地归一化)。
#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <hongtu/renderer/pbr/skinned_pbr_pipeline.hpp>
#include <hongtu/renderer/animation/skeleton.hpp>
#include <hongtu/renderer/animation/animation_clip.hpp>
#include <hongtu/asset/GltfLoader.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace crs {

class DrillAvatarLayer {
 public:
  using RHIDevice = HongTu::RHI::RHIDevice;

  // 运动三态。
  enum Loco { kIdle = 0, kWalk = 1, kRun = 2, kLocoCount = 3 };

  /// 初始化:蒙皮管线 + 加载角色 + VBO/IBO/三态 SSBO/描述符。失败返回 false
  /// (调用方此时让人员回退到程序化占位)。
  bool initialize(RHIDevice* device, HongTu::RHI::RHIRenderPassPtr hdr_pass,
                  const std::string& glb_path) {
    device_ = device;
    if (!pipeline_.initialize(device, hdr_pass)) {
      spdlog::warn("[avatar] SkinnedPbrPipeline init 失败 — 人员骨骼渲染禁用");
      return false;
    }
    if (!loadCharacter(glb_path)) {
      spdlog::warn("[avatar] 角色加载失败({}) — 人员回退程序化占位", glb_path);
      return false;
    }
    if (!uploadBuffers()) return false;
    ready_ = true;
    spdlog::info("[avatar] 就绪:verts={} idx={} joints={} walk={:.2f}s run={}({:.2f}s) idle={}",
                 verts_.size(), indices_.size(), skeleton_.jointCount(),
                 walk_dur_, has_run_ ? "clip" : "walk×加速", run_dur_,
                 has_idle_ ? "clip" : "绑定姿态");
    return true;
  }

  bool ready() const { return ready_; }
  bool empty() const { return avatars_.empty(); }

  void add(uint64_t id, const glm::vec3& pos, float yaw_rad) {
    Avatar a;
    a.pos = pos; a.yaw = yaw_rad;
    a.last_pos = pos; a.has_last = true;
    avatars_[id] = a;
  }

  /// 移动更新:由相邻两次更新的位移估速度,选 idle/walk/run。
  void update(uint64_t id, const glm::vec3& pos, float yaw_rad) {
    auto& a = avatars_[id];
    if (a.has_last) {
      const float dt   = std::max(a.t_since_update, 1e-3f);
      const float inst = glm::length(pos - a.last_pos) / dt;
      a.speed     = a.has_speed ? glm::mix(a.speed, inst, 0.5f) : inst;  // 平滑抖动
      a.has_speed = true;
    }
    a.last_pos = pos; a.has_last = true; a.t_since_update = 0.0f;
    a.pos = pos; a.yaw = yaw_rad;
    a.loco = classify(a.speed);
  }

  void remove(uint64_t id) { avatars_.erase(id); }
  void clear() { avatars_.clear(); }
  bool has(uint64_t id) const { return avatars_.count(id) > 0; }

  /// 推进共享动画时间 + 老化每个人员的「上次更新至今」;久无更新(到终点停下)→ idle。
  void tick(float dt) {
    anim_time_ += dt;
    for (auto& [id, a] : avatars_) {
      a.t_since_update += dt;
      if (a.t_since_update > kIdleTimeout) { a.speed = 0.0f; a.loco = kIdle; }
    }
  }

  /// 记录绘制:每个在用三态各采样一次 → 上传该态 SSBO → 按态分组绘制各人员。
  /// 在 HDR 钩子里、与 dyn.record/recordModels/recordFires 同处调用。
  void record(const HongTu::RHI::RHICommandBufferPtr& cmd, const glm::mat4& view_proj,
              const glm::vec3& sun_dir, const glm::vec3& sun_color, float sun_intensity) {
    using namespace HongTu::RHI;
    if (!ready_ || avatars_.empty()) return;

    bool used[kLocoCount] = {false, false, false};
    for (const auto& [id, a] : avatars_) used[a.loco] = true;
    for (int s = 0; s < kLocoCount; ++s)
      if (used[s]) uploadSlot(s);

    cmd->bindPipeline(pipeline_.getPipeline());
    cmd->bindVertexBuffer(0, vbo_);
    cmd->bindIndexBuffer(ibo_);

    // 半-Lambert 着色器期望 light_color≈1(sun_intensity 是 BRDF 用的,直接乘会过曝)。
    const float lit = glm::clamp(sun_intensity / 6.0f, 0.15f, 1.1f);
    hongtu::renderer::SkinnedPbrPipeline::PushConstants pc{};
    pc.view_proj   = view_proj;
    pc.light_dir   = glm::vec4(glm::normalize(sun_dir), 0.0f);
    pc.light_color = glm::vec4(sun_color * lit, 1.0f);
    pc.base_color  = textured_ ? glm::vec4(1.0f) : glm::vec4(0.32f, 0.45f, 0.72f, 1.0f);

    const auto stages = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
                      | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment);
    for (int s = 0; s < kLocoCount; ++s) {
      if (!used[s]) continue;
      cmd->bindDescriptorSet(0, desc_set_[s], pipeline_.getPipelineLayout());
      for (const auto& [id, av] : avatars_) {
        if (av.loco != s) continue;
        glm::mat4 world(1.0f);
        world = glm::translate(world, av.pos);
        world = glm::rotate(world, av.yaw, glm::vec3(0, 0, 1));
        pc.model = world * rig_xform_;   // rig 空间蒙皮 → 归一化(Z-up/落地)→ 摆放
        cmd->pushConstants(pipeline_.getPipelineLayout(), stages, 0, sizeof(pc), &pc);
        cmd->drawIndexed(static_cast<uint32_t>(indices_.size()), 1, 0, 0, 0);
      }
    }
  }

 private:
  struct Avatar {
    glm::vec3 pos{0.0f};
    float     yaw = 0.0f;
    glm::vec3 last_pos{0.0f};
    bool      has_last = false;
    bool      has_speed = false;
    float     speed = 0.0f;            // 平滑速度(m/s)
    float     t_since_update = 0.0f;   // 距上次 update 的秒数
    int       loco = kIdle;
  };

  static constexpr float kIdleSpeed  = 0.4f;   // < 此速 → idle
  static constexpr float kRunSpeed   = 2.6f;   // > 此速 → run
  static constexpr float kIdleTimeout = 0.35f; // 久无更新 → 视为停下

  static int classify(float spd) {
    if (spd < kIdleSpeed) return kIdle;
    if (spd > kRunSpeed)  return kRun;
    return kWalk;
  }

  static bool nameHas(const std::string& s, const char* sub) {
    std::string a = s;
    std::transform(a.begin(), a.end(), a.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(sub) != std::string::npos;
  }

  bool loadCharacter(const std::string& path) {
    hongtu::asset::GltfLoader loader;
    auto model = loader.load(path);
    if (!model || model->skins.empty() || model->animations.empty()) return false;
    skeleton_ = model->skins[0].skeleton;

    // 命名 clip → 三态;缺 run 用 walk 加速、缺 idle 用绑定姿态。
    const hongtu::renderer::animation::AnimationClip* pw = nullptr;
    const hongtu::renderer::animation::AnimationClip* pr = nullptr;
    const hongtu::renderer::animation::AnimationClip* pi = nullptr;
    for (const auto& c : model->animations) {
      if (!pw && nameHas(c.name, "walk")) pw = &c;
      if (!pr && nameHas(c.name, "run"))  pr = &c;
      if (!pi && nameHas(c.name, "idle")) pi = &c;
    }
    if (!pw) pw = &model->animations[0];   // 无命名 → 首个 clip 当 walk
    walk_clip_ = *pw;
    has_run_   = (pr != nullptr);
    run_clip_  = has_run_ ? *pr : walk_clip_;
    run_rate_  = has_run_ ? 1.0f : 1.7f;   // 无 run clip → walk 提速近似奔跑
    has_idle_  = (pi != nullptr);
    if (has_idle_) idle_clip_ = *pi;
    walk_dur_  = walk_clip_.duration_seconds > 0.0f ? walk_clip_.duration_seconds : 1.0f;
    run_dur_   = run_clip_.duration_seconds  > 0.0f ? run_clip_.duration_seconds  : 1.0f;
    idle_dur_  = has_idle_ && idle_clip_.duration_seconds > 0.0f ? idle_clip_.duration_seconds : 1.0f;

    int albedo_tex_id = -1;
    for (const auto& md : model->meshes) {
      if (md.skin_id < 0 || md.joint_indices.empty() || md.positions.empty()) continue;
      const uint32_t base = static_cast<uint32_t>(verts_.size());
      for (size_t i = 0; i < md.positions.size(); ++i) {
        hongtu::renderer::SkinnedMeshVertex v{};
        v.position = md.positions[i];
        v.normal   = i < md.normals.size() ? md.normals[i] : glm::vec3(0, 0, 1);
        v.uv       = i < md.uvs.size() ? md.uvs[i] : glm::vec2(0.0f);
        v.tangent  = glm::vec4(1, 0, 0, 1);
        v.joint_indices = i < md.joint_indices.size() ? md.joint_indices[i] : glm::uvec4(0);
        v.joint_weights = i < md.joint_weights.size() ? md.joint_weights[i] : glm::vec4(1, 0, 0, 0);
        verts_.push_back(v);
      }
      for (uint32_t idx : md.indices) indices_.push_back(base + idx);
      if (albedo_tex_id < 0 && md.material_id >= 0 &&
          md.material_id < static_cast<int>(model->materials.size())) {
        albedo_tex_id = model->materials[md.material_id].base_color_texture;
      }
    }
    if (verts_.empty() || indices_.empty()) return false;

    // rig→本地归一化:绕 X+90° 把 glTF Y-up 转 Z-up,按【蒙皮后】实际姿态拟合身高
    // 缩放到 ~1.75m、脚底落 z=0、x/y 居中(蒙皮在 rig 空间算,此矩阵整体抬入本地)。
    const glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    glm::vec3 e_min(std::numeric_limits<float>::max()), e_max(-std::numeric_limits<float>::max());
    {
      using hongtu::renderer::animation::AnimationSampler;
      hongtu::renderer::animation::SkinPose pose;
      std::vector<glm::mat4> skin;
      const float dur = walk_dur_;
      for (float t : {0.0f, dur * 0.25f, dur * 0.5f, dur * 0.75f}) {
        AnimationSampler::bindPose(skeleton_, pose);
        AnimationSampler::sample(skeleton_, walk_clip_, t, pose);
        skeleton_.skinMatrices(pose, skin);
        for (const auto& v : verts_) {
          const glm::uvec4& ji = v.joint_indices;
          const glm::vec4&  w  = v.joint_weights;
          glm::mat4 m = w.x * skin[ji.x] + w.y * skin[ji.y]
                      + w.z * skin[ji.z] + w.w * skin[ji.w];
          glm::vec3 e = glm::vec3(Rx * (m * glm::vec4(v.position, 1.0f)));
          e_min = glm::min(e_min, e);
          e_max = glm::max(e_max, e);
        }
      }
    }
    const float height = std::max(0.01f, e_max.z - e_min.z);
    const float scale  = 1.75f / height;
    glm::mat4 M(1.0f);
    M = glm::translate(M, glm::vec3(-scale * (e_min.x + e_max.x) * 0.5f,
                                    -scale * (e_min.y + e_max.y) * 0.5f, -scale * e_min.z));
    M = glm::scale(M, glm::vec3(scale));
    rig_xform_ = M * Rx;

    loadAlbedo(*model, albedo_tex_id);
    return true;
  }

  void loadAlbedo(const hongtu::asset::ModelData& model, int tex_id) {
    using namespace HongTu::RHI;
    if (tex_id < 0 || tex_id >= static_cast<int>(model.textures.size())) return;
    const auto& t = model.textures[tex_id];
    if (t.pixels.empty() || t.width <= 0 || t.height <= 0) return;
    std::vector<uint8_t> rgba;
    const uint8_t* src = t.pixels.data();
    if (t.channels == 4) {
      rgba.assign(src, src + size_t(t.width) * t.height * 4);
    } else {
      rgba.resize(size_t(t.width) * t.height * 4, 255);
      const int ch = t.channels > 0 ? t.channels : 3;
      for (size_t p = 0; p < size_t(t.width) * t.height; ++p)
        for (int c = 0; c < 3; ++c) rgba[p * 4 + c] = (c < ch) ? src[p * ch + c] : 0;
    }
    TextureDesc td;
    td.type = TextureType::Texture2D;
    td.extent = {static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height), 1u};
    td.mipLevels = 1u; td.arrayLayers = 1u;
    td.format = Format::R8G8B8A8_UNORM;
    td.usage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst;
    td.sampleCount = 1u;
    albedo_tex_ = device_->createTexture(td);
    if (!albedo_tex_) return;
    albedo_view_ = albedo_tex_->createView();
    RHIDevice::MipData mip;
    mip.width = t.width; mip.height = t.height; mip.depth = 1;
    mip.data = rgba.data(); mip.dataSize = rgba.size();
    device_->uploadTextureData(albedo_tex_, {mip}, 0);
    SamplerDesc sd;
    sd.magFilter = SamplerFilter::Linear; sd.minFilter = SamplerFilter::Linear;
    sd.addressModeU = SamplerAddressMode::Repeat;
    sd.addressModeV = SamplerAddressMode::Repeat;
    sd.addressModeW = SamplerAddressMode::Repeat;
    albedo_sampler_ = device_->createSampler(sd);
    textured_ = (albedo_view_ && albedo_sampler_);
  }

  // 采样某态姿态 → 蒙皮矩阵 → 上传该态 SSBO。
  void uploadSlot(int slot) {
    using AS = hongtu::renderer::animation::AnimationSampler;
    AS::bindPose(skeleton_, pose_);
    if (slot == kWalk) {
      AS::sample(skeleton_, walk_clip_, std::fmod(anim_time_, walk_dur_), pose_);
    } else if (slot == kRun) {
      AS::sample(skeleton_, run_clip_, std::fmod(anim_time_ * run_rate_, run_dur_), pose_);
    } else if (has_idle_) {
      AS::sample(skeleton_, idle_clip_, std::fmod(anim_time_, idle_dur_), pose_);
    }
    // else idle 无 clip:保留 bindPose(站立)。
    std::vector<glm::mat4> mats;
    skeleton_.skinMatrices(pose_, mats);
    std::memcpy(skin_ssbo_[slot]->map(), mats.data(), mats.size() * sizeof(glm::mat4));
    skin_ssbo_[slot]->unmap();
  }

  bool uploadBuffers() {
    using namespace HongTu::RHI;
    {
      BufferDesc bd;
      bd.size = verts_.size() * sizeof(hongtu::renderer::SkinnedMeshVertex);
      bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
      bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      vbo_ = device_->createBuffer(bd);
      if (!vbo_) return false;
      std::memcpy(vbo_->map(), verts_.data(), bd.size); vbo_->unmap();
    }
    {
      BufferDesc bd;
      bd.size = indices_.size() * sizeof(uint32_t);
      bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
      bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      ibo_ = device_->createBuffer(bd);
      if (!ibo_) return false;
      std::memcpy(ibo_->map(), indices_.data(), bd.size); ibo_->unmap();
    }
    // 三态各一份蒙皮 SSBO + 描述符(共用 albedo);避免帧内改同一缓冲产生写后读冒险。
    const uint32_t ssbo_size = static_cast<uint32_t>(skeleton_.jointCount() * sizeof(glm::mat4));
    for (int s = 0; s < kLocoCount; ++s) {
      BufferDesc bd;
      bd.size = ssbo_size;
      bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Storage);
      bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
      skin_ssbo_[s] = device_->createBuffer(bd);
      if (!skin_ssbo_[s]) return false;
      desc_set_[s] = pipeline_.createDescriptorSet(
          skin_ssbo_[s], ssbo_size,
          textured_ ? albedo_view_ : nullptr, textured_ ? albedo_sampler_ : nullptr);
      if (!desc_set_[s]) return false;
    }
    return true;
  }

  RHIDevice* device_ = nullptr;
  hongtu::renderer::SkinnedPbrPipeline pipeline_;
  hongtu::renderer::animation::Skeleton      skeleton_;
  hongtu::renderer::animation::AnimationClip walk_clip_;
  hongtu::renderer::animation::AnimationClip run_clip_;
  hongtu::renderer::animation::AnimationClip idle_clip_;
  bool  has_run_  = false;
  bool  has_idle_ = false;
  float run_rate_ = 1.7f;
  float walk_dur_ = 1.0f, run_dur_ = 1.0f, idle_dur_ = 1.0f;
  hongtu::renderer::animation::SkinPose      pose_;
  std::vector<hongtu::renderer::SkinnedMeshVertex> verts_;
  std::vector<uint32_t> indices_;
  glm::mat4 rig_xform_{1.0f};

  HongTu::RHI::RHIBufferPtr vbo_, ibo_;
  HongTu::RHI::RHIBufferPtr        skin_ssbo_[kLocoCount];
  HongTu::RHI::RHIDescriptorSetPtr desc_set_[kLocoCount];
  HongTu::RHI::RHITexturePtr     albedo_tex_;
  HongTu::RHI::RHITextureViewPtr albedo_view_;
  HongTu::RHI::RHISamplerPtr     albedo_sampler_;
  bool textured_ = false;
  bool ready_ = false;
  float anim_time_ = 0.0f;

  std::unordered_map<uint64_t, Avatar> avatars_;
};

}  // namespace crs
