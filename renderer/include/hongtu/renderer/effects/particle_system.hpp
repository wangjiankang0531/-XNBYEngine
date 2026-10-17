#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

// =============================================================================
// Particle data — SoA layout for cache efficiency on CPU advection.
// Coordinate-agnostic: all positions / velocities live in whatever space the
// caller wants (typically RTC). EmitterEntity (see scene/) lowers a world
// position to RTC before feeding it here.
// =============================================================================

/// One emitter spawn shape — controls where new particles appear.
enum class EmitterShape : uint32_t {
  Point,    ///< all spawn at center
  Box,      ///< uniform in axis-aligned box of `half_extent`
  Sphere,   ///< uniform in sphere of `sphere_radius`
  Cone,     ///< from apex, along `cone_dir`, half-angle `cone_half_angle`
};

/// Per-emitter configuration. Tweakable at runtime — for example switching
/// `preset_color_ramp` flips fire ↔ smoke.
struct EmitterDesc {
  // Spawn shape
  EmitterShape shape           = EmitterShape::Point;
  glm::vec3    center          = {0.0f, 0.0f, 0.0f};   ///< RTC position of the source
  glm::vec3    half_extent     = {0.5f, 0.5f, 0.5f};   ///< box only
  float        sphere_radius   = 0.5f;                  ///< sphere only
  glm::vec3    cone_dir        = {0.0f, 0.0f, 1.0f};   ///< cone only (unit)
  float        cone_half_angle = 0.3f;                  ///< radians, cone only

  /// Rotation applied to `initial_velocity` and `cone_dir` at spawn time
  /// (ZYX Euler in degrees, same convention as `ModelInstance.rotation_euler_deg`).
  /// Lets a fire emitter point sideways or upside-down without rewriting
  /// presets. `center` / `half_extent` / `sphere_radius` are unaffected.
  glm::vec3    rotation_euler_deg = {0.0f, 0.0f, 0.0f};

  // Emission
  float emit_rate         = 100.0f;   ///< particles per second
  bool  enabled           = true;
  float burst_remaining   = 0.0f;     ///< if > 0, emit this many (one-shot burst)

  // Per-particle initial state
  glm::vec3 initial_velocity   = {0.0f, 0.0f, 2.0f};   ///< base direction × speed
  float     velocity_jitter    = 0.5f;                  ///< 0..1 random scale on velocity
  float     lifetime_seconds   = 1.5f;
  float     lifetime_jitter    = 0.3f;                  ///< 0..1
  float     initial_size       = 0.4f;                  ///< world meters
  float     size_jitter        = 0.2f;
  glm::vec3 acceleration       = {0.0f, 0.0f, 0.0f};   ///< gravity / wind
  float     drag               = 0.05f;                 ///< 1/s exponential drag
  float     size_growth        = 0.5f;                  ///< m/s, additive size over time

  // Color ramp — 4 keyframes evaluated by `age / lifetime`. Particle color is
  // a linear blend across these. Set ramp.colors[*] = same vec4 to disable.
  struct ColorRamp {
    glm::vec4 colors[4] = {
      {1.0f, 0.95f, 0.85f, 1.0f},   // t=0    bright white
      {1.0f, 0.6f,  0.2f,  1.0f},   // t=0.33 yellow
      {0.6f, 0.15f, 0.05f, 0.7f},   // t=0.66 dark red
      {0.1f, 0.05f, 0.05f, 0.0f},   // t=1    near-black + fade
    };
  };
  ColorRamp color_ramp;
};

/// One frame's-worth of pre-built GPU instance data, derived from the live
/// particle pool. Caller uploads this slice to a per-frame instance VBO and
/// invokes `ParticleRenderer::record(cmd, params, instances)`.
struct ParticleInstance {
  glm::vec3 world_pos;   ///< RTC space
  float     size;
  glm::vec4 color;       ///< pre-multiplied by lifetime fade
};
static_assert(sizeof(ParticleInstance) == 32,
              "ParticleInstance layout must be 32 bytes (tight pack)");

// =============================================================================
// ParticleSystem — owns one particle pool + one emitter.
// CPU update path: every `update(dt)` advances ages, advects positions,
// culls dead, and spawns new from emitter. Coordinate-agnostic — caller
// passes an emitter `center` already lowered to RTC.
//
// Design choice: ONE emitter per ParticleSystem. To play multiple effects
// at once, create multiple ParticleSystem instances. This keeps the
// per-frame instance buffer single-batched and the spawn math local.
// =============================================================================

class ParticleSystem {
 public:
  ParticleSystem() = default;
  explicit ParticleSystem(uint32_t max_particles);

  /// Resize the pool. Existing particles are clipped if `max < current`.
  /// Cheap to call once at startup; not intended for per-frame use.
  void setCapacity(uint32_t max_particles);
  uint32_t getCapacity() const { return capacity_; }

  /// Live particle count (after the latest `update`).
  uint32_t getAliveCount() const { return alive_count_; }

  /// Replace the emitter config. Existing live particles keep their current
  /// state; only future spawns use the new desc.
  void setEmitter(const EmitterDesc& desc) { emitter_ = desc; }
  const EmitterDesc& getEmitter() const { return emitter_; }
  EmitterDesc& getEmitter()             { return emitter_; }

  /// Wipe all live particles and reset accumulators.
  void reset();

  /// Advance the simulation by `dt` seconds. Spawns from emitter, advects
  /// positions, kills dead. Returns the new alive count.
  uint32_t update(float dt);

  /// Pack live particles into the GPU instance layout. `out` is cleared
  /// then filled. Sorted by descending `world_pos.z` heuristic (so far-
  /// away particles draw first under alpha blend) — *not* a true camera-
  /// space sort; good enough for vertical fires/smoke and cheap.
  void buildInstanceBuffer(std::vector<ParticleInstance>& out) const;

  // ---------------------------------------------------------------------------
  // Read-only access to SoA pool for testing / debug.
  // ---------------------------------------------------------------------------
  const std::vector<glm::vec3>& getPositions() const { return positions_; }
  const std::vector<glm::vec3>& getVelocities() const { return velocities_; }
  const std::vector<float>&     getAges() const      { return ages_; }
  const std::vector<float>&     getLifetimes() const { return lifetimes_; }
  const std::vector<float>&     getSizes() const     { return sizes_; }

 private:
  // SoA pool. Indices 0..alive_count_-1 are live; rest is free slots.
  // When a particle dies, we swap it with `positions_[alive_count_-1]` and
  // decrement count — O(1) cull.
  std::vector<glm::vec3> positions_;
  std::vector<glm::vec3> velocities_;
  std::vector<float>     ages_;        ///< seconds since spawn
  std::vector<float>     lifetimes_;   ///< total seconds before death
  std::vector<float>     sizes_;       ///< current size in meters

  uint32_t capacity_    = 0;
  uint32_t alive_count_ = 0;

  EmitterDesc emitter_;
  float       spawn_accumulator_ = 0.0f;  ///< fractional particles owed to emit_rate

  // RNG state — deterministic per-system so tests are reproducible.
  mutable uint64_t rng_state_ = 0x123456789ABCDEFULL;

  // Spawn one particle. Picks position+velocity from emitter shape.
  // Returns false if pool is full.
  bool spawnOne();
  // Internal helpers (defined in cpp).
  float    rand01() const;
  glm::vec3 randPointInShape() const;
  glm::vec3 randInitialVelocity() const;
};

// =============================================================================
// EmitterPresets — factory of `EmitterDesc` for the 4 phase-2 stock effects.
// Demos do `system.setEmitter(makeFire(...))` then `update + buildInstanceBuffer`.
// =============================================================================

namespace EmitterPresets {

/// Fire: black-body warm ramp (white→yellow→red→black), upward bias, short
/// lifetime, slight turbulence via drag.
EmitterDesc makeFire(glm::vec3 source, float emit_rate = 80.0f);

/// Smoke: long-lived gray particles, slow rise with wind drift, expanding.
EmitterDesc makeSmoke(glm::vec3 source, glm::vec3 wind = {0.5f, 0.0f, 0.0f},
                      float emit_rate = 40.0f);

/// Sparks / debris: small high-velocity particles with gravity, short life.
EmitterDesc makeSparks(glm::vec3 source, float emit_rate = 150.0f);

/// Jet fire: directional cone (for vapor jet / pressure release fires).
EmitterDesc makeJet(glm::vec3 source, glm::vec3 direction, float emit_rate = 200.0f);

}  // namespace EmitterPresets

}  // namespace hongtu::renderer

// =============================================================================
// ParticleSystemGPU declaration — needs RHI types,延后到 #include 之后。
// =============================================================================

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

namespace hongtu::renderer {

/// GPU-compute backend for `ParticleSystem`. Same conceptual model(SoA
/// pool + EmitterDesc),但池在 device-local SSBO,update 走 compute shader
/// dispatch — 没有 CPU loop,没有 per-frame instance upload。
///
/// 工作流:
///   gpu_ps.setEmitter(makeFire(...));
///   gpu_ps.recordUpdate(cmd, dt);                  // compute pass(在 render pass 之外)
///   particle_renderer.recordFromBuffer(cmd, p,     // 复用同一 SSBO 当 instance VBO
///                                       gpu_ps.getInstanceBuffer(),
///                                       gpu_ps.getCapacity());
///
/// 适合大规模粒子(>1 万)。<1 千 stick 用 CPU `ParticleSystem` 更简单。
class ParticleSystemGPU {
 public:
  bool initialize(HongTu::RHI::RHIDevice* device, uint32_t capacity);

  void setEmitter(const EmitterDesc& desc) { emitter_ = desc; }
  const EmitterDesc& getEmitter() const { return emitter_; }
  EmitterDesc&       getEmitter()       { return emitter_; }
  uint32_t getCapacity() const { return capacity_; }

  /// Compute pass dispatch — 必须在任何 render pass 之外。
  /// 内部走 2 个 pass:spawn pass(写新粒子)+ advect pass(全员推进 + cull)。
  void recordUpdate(const HongTu::RHI::RHICommandBufferPtr& cmd, float dt);

  /// Render buffer(32B/inst,与 ParticleInstance 布局完全一致)— 直接
  /// 当 ParticleRenderer 的 instance VBO,无拷贝。
  HongTu::RHI::RHIBufferPtr getInstanceBuffer() const { return render_buffer_; }

 private:
  HongTu::RHI::RHIDevice*                    device_ = nullptr;
  uint32_t                                   capacity_     = 0;
  EmitterDesc                                emitter_;
  float                                      spawn_accumulator_ = 0.0f;
  float                                      time_seconds_      = 0.0f;
  uint32_t                                   frame_counter_     = 0;

  // 双 buffer:render(对外可见)+ state(compute-only)
  HongTu::RHI::RHIBufferPtr                  render_buffer_;
  HongTu::RHI::RHIBufferPtr                  state_buffer_;
  HongTu::RHI::RHIShaderPtr                  compute_shader_;
  HongTu::RHI::RHIDescriptorSetLayoutPtr     desc_layout_;
  HongTu::RHI::RHIDescriptorSetPtr           desc_set_;
  HongTu::RHI::RHIPipelineLayoutPtr          pipeline_layout_;
  HongTu::RHI::RHIPipelinePtr                pipeline_;
};

}  // namespace hongtu::renderer
