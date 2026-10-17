#include <hongtu/renderer/postprocessing/post_process_pipeline.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace hongtu::renderer {

static const char* kEffectNames[] = {
    "ToneMapping", "Bloom", "DepthOfField", "MotionBlur",
    "ColorGrading", "Vignette", "ChromaticAberration", "FilmGrain", "FXAA"};

const char* postEffectTypeName(PostEffectType type) {
  auto idx = static_cast<int>(type);
  if (idx >= 0 && idx < static_cast<int>(PostEffectType::Count)) {
    return kEffectNames[idx];
  }
  return "Unknown";
}

PostEffectType postEffectTypeFromName(const std::string& name) {
  for (int i = 0; i < static_cast<int>(PostEffectType::Count); ++i) {
    if (name == kEffectNames[i]) return static_cast<PostEffectType>(i);
  }
  return PostEffectType::ToneMapping;
}

// ============================================================================
// PostProcessPipeline
// ============================================================================

PostProcessPipeline::PostProcessPipeline() = default;

u32 PostProcessPipeline::addEffect(PostEffectType type) {
  PostEffectSlot slot;
  slot.type = type;
  slot.enabled = true;
  effects_.push_back(slot);
  return static_cast<u32>(effects_.size() - 1);
}

void PostProcessPipeline::insertEffect(u32 position, PostEffectType type) {
  PostEffectSlot slot;
  slot.type = type;
  slot.enabled = true;
  auto pos = std::min(static_cast<size_t>(position), effects_.size());
  effects_.insert(effects_.begin() + static_cast<ptrdiff_t>(pos), slot);
}

void PostProcessPipeline::removeEffect(u32 position) {
  if (position < effects_.size()) {
    effects_.erase(effects_.begin() + static_cast<ptrdiff_t>(position));
  }
}

void PostProcessPipeline::moveEffect(u32 from, u32 to) {
  if (from >= effects_.size() || to >= effects_.size() || from == to) return;

  auto slot = effects_[from];
  effects_.erase(effects_.begin() + static_cast<ptrdiff_t>(from));
  auto insert_pos = std::min(static_cast<size_t>(to), effects_.size());
  effects_.insert(effects_.begin() + static_cast<ptrdiff_t>(insert_pos), slot);
}

void PostProcessPipeline::setEffectEnabled(u32 position, bool enabled) {
  if (position < effects_.size()) {
    effects_[position].enabled = enabled;
  }
}

bool PostProcessPipeline::isEffectEnabled(u32 position) const {
  return position < effects_.size() && effects_[position].enabled;
}

PostEffectSlot& PostProcessPipeline::getEffect(u32 position) {
  return effects_[position];
}

const PostEffectSlot& PostProcessPipeline::getEffect(u32 position) const {
  return effects_[position];
}

i32 PostProcessPipeline::findEffect(PostEffectType type) const {
  for (u32 i = 0; i < static_cast<u32>(effects_.size()); ++i) {
    if (effects_[i].type == type) return static_cast<i32>(i);
  }
  return -1;
}

std::vector<PostEffectType> PostProcessPipeline::getEnabledEffects() const {
  std::vector<PostEffectType> result;
  for (const auto& slot : effects_) {
    if (slot.enabled) result.push_back(slot.type);
  }
  return result;
}

PostProcessPipeline PostProcessPipeline::createDefault() {
  PostProcessPipeline pipeline;
  pipeline.addEffect(PostEffectType::Bloom);
  pipeline.addEffect(PostEffectType::ToneMapping);
  pipeline.addEffect(PostEffectType::ColorGrading);
  pipeline.addEffect(PostEffectType::Vignette);
  pipeline.addEffect(PostEffectType::FXAA);
  return pipeline;
}

nlohmann::json PostProcessPipeline::toJson() const {
  nlohmann::json j = nlohmann::json::array();
  for (const auto& slot : effects_) {
    nlohmann::json effect;
    effect["type"] = postEffectTypeName(slot.type);
    effect["enabled"] = slot.enabled;

    switch (slot.type) {
      case PostEffectType::ToneMapping:
        effect["operator"] = static_cast<int>(slot.tone_mapping.op);
        effect["exposure"] = slot.tone_mapping.exposure;
        effect["gamma"] = slot.tone_mapping.gamma;
        break;
      case PostEffectType::Bloom:
        effect["threshold"] = slot.bloom.threshold;
        effect["intensity"] = slot.bloom.intensity;
        effect["radius"] = slot.bloom.radius;
        effect["mip_levels"] = slot.bloom.mip_levels;
        break;
      case PostEffectType::ColorGrading:
        effect["saturation"] = slot.color_grading.saturation;
        effect["contrast"] = slot.color_grading.contrast;
        effect["temperature"] = slot.color_grading.temperature;
        break;
      case PostEffectType::Vignette:
        effect["intensity"] = slot.vignette.intensity;
        effect["smoothness"] = slot.vignette.smoothness;
        break;
      default:
        break;
    }
    j.push_back(effect);
  }
  return j;
}

void PostProcessPipeline::fromJson(const nlohmann::json& j) {
  effects_.clear();
  for (const auto& item : j) {
    PostEffectSlot slot;
    slot.type = postEffectTypeFromName(item.at("type").get<std::string>());
    slot.enabled = item.value("enabled", true);

    switch (slot.type) {
      case PostEffectType::ToneMapping:
        slot.tone_mapping.op = static_cast<ToneMappingOperator>(item.value("operator", 0));
        slot.tone_mapping.exposure = item.value("exposure", 1.0f);
        slot.tone_mapping.gamma = item.value("gamma", 2.2f);
        break;
      case PostEffectType::Bloom:
        slot.bloom.threshold = item.value("threshold", 1.0f);
        slot.bloom.intensity = item.value("intensity", 0.5f);
        slot.bloom.radius = item.value("radius", 5.0f);
        slot.bloom.mip_levels = item.value("mip_levels", 5u);
        break;
      case PostEffectType::ColorGrading:
        slot.color_grading.saturation = item.value("saturation", 1.0f);
        slot.color_grading.contrast = item.value("contrast", 1.0f);
        slot.color_grading.temperature = item.value("temperature", 0.0f);
        break;
      case PostEffectType::Vignette:
        slot.vignette.intensity = item.value("intensity", 0.3f);
        slot.vignette.smoothness = item.value("smoothness", 0.5f);
        break;
      default:
        break;
    }
    effects_.push_back(slot);
  }
}

// ============================================================================
// Tone Mapping CPU Reference
// ============================================================================

glm::vec3 acesToneMap(const glm::vec3& hdr_color, f32 exposure) {
  glm::vec3 c = hdr_color * exposure;
  // ACES approximation by Krzysztof Narkowicz
  const f32 a = 2.51f;
  const f32 b = 0.03f;
  const f32 cc = 2.43f;
  const f32 d = 0.59f;
  const f32 e = 0.14f;

  auto mapped = (c * (a * c + b)) / (c * (cc * c + d) + e);
  return glm::clamp(mapped, 0.0f, 1.0f);
}

glm::vec3 reinhardToneMap(const glm::vec3& hdr_color, f32 white_point) {
  f32 wp2 = white_point * white_point;
  auto mapped = hdr_color * (1.0f + hdr_color / wp2) / (1.0f + hdr_color);
  return glm::clamp(mapped, 0.0f, 1.0f);
}

} // namespace hongtu::renderer
