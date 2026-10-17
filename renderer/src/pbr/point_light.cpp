#include <hongtu/renderer/pbr/point_light.hpp>

#include <algorithm>

namespace hongtu::renderer {

std::vector<PointLight> selectNearestLights(const glm::vec3& camera_pos,
                                            const std::vector<PointLight>& all,
                                            int max) {
  std::vector<PointLight> valid;
  valid.reserve(all.size());
  for (const auto& l : all) {
    if (l.intensity > 0.0f && l.radius > 0.0f) valid.push_back(l);
  }
  if (max < 0) max = 0;
  auto closer = [&](const PointLight& a, const PointLight& b) {
    return glm::dot(a.position - camera_pos, a.position - camera_pos) <
           glm::dot(b.position - camera_pos, b.position - camera_pos);
  };
  if (static_cast<int>(valid.size()) <= max) {
    std::sort(valid.begin(), valid.end(), closer);
    return valid;
  }
  std::partial_sort(valid.begin(), valid.begin() + max, valid.end(), closer);
  valid.resize(max);
  return valid;
}

}  // namespace hongtu::renderer
