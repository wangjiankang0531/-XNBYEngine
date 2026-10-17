#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace hongtu::renderer {

/// Post-processing effect type.
enum class PostEffectType {
  ToneMapping,
  Bloom,
  DepthOfField,
  MotionBlur,
  ColorGrading,
  Vignette,
  ChromaticAberration,
  FilmGrain,
  FXAA,
  // Count
  Count
};

/// Get string name for a post-processing effect type.
const char* postEffectTypeName(PostEffectType type);

/// Parse post-processing effect type from string.
PostEffectType postEffectTypeFromName(const std::string& name);

/// Tone mapping operator types.
enum class ToneMappingOperator {
  ACES,           ///< Academy Color Encoding System
  Reinhard,       ///< Reinhard simple
  Filmic,         ///< Uncharted 2 filmic
  AgX,            ///< AgX tone mapping
};

/// Parameters for individual post-processing effects.
struct ToneMappingParams {
  ToneMappingOperator op = ToneMappingOperator::ACES;
  f32 exposure = 1.0f;
  f32 gamma = 2.2f;
  f32 white_point = 4.0f;
};

struct BloomParams {
  f32 threshold = 1.0f;        ///< Brightness threshold for bloom
  f32 intensity = 0.5f;        ///< Bloom intensity
  f32 radius = 5.0f;           ///< Bloom blur radius
  u32 mip_levels = 5;          ///< Number of downscale passes
};

struct DepthOfFieldParams {
  f32 focus_distance = 10.0f;  ///< Focus plane distance
  f32 focus_range = 5.0f;      ///< Range around focus that's sharp
  f32 bokeh_radius = 5.0f;     ///< Maximum blur radius
};

struct MotionBlurParams {
  f32 intensity = 1.0f;
  u32 samples = 8;
};

struct ColorGradingParams {
  glm::vec3 lift = {0, 0, 0};        ///< Shadows color adjustment
  glm::vec3 gamma = {1, 1, 1};       ///< Midtones color adjustment
  glm::vec3 gain = {1, 1, 1};        ///< Highlights color adjustment
  f32 saturation = 1.0f;
  f32 contrast = 1.0f;
  f32 temperature = 0.0f;            ///< Color temperature shift
};

struct VignetteParams {
  f32 intensity = 0.3f;
  f32 smoothness = 0.5f;
  f32 roundness = 1.0f;
};

/// A single effect slot in the pipeline.
struct PostEffectSlot {
  PostEffectType type;
  bool enabled = true;
  // Union-like storage for parameters
  ToneMappingParams tone_mapping;
  BloomParams bloom;
  DepthOfFieldParams dof;
  MotionBlurParams motion_blur;
  ColorGradingParams color_grading;
  VignetteParams vignette;
};

/// Configurable post-processing pipeline.
/// Manages a chain of effects that can be reordered, enabled/disabled, and serialized.
class PostProcessPipeline {
 public:
  PostProcessPipeline();

  /// Add an effect to the end of the pipeline. Returns slot index.
  u32 addEffect(PostEffectType type);

  /// Insert an effect at a specific position.
  void insertEffect(u32 position, PostEffectType type);

  /// Remove an effect at a specific position.
  void removeEffect(u32 position);

  /// Move an effect from one position to another.
  void moveEffect(u32 from, u32 to);

  /// Enable/disable an effect.
  void setEffectEnabled(u32 position, bool enabled);
  bool isEffectEnabled(u32 position) const;

  /// Get effect at position.
  PostEffectSlot& getEffect(u32 position);
  const PostEffectSlot& getEffect(u32 position) const;

  /// Find first effect of given type. Returns -1 if not found.
  i32 findEffect(PostEffectType type) const;

  /// Number of effects in the pipeline.
  u32 getEffectCount() const { return static_cast<u32>(effects_.size()); }

  /// Get all effects (read-only).
  const std::vector<PostEffectSlot>& getEffects() const { return effects_; }

  /// Get list of enabled effects in order.
  std::vector<PostEffectType> getEnabledEffects() const;

  /// Create a default pipeline with common effects.
  static PostProcessPipeline createDefault();

  /// Serialize to JSON.
  nlohmann::json toJson() const;

  /// Deserialize from JSON.
  void fromJson(const nlohmann::json& j);

  /// Clear all effects.
  void clear() { effects_.clear(); }

 private:
  std::vector<PostEffectSlot> effects_;
};

// ============================================================================
// ACES Tone Mapping (CPU reference implementation for validation)
// ============================================================================

/// Apply ACES filmic tone mapping to a single color value.
/// Input: linear HDR color. Output: mapped SDR color [0, 1].
glm::vec3 acesToneMap(const glm::vec3& hdr_color, f32 exposure = 1.0f);

/// Apply Reinhard tone mapping.
glm::vec3 reinhardToneMap(const glm::vec3& hdr_color, f32 white_point = 4.0f);

} // namespace hongtu::renderer
