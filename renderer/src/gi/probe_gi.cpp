#include <hongtu/renderer/gi/probe_gi.hpp>
#include <cmath>
#include <algorithm>

namespace hongtu::renderer {

// ============================================================================
// Spherical Harmonics (L2)
// ============================================================================

std::array<f32, 9> shBasis(const glm::vec3& dir) {
  // L0
  f32 Y00 = 0.282095f; // 1/(2*sqrt(pi))
  // L1
  f32 Y1n1 = 0.488603f * dir.y;
  f32 Y10  = 0.488603f * dir.z;
  f32 Y1p1 = 0.488603f * dir.x;
  // L2
  f32 Y2n2 = 1.092548f * dir.x * dir.y;
  f32 Y2n1 = 1.092548f * dir.y * dir.z;
  f32 Y20  = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);
  f32 Y2p1 = 1.092548f * dir.x * dir.z;
  f32 Y2p2 = 0.546274f * (dir.x * dir.x - dir.y * dir.y);

  return {Y00, Y1n1, Y10, Y1p1, Y2n2, Y2n1, Y20, Y2p1, Y2p2};
}

glm::vec3 SH9::evaluate(const glm::vec3& dir) const {
  auto basis = shBasis(dir);
  glm::vec3 result(0.0f);
  for (int i = 0; i < 9; ++i) {
    result += coeffs[i] * basis[i];
  }
  return glm::max(result, glm::vec3(0.0f));
}

void SH9::addSample(const glm::vec3& dir, const glm::vec3& color, f32 weight) {
  auto basis = shBasis(dir);
  for (int i = 0; i < 9; ++i) {
    coeffs[i] += color * (basis[i] * weight);
  }
}

SH9 SH9::operator*(f32 s) const {
  SH9 result;
  for (int i = 0; i < 9; ++i) result.coeffs[i] = coeffs[i] * s;
  return result;
}

SH9 SH9::operator+(const SH9& other) const {
  SH9 result;
  for (int i = 0; i < 9; ++i) result.coeffs[i] = coeffs[i] + other.coeffs[i];
  return result;
}

void SH9::clear() {
  for (auto& c : coeffs) c = glm::vec3(0.0f);
}

// ============================================================================
// ProbeGrid
// ============================================================================

ProbeGrid::ProbeGrid(const ProbeGridConfig& config) : config_(config) {
  initialize();
}

void ProbeGrid::initialize() {
  u32 count = getProbeCount();
  probes_.resize(count);

  for (u32 i = 0; i < count; ++i) {
    auto grid = linearToGrid(i);
    probes_[i].position = probePosition(grid.x, grid.y, grid.z);
    probes_[i].valid = false;
    probes_[i].needs_update = true;
  }
}

void ProbeGrid::resize(const ProbeGridConfig& config) {
  config_ = config;
  initialize();
}

u32 ProbeGrid::getProbeCount() const {
  return static_cast<u32>(config_.grid_size.x * config_.grid_size.y * config_.grid_size.z);
}

u32 ProbeGrid::gridToLinear(int x, int y, int z) const {
  return static_cast<u32>(z * config_.grid_size.y * config_.grid_size.x +
                          y * config_.grid_size.x + x);
}

glm::ivec3 ProbeGrid::linearToGrid(u32 index) const {
  int x = static_cast<int>(index % config_.grid_size.x);
  int y = static_cast<int>((index / config_.grid_size.x) % config_.grid_size.y);
  int z = static_cast<int>(index / (config_.grid_size.x * config_.grid_size.y));
  return {x, y, z};
}

glm::vec3 ProbeGrid::probePosition(int x, int y, int z) const {
  return config_.grid_origin + glm::vec3(
      static_cast<f32>(x) * config_.grid_spacing.x,
      static_cast<f32>(y) * config_.grid_spacing.y,
      static_cast<f32>(z) * config_.grid_spacing.z);
}

LightProbe& ProbeGrid::getProbe(int x, int y, int z) {
  return probes_[gridToLinear(x, y, z)];
}

const LightProbe& ProbeGrid::getProbe(int x, int y, int z) const {
  return probes_[gridToLinear(x, y, z)];
}

LightProbe& ProbeGrid::getProbe(u32 index) { return probes_[index]; }
const LightProbe& ProbeGrid::getProbe(u32 index) const { return probes_[index]; }

bool ProbeGrid::findSurroundingProbes(const glm::vec3& world_pos,
                                       std::array<u32, 8>& probe_indices,
                                       glm::vec3& weights) const {
  glm::vec3 local = (world_pos - config_.grid_origin) /
                     glm::vec3(config_.grid_spacing);

  // Check bounds
  if (local.x < 0 || local.y < 0 || local.z < 0 ||
      local.x >= config_.grid_size.x - 1 ||
      local.y >= config_.grid_size.y - 1 ||
      local.z >= config_.grid_size.z - 1) {
    return false;
  }

  int ix = static_cast<int>(local.x);
  int iy = static_cast<int>(local.y);
  int iz = static_cast<int>(local.z);

  weights = glm::fract(local);

  // 8 corners of the cell
  probe_indices[0] = gridToLinear(ix,     iy,     iz);
  probe_indices[1] = gridToLinear(ix + 1, iy,     iz);
  probe_indices[2] = gridToLinear(ix,     iy + 1, iz);
  probe_indices[3] = gridToLinear(ix + 1, iy + 1, iz);
  probe_indices[4] = gridToLinear(ix,     iy,     iz + 1);
  probe_indices[5] = gridToLinear(ix + 1, iy,     iz + 1);
  probe_indices[6] = gridToLinear(ix,     iy + 1, iz + 1);
  probe_indices[7] = gridToLinear(ix + 1, iy + 1, iz + 1);

  return true;
}

glm::vec3 ProbeGrid::sampleIrradiance(const glm::vec3& world_pos,
                                       const glm::vec3& normal) const {
  std::array<u32, 8> indices{};
  glm::vec3 weights;

  if (!findSurroundingProbes(world_pos, indices, weights)) {
    return glm::vec3(0.0f);
  }

  // Trilinear interpolation of SH-evaluated irradiance
  f32 wx = weights.x, wy = weights.y, wz = weights.z;

  auto sample = [&](u32 idx) -> glm::vec3 {
    const auto& probe = probes_[idx];
    if (!probe.valid) return glm::vec3(0.0f);
    return probe.irradiance.evaluate(normal);
  };

  // Trilinear blend
  glm::vec3 c000 = sample(indices[0]) * (1 - wx) * (1 - wy) * (1 - wz);
  glm::vec3 c100 = sample(indices[1]) * wx       * (1 - wy) * (1 - wz);
  glm::vec3 c010 = sample(indices[2]) * (1 - wx) * wy       * (1 - wz);
  glm::vec3 c110 = sample(indices[3]) * wx       * wy       * (1 - wz);
  glm::vec3 c001 = sample(indices[4]) * (1 - wx) * (1 - wy) * wz;
  glm::vec3 c101 = sample(indices[5]) * wx       * (1 - wy) * wz;
  glm::vec3 c011 = sample(indices[6]) * (1 - wx) * wy       * wz;
  glm::vec3 c111 = sample(indices[7]) * wx       * wy       * wz;

  return c000 + c100 + c010 + c110 + c001 + c101 + c011 + c111;
}

std::vector<u32> ProbeGrid::getProbesForUpdate(u32 max_count, u32 frame) const {
  std::vector<u32> update_list;
  u32 total = getProbeCount();
  if (total == 0) return update_list;

  // Round-robin: start from frame offset, wrap around
  u32 start = frame % total;
  for (u32 i = 0; i < std::min(max_count, total); ++i) {
    u32 idx = (start + i) % total;
    update_list.push_back(idx);
  }
  return update_list;
}

void ProbeGrid::invalidateAll() {
  for (auto& p : probes_) {
    p.needs_update = true;
    p.valid = false;
  }
}

void ProbeGrid::getBounds(glm::vec3& min_bound, glm::vec3& max_bound) const {
  min_bound = config_.grid_origin;
  max_bound = config_.grid_origin + glm::vec3(
      (config_.grid_size.x - 1) * config_.grid_spacing.x,
      (config_.grid_size.y - 1) * config_.grid_spacing.y,
      (config_.grid_size.z - 1) * config_.grid_spacing.z);
}

} // namespace hongtu::renderer
