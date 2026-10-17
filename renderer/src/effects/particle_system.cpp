#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/asset/ShaderCompiler.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hongtu::renderer {

// =============================================================================
// ParticleSystem — CPU update path.
//
// SoA layout: 5 parallel vectors all sized to `capacity_`. Indices 0..alive-1
// are live. Cull = swap-with-last + decrement. Spawn = write at index
// `alive_count_++`. O(1) for both — no allocations per frame after warmup.
// =============================================================================

ParticleSystem::ParticleSystem(uint32_t max_particles) {
  setCapacity(max_particles);
}

void ParticleSystem::setCapacity(uint32_t max_particles) {
  capacity_ = max_particles;
  positions_.resize(max_particles);
  velocities_.resize(max_particles);
  ages_.resize(max_particles, 0.0f);
  lifetimes_.resize(max_particles, 1.0f);
  sizes_.resize(max_particles, 1.0f);
  if (alive_count_ > capacity_) alive_count_ = capacity_;
}

void ParticleSystem::reset() {
  alive_count_      = 0;
  spawn_accumulator_ = 0.0f;
  // SoA slots are kept allocated; we just rewind alive_count_.
}

// -----------------------------------------------------------------------------
// xorshift64 RNG — deterministic + fast.
// `mutable` on the system so `spawnOne()` (which we call from update) can
// advance state without forcing the public API to take a non-const this.
// -----------------------------------------------------------------------------

float ParticleSystem::rand01() const {
  uint64_t s = rng_state_;
  s ^= s << 13;
  s ^= s >> 7;
  s ^= s << 17;
  rng_state_ = s;
  // upper 24 bits → float in [0, 1)
  return (s >> 40) / 16777216.0f;
}

glm::vec3 ParticleSystem::randPointInShape() const {
  switch (emitter_.shape) {
    case EmitterShape::Point:
      return emitter_.center;

    case EmitterShape::Box: {
      const glm::vec3& he = emitter_.half_extent;
      glm::vec3 r(rand01() * 2.0f - 1.0f,
                  rand01() * 2.0f - 1.0f,
                  rand01() * 2.0f - 1.0f);
      return emitter_.center + r * he;
    }

    case EmitterShape::Sphere: {
      // Rejection sampling inside unit sphere then scale. ~52% accept rate,
      // negligible at small particle counts and avoids trig.
      glm::vec3 r;
      do {
        r = glm::vec3(rand01() * 2.0f - 1.0f,
                       rand01() * 2.0f - 1.0f,
                       rand01() * 2.0f - 1.0f);
      } while (glm::dot(r, r) > 1.0f);
      return emitter_.center + r * emitter_.sphere_radius;
    }

    case EmitterShape::Cone:
      // Cone spawn point is the apex — direction handled by velocity.
      return emitter_.center;
  }
  return emitter_.center;
}

glm::vec3 ParticleSystem::randInitialVelocity() const {
  // Base direction from emitter; cone overrides direction with a random
  // vector inside the cone's half-angle.
  glm::vec3 base = emitter_.initial_velocity;

  if (emitter_.shape == EmitterShape::Cone) {
    // Sample direction inside cone. We build an orthonormal basis around
    // cone_dir then perturb by a random angle ∈ [0, half_angle].
    glm::vec3 axis = glm::normalize(emitter_.cone_dir);
    // Pick any vector not parallel to axis.
    glm::vec3 helper = std::abs(axis.z) < 0.9f ? glm::vec3(0, 0, 1)
                                               : glm::vec3(1, 0, 0);
    glm::vec3 tan_x = glm::normalize(glm::cross(axis, helper));
    glm::vec3 tan_y = glm::cross(axis, tan_x);

    float u1 = rand01();
    float u2 = rand01();
    // Uniform cosine within cone — angle ∈ [0, half_angle].
    float cos_t = 1.0f - u1 * (1.0f - std::cos(emitter_.cone_half_angle));
    float sin_t = std::sqrt(std::max(0.0f, 1.0f - cos_t * cos_t));
    float phi   = u2 * 2.0f * 3.14159265f;
    glm::vec3 dir = axis * cos_t
                  + tan_x * (sin_t * std::cos(phi))
                  + tan_y * (sin_t * std::sin(phi));
    float speed = glm::length(emitter_.initial_velocity);
    base = dir * speed;
  }

  if (emitter_.velocity_jitter > 0.0f) {
    glm::vec3 jitter(rand01() * 2.0f - 1.0f,
                     rand01() * 2.0f - 1.0f,
                     rand01() * 2.0f - 1.0f);
    base += jitter * (emitter_.velocity_jitter * glm::length(base));
  }

  // Apply emitter orientation (ZYX Euler in degrees) — rotates the spawn
  // velocity in world space. Zero rotation = identity → cheap skip.
  const auto& re = emitter_.rotation_euler_deg;
  if (re.x != 0.0f || re.y != 0.0f || re.z != 0.0f) {
    constexpr float kDegToRad = 0.0174532925f;
    float cz = std::cos(re.z * kDegToRad), sz = std::sin(re.z * kDegToRad);
    float cy = std::cos(re.y * kDegToRad), sy = std::sin(re.y * kDegToRad);
    float cx = std::cos(re.x * kDegToRad), sx = std::sin(re.x * kDegToRad);
    // R = Rz * Ry * Rx (same convention as ModelInstance).
    glm::mat3 Rz{cz, sz, 0,  -sz, cz, 0,  0, 0, 1};
    glm::mat3 Ry{cy, 0, -sy,  0, 1, 0,  sy, 0, cy};
    glm::mat3 Rx{1, 0, 0,  0, cx, sx,  0, -sx, cx};
    base = (Rz * Ry * Rx) * base;
  }
  return base;
}

bool ParticleSystem::spawnOne() {
  if (alive_count_ >= capacity_) return false;
  const uint32_t i = alive_count_++;
  positions_[i]  = randPointInShape();
  velocities_[i] = randInitialVelocity();
  ages_[i]       = 0.0f;
  const float life_jitter = (rand01() * 2.0f - 1.0f) * emitter_.lifetime_jitter;
  lifetimes_[i]  = std::max(0.01f, emitter_.lifetime_seconds * (1.0f + life_jitter));
  const float size_jitter = (rand01() * 2.0f - 1.0f) * emitter_.size_jitter;
  sizes_[i]      = std::max(0.0f, emitter_.initial_size * (1.0f + size_jitter));
  return true;
}

uint32_t ParticleSystem::update(float dt) {
  if (dt <= 0.0f || capacity_ == 0) return alive_count_;

  // 1. Spawn — emit_rate per second + burst remainder.
  if (emitter_.enabled) {
    spawn_accumulator_ += emitter_.emit_rate * dt;
    while (spawn_accumulator_ >= 1.0f) {
      if (!spawnOne()) break;
      spawn_accumulator_ -= 1.0f;
    }
    // Burst: emit `burst_remaining` particles regardless of rate.
    while (emitter_.burst_remaining > 0.0f) {
      if (!spawnOne()) break;
      emitter_.burst_remaining -= 1.0f;
    }
    if (emitter_.burst_remaining < 0.0f) emitter_.burst_remaining = 0.0f;
  }

  // 2. Advect + age + cull. Walk live range; on death, swap-with-last so we
  //    revisit the just-swapped index on next iteration of the loop.
  const float drag_factor = std::max(0.0f, 1.0f - emitter_.drag * dt);
  uint32_t i = 0;
  while (i < alive_count_) {
    ages_[i] += dt;
    if (ages_[i] >= lifetimes_[i]) {
      // Swap with last live and decrement.
      const uint32_t last = --alive_count_;
      if (i != last) {
        positions_[i]  = positions_[last];
        velocities_[i] = velocities_[last];
        ages_[i]       = ages_[last];
        lifetimes_[i]  = lifetimes_[last];
        sizes_[i]      = sizes_[last];
      }
      continue;  // re-check this slot (now the swapped particle)
    }
    velocities_[i] = velocities_[i] * drag_factor + emitter_.acceleration * dt;
    positions_[i] += velocities_[i] * dt;
    sizes_[i]     += emitter_.size_growth * dt;
    ++i;
  }
  return alive_count_;
}

// -----------------------------------------------------------------------------
// Color ramp sampling — 4-keyframe linear interpolation by normalized age.
// -----------------------------------------------------------------------------

namespace {
glm::vec4 sampleRamp(const EmitterDesc::ColorRamp& ramp, float t) {
  t = std::clamp(t, 0.0f, 0.9999f);
  const float scaled = t * 3.0f;       // 4 stops → 3 segments
  const int   seg    = static_cast<int>(scaled);
  const float u      = scaled - static_cast<float>(seg);
  return glm::mix(ramp.colors[seg], ramp.colors[seg + 1], u);
}
}  // namespace

void ParticleSystem::buildInstanceBuffer(std::vector<ParticleInstance>& out) const {
  out.clear();
  out.reserve(alive_count_);
  for (uint32_t i = 0; i < alive_count_; ++i) {
    ParticleInstance inst;
    inst.world_pos = positions_[i];
    inst.size      = sizes_[i];
    const float t = ages_[i] / std::max(1e-6f, lifetimes_[i]);
    inst.color = sampleRamp(emitter_.color_ramp, t);
    out.push_back(inst);
  }
  // Cheap painter's sort: descending Z so far-away particles (low Z if camera
  // looks down) draw first. Real cameras may need view-space depth sort —
  // future optimization.
  std::sort(out.begin(), out.end(), [](const ParticleInstance& a, const ParticleInstance& b) {
    return a.world_pos.z < b.world_pos.z;
  });
}

// =============================================================================
// EmitterPresets — 4 stock effects.
// =============================================================================

namespace EmitterPresets {

EmitterDesc makeFire(glm::vec3 source, float emit_rate) {
  EmitterDesc d;
  d.shape           = EmitterShape::Sphere;
  d.center          = source;
  d.sphere_radius   = 0.25f;
  d.emit_rate       = emit_rate;
  d.initial_velocity = {0.0f, 0.0f, 2.0f};   // strong upward bias
  d.velocity_jitter  = 0.4f;
  d.lifetime_seconds = 1.4f;
  d.lifetime_jitter  = 0.3f;
  d.initial_size     = 0.45f;
  d.size_jitter      = 0.25f;
  d.acceleration     = {0.0f, 0.0f, 0.6f};   // buoyancy
  d.drag             = 0.15f;
  d.size_growth      = 0.4f;
  // Black-body fire color ramp: bright white → yellow → red → black + fade
  d.color_ramp.colors[0] = {1.0f, 0.95f, 0.85f, 1.0f};
  d.color_ramp.colors[1] = {1.0f, 0.70f, 0.20f, 1.0f};
  d.color_ramp.colors[2] = {0.65f, 0.18f, 0.05f, 0.85f};
  d.color_ramp.colors[3] = {0.05f, 0.02f, 0.02f, 0.0f};
  return d;
}

EmitterDesc makeSmoke(glm::vec3 source, glm::vec3 wind, float emit_rate) {
  EmitterDesc d;
  d.shape           = EmitterShape::Sphere;
  d.center          = source;
  d.sphere_radius   = 0.30f;
  d.emit_rate       = emit_rate;
  d.initial_velocity = {0.0f, 0.0f, 1.0f};   // slow rise
  d.velocity_jitter  = 0.6f;
  d.lifetime_seconds = 4.0f;                  // long lived
  d.lifetime_jitter  = 0.4f;
  d.initial_size     = 0.5f;
  d.size_jitter      = 0.3f;
  d.acceleration     = wind + glm::vec3(0.0f, 0.0f, 0.2f);  // wind + slight buoyancy
  d.drag             = 0.25f;
  d.size_growth      = 0.6f;                  // smoke expands
  // Gray ramp, mostly stable, fade alpha at end
  d.color_ramp.colors[0] = {0.55f, 0.55f, 0.55f, 0.6f};
  d.color_ramp.colors[1] = {0.40f, 0.40f, 0.42f, 0.7f};
  d.color_ramp.colors[2] = {0.25f, 0.25f, 0.28f, 0.55f};
  d.color_ramp.colors[3] = {0.15f, 0.15f, 0.18f, 0.0f};
  return d;
}

EmitterDesc makeSparks(glm::vec3 source, float emit_rate) {
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.center          = source;
  d.emit_rate       = emit_rate;
  d.initial_velocity = {0.0f, 0.0f, 4.0f};   // fast pop
  d.velocity_jitter  = 1.0f;                  // chaotic
  d.lifetime_seconds = 0.6f;                  // short
  d.lifetime_jitter  = 0.3f;
  d.initial_size     = 0.08f;                 // tiny
  d.size_jitter      = 0.2f;
  d.acceleration     = {0.0f, 0.0f, -9.8f};   // gravity
  d.drag             = 0.05f;
  d.size_growth      = -0.05f;                // slightly shrink as cooling
  d.color_ramp.colors[0] = {1.0f, 0.95f, 0.7f, 1.0f};
  d.color_ramp.colors[1] = {1.0f, 0.55f, 0.15f, 1.0f};
  d.color_ramp.colors[2] = {0.7f, 0.15f, 0.05f, 0.8f};
  d.color_ramp.colors[3] = {0.2f, 0.05f, 0.02f, 0.0f};
  return d;
}

EmitterDesc makeJet(glm::vec3 source, glm::vec3 direction, float emit_rate) {
  EmitterDesc d;
  d.shape           = EmitterShape::Cone;
  d.center          = source;
  d.cone_dir        = glm::normalize(direction);
  d.cone_half_angle = 0.25f;                  // ~14°
  d.emit_rate       = emit_rate;
  d.initial_velocity = direction * 8.0f;     // fast directional
  d.velocity_jitter  = 0.2f;
  d.lifetime_seconds = 0.8f;
  d.lifetime_jitter  = 0.2f;
  d.initial_size     = 0.25f;
  d.size_jitter      = 0.15f;
  d.acceleration     = {0.0f, 0.0f, 0.0f};
  d.drag             = 0.3f;                  // jet decelerates with air
  d.size_growth      = 0.3f;                  // mild flame stretching
  d.color_ramp.colors[0] = {1.0f, 0.95f, 0.9f, 1.0f};
  d.color_ramp.colors[1] = {1.0f, 0.7f,  0.2f, 1.0f};
  d.color_ramp.colors[2] = {0.7f, 0.2f,  0.05f, 0.85f};
  d.color_ramp.colors[3] = {0.1f, 0.05f, 0.05f, 0.0f};
  return d;
}

}  // namespace EmitterPresets

// =============================================================================
// ParticleSystemGPU — compute-shader 粒子推进
// =============================================================================

namespace {
// 双 buffer 布局 — 让 ParticleRenderer 直接用 render_buffer 当 instance VBO
// (stride=32B 跟 ParticleInstance 完全一致,共享同一 pipeline):
//   render_buffer:32B/inst = vec3 pos + float size + vec4 color
//   state_buffer: 32B/inst = vec3 vel + float age + float lifetime
//                                     + float size_growth + float drag + float pad
struct ParticleStateGPU {
  glm::vec3 velocity;
  float     age;
  float     lifetime;
  float     size_growth;
  float     drag;
  float     _pad;
};
static_assert(sizeof(ParticleStateGPU) == 32, "ParticleStateGPU 32B");

struct ParticleComputePush {
  glm::vec4 center_rate;          // .xyz center, .w emit_count_this_frame
  glm::vec4 init_vel_jitter;      // .xyz initial_velocity, .w velocity_jitter
  glm::vec4 accel_drag;           // .xyz acceleration, .w drag
  glm::vec4 lifetime_size;        // .x lifetime, .y life_jitter, .z size, .w size_jitter
  glm::vec4 shape_growth;         // .x size_growth, .y shape, .z half_ext.x/sphere_r, .w cone_half
  glm::vec4 half_ext_yz_cone;     // .xy half_ext.yz, .zw cone_dir.xy
  glm::vec4 cone_dir_z_dt_seed;   // .x cone_dir.z, .y dt, .z time, .w seed
};
static_assert(sizeof(ParticleComputePush) == 112, "ParticleComputePush 112B");

}  // namespace

bool ParticleSystemGPU::initialize(HongTu::RHI::RHIDevice* device, uint32_t capacity) {
  using namespace HongTu::RHI;
  device_   = device;
  capacity_ = capacity;

  // 1A. render_buffer(32B/inst)— Storage + Vertex,ParticleRenderer 当 instance VBO
  {
    BufferDesc bd;
    bd.size  = static_cast<uint64_t>(capacity) * sizeof(ParticleInstance);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Storage)
             | static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    render_buffer_ = device_->createBuffer(bd);
    if (!render_buffer_) return false;
    void* dst = render_buffer_->map();
    std::vector<ParticleInstance> init(capacity);
    for (auto& p : init) { p.size = 0.0f; p.color = glm::vec4(0.0f); }
    std::memcpy(dst, init.data(), bd.size);
    render_buffer_->unmap();
  }
  // 1B. state_buffer(32B/inst)— Storage only(vel/age/lifetime/...)
  {
    BufferDesc bd;
    bd.size  = static_cast<uint64_t>(capacity) * sizeof(ParticleStateGPU);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Storage);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible
                        | MemoryPropertyFlagBits::HostCoherent;
    state_buffer_ = device_->createBuffer(bd);
    if (!state_buffer_) return false;
    void* dst = state_buffer_->map();
    std::vector<ParticleStateGPU> init(capacity);
    for (auto& s : init) { s.age = 1.0f; s.lifetime = 0.5f; }
    std::memcpy(dst, init.data(), bd.size);
    state_buffer_->unmap();
  }

  // 2. Compute shader
  HongTu::Asset::ShaderCompiler compiler;
  // 注意:ShaderCompiler 在文件头自动注入 `#version 450\n#extension ...\n`,
  // 我们的源不要再写 #version,否则会变 double-#version → glslc 报
  // "'#version' must occur first in shader"
  const char* csrc = R"glsl(
    layout(local_size_x = 64) in;

    // 双 buffer 设计 — render 32B / state 32B,分开存
    struct ParticleRender {
      vec3  world_pos;
      float size;
      vec4  color;
    };
    struct ParticleState {
      vec3  velocity;
      float age;
      float lifetime;
      float size_growth;
      float drag;
      float _pad;
    };

    layout(set = 0, binding = 0) buffer RBuf { ParticleRender renders[]; };
    layout(set = 0, binding = 1) buffer SBuf { ParticleState  states[]; };

    layout(push_constant) uniform PC {
      vec4 center_rate;
      vec4 init_vel_jitter;
      vec4 accel_drag;
      vec4 lifetime_size;
      vec4 shape_growth;
      vec4 half_ext_yz_cone;
      vec4 cone_dir_z_dt_seed;
    } pc;

    // 简单 PRNG — 单 thread 独立(seed = base_seed ^ global_id ^ time)
    uint rng_state;
    void rng_seed(uint i) {
      rng_state = uint(pc.cone_dir_z_dt_seed.w)
                ^ (i * 0x85ebca6bu)
                ^ uint(pc.cone_dir_z_dt_seed.z * 1000.0);
    }
    float rand01() {
      rng_state ^= rng_state << 13;
      rng_state ^= rng_state >> 17;
      rng_state ^= rng_state << 5;
      return float(rng_state & 0xFFFFFFu) / 16777216.0;
    }

    vec3 randInShape() {
      int shape = int(pc.shape_growth.y + 0.5);
      vec3 center = pc.center_rate.xyz;
      if (shape == 0) return center;                        // Point
      if (shape == 1) {                                     // Box
        return center + vec3(rand01() * 2.0 - 1.0,
                              rand01() * 2.0 - 1.0,
                              rand01() * 2.0 - 1.0)
                       * vec3(pc.shape_growth.z,
                              pc.half_ext_yz_cone.x,
                              pc.half_ext_yz_cone.y);
      }
      if (shape == 2) {                                     // Sphere
        vec3 r = vec3(0);
        for (int i = 0; i < 8; ++i) {
          r = vec3(rand01() * 2.0 - 1.0,
                   rand01() * 2.0 - 1.0,
                   rand01() * 2.0 - 1.0);
          if (dot(r, r) <= 1.0) break;
        }
        return center + r * pc.shape_growth.z;
      }
      return center;                                        // Cone apex
    }

    vec3 randVel() {
      int shape = int(pc.shape_growth.y + 0.5);
      vec3 base = pc.init_vel_jitter.xyz;
      if (shape == 3) {  // Cone
        vec3 axis = normalize(vec3(pc.half_ext_yz_cone.z,
                                    pc.half_ext_yz_cone.w,
                                    pc.cone_dir_z_dt_seed.x));
        vec3 helper = abs(axis.z) < 0.9 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 tx = normalize(cross(axis, helper));
        vec3 ty = cross(axis, tx);
        float u1 = rand01(), u2 = rand01();
        float cos_t = 1.0 - u1 * (1.0 - cos(pc.shape_growth.w));
        float sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
        float phi = u2 * 6.28318;
        vec3 dir = axis * cos_t + tx * (sin_t * cos(phi)) + ty * (sin_t * sin(phi));
        return dir * length(base);
      }
      vec3 jitter = vec3(rand01()*2-1, rand01()*2-1, rand01()*2-1)
                  * (pc.init_vel_jitter.w * length(base));
      return base + jitter;
    }

    void main() {
      uint i = gl_GlobalInvocationID.x;
      if (i >= renders.length()) return;
      rng_seed(i);

      ParticleRender pr = renders[i];
      ParticleState  ps = states[i];
      uint emit_count = uint(pc.center_rate.w + 0.5);

      bool spawn = (i < emit_count) || (ps.age >= ps.lifetime);
      if (spawn && i >= emit_count) {
        // 死 + 没分配 spawn → 保持死
        pr.size = 0.0;
        pr.color.a = 0.0;
        renders[i] = pr;
        return;
      }
      if (spawn) {
        pr.world_pos = randInShape();
        ps.velocity  = randVel();
        ps.age       = 0.0;
        float lj = (rand01() * 2.0 - 1.0) * pc.lifetime_size.y;
        ps.lifetime  = max(0.01, pc.lifetime_size.x * (1.0 + lj));
        float sj = (rand01() * 2.0 - 1.0) * pc.lifetime_size.w;
        pr.size      = max(0.0, pc.lifetime_size.z * (1.0 + sj));
        ps.size_growth = pc.shape_growth.x;
        ps.drag        = pc.accel_drag.w;
      }

      // Advect
      float dt = pc.cone_dir_z_dt_seed.y;
      ps.age += dt;
      if (ps.age >= ps.lifetime) {
        pr.size = 0.0;
        pr.color.a = 0.0;
        renders[i] = pr;
        states[i]  = ps;
        return;
      }
      float drag_factor = max(0.0, 1.0 - ps.drag * dt);
      ps.velocity   = ps.velocity * drag_factor + pc.accel_drag.xyz * dt;
      pr.world_pos += ps.velocity * dt;
      pr.size      += ps.size_growth * dt;

      // Color ramp(硬编码 fire — 后续可以走 push 传 keyframe)
      float t = clamp(ps.age / max(0.001, ps.lifetime), 0.0, 0.9999);
      vec4 r0 = vec4(1.00, 0.95, 0.85, 1.0);
      vec4 r1 = vec4(1.00, 0.70, 0.20, 1.0);
      vec4 r2 = vec4(0.65, 0.18, 0.05, 0.85);
      vec4 r3 = vec4(0.05, 0.02, 0.02, 0.0);
      float scaled = t * 3.0;
      int seg = int(scaled);
      float u = scaled - float(seg);
      vec4 a = (seg == 0) ? r0 : ((seg == 1) ? r1 : r2);
      vec4 b = (seg == 0) ? r1 : ((seg == 1) ? r2 : r3);
      pr.color = mix(a, b, u);

      renders[i] = pr;
      states[i]  = ps;
    }
  )glsl";

  auto cr = compiler.compileGLSLToSPIRV(csrc, "main", "compute");
  if (!cr.success) {
    spdlog::error("ParticleSystemGPU compute shader fail: {}", cr.errorMessage);
    return false;
  }
  ShaderDesc sd;
  sd.stage = ShaderStageFlagBits::Compute;
  sd.spirvCode = cr.spirvCode;
  compute_shader_ = device_->createShader(sd);
  if (!compute_shader_) return false;

  // 3. Descriptor set layout: set=0 binding=0/1 = 两个 SSBO
  DescriptorSetLayoutDesc dl;
  for (uint32_t b = 0; b <= 1; ++b) {
    DescriptorSetLayoutBinding lb;
    lb.binding         = b;
    lb.descriptorCount = 1u;
    lb.stageFlags      = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Compute);
    lb.descriptorType  = DescriptorType::StorageBuffer;
    dl.bindings.push_back(lb);
  }
  desc_layout_ = device_->createDescriptorSetLayout(dl);
  if (!desc_layout_) return false;

  // 4. Descriptor set 绑两个 buffer
  DescriptorSetDesc dsd;
  {
    DescriptorBinding db;
    db.binding = 0u; db.type = DescriptorType::StorageBuffer;
    db.buffer = render_buffer_;
    dsd.bindings.push_back(db);
  }
  {
    DescriptorBinding db;
    db.binding = 1u; db.type = DescriptorType::StorageBuffer;
    db.buffer = state_buffer_;
    dsd.bindings.push_back(db);
  }
  desc_set_ = device_->createDescriptorSet(desc_layout_, dsd);
  if (!desc_set_) return false;

  // 5. Pipeline layout + compute pipeline
  PipelineLayoutDesc pll;
  pll.setLayouts.push_back(desc_layout_);
  PushConstantRange pcr;
  pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Compute);
  pcr.offset = 0;
  pcr.size   = sizeof(ParticleComputePush);
  pll.pushConstantRanges.push_back(pcr);
  pipeline_layout_ = device_->createPipelineLayout(pll);
  if (!pipeline_layout_) return false;

  ComputePipelineDesc cpd;
  cpd.shader         = compute_shader_;
  cpd.pipelineLayout = pipeline_layout_;
  pipeline_ = device_->createComputePipeline(cpd);
  return pipeline_ != nullptr;
}

void ParticleSystemGPU::recordUpdate(const HongTu::RHI::RHICommandBufferPtr& cmd, float dt) {
  using namespace HongTu::RHI;
  if (!pipeline_ || dt <= 0.0f) return;
  time_seconds_ += dt;
  ++frame_counter_;

  // CPU 端算本帧 emit_count
  uint32_t emit_count = 0;
  if (emitter_.enabled) {
    spawn_accumulator_ += emitter_.emit_rate * dt;
    emit_count = static_cast<uint32_t>(spawn_accumulator_);
    spawn_accumulator_ -= static_cast<float>(emit_count);
    if (emitter_.burst_remaining > 0.0f) {
      emit_count += static_cast<uint32_t>(emitter_.burst_remaining);
      emitter_.burst_remaining = 0.0f;
    }
    emit_count = std::min(emit_count, capacity_);
  }

  ParticleComputePush pc{};
  pc.center_rate          = glm::vec4(emitter_.center, static_cast<float>(emit_count));
  pc.init_vel_jitter      = glm::vec4(emitter_.initial_velocity, emitter_.velocity_jitter);
  pc.accel_drag           = glm::vec4(emitter_.acceleration, emitter_.drag);
  pc.lifetime_size        = glm::vec4(emitter_.lifetime_seconds, emitter_.lifetime_jitter,
                                       emitter_.initial_size, emitter_.size_jitter);
  pc.shape_growth         = glm::vec4(emitter_.size_growth,
                                       static_cast<float>(emitter_.shape),
                                       (emitter_.shape == EmitterShape::Sphere)
                                         ? emitter_.sphere_radius
                                         : emitter_.half_extent.x,
                                       emitter_.cone_half_angle);
  pc.half_ext_yz_cone     = glm::vec4(emitter_.half_extent.y, emitter_.half_extent.z,
                                       emitter_.cone_dir.x, emitter_.cone_dir.y);
  pc.cone_dir_z_dt_seed   = glm::vec4(emitter_.cone_dir.z, dt, time_seconds_,
                                       static_cast<float>(frame_counter_ * 1664525u + 1013904223u));

  cmd->bindPipeline(pipeline_);
  cmd->bindDescriptorSet(0u, desc_set_, pipeline_layout_, PipelineBindPoint::Compute);
  cmd->pushConstants(pipeline_layout_,
      static_cast<ShaderStageFlags>(ShaderStageFlagBits::Compute),
      0, sizeof(pc), &pc);

  const uint32_t local_size = 64u;
  uint32_t groups = (capacity_ + local_size - 1u) / local_size;
  cmd->dispatch(groups, 1u, 1u);

  // Barrier:compute write → vertex 阶段 read(render_buffer 给 ParticleRenderer 用)
  BufferMemoryBarrier bb;
  bb.srcAccessMask = static_cast<AccessFlags>(AccessFlagBits::ShaderWrite);
  bb.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::VertexAttributeRead);
  bb.buffer        = render_buffer_;
  bb.offset        = 0;
  bb.size          = 0;
  cmd->pipelineBarrier(
      static_cast<PipelineStageFlags>(PipelineStageFlagBits::ComputeShader),
      static_cast<PipelineStageFlags>(PipelineStageFlagBits::VertexInput),
      {}, {bb});
}

}  // namespace hongtu::renderer
