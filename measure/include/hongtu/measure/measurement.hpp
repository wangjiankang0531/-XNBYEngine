#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <vector>

namespace hongtu::measure {

// =============================================================================
// 测量模块(引擎层,纯几何) —— 直线 / 高度 / 面积 / 点坐标。
//
// 与渲染/UI 解耦:输入是世界坐标点(geo 模式 = ECEF 米,本地模式 = 平地米),
// 输出是数值结果。编辑器/云渲染/demo 各自做交互+叠加,共用本模块的计算。
// =============================================================================

enum class MeasureKind {
  Distance,  ///< 多点折线:总长 + 每段长(geo 给地表测地线累加 + 两点斜距)
  Height,    ///< 两点:高差 + 水平距离 + 斜距
  Area,      ///< 多边形:面积 + 周长
  Point,     ///< 单点:geo 经/纬/高,本地 x/y/z
};

struct MeasureResult {
  bool   valid       = false;
  double total_len   = 0.0;   ///< Distance 折线总长;Area 周长(米)
  double slant_len   = 0.0;   ///< Distance(=2点)/Height:3D 斜距(米)
  double area        = 0.0;   ///< Area:面积(平方米)
  double height      = 0.0;   ///< Height:高差(米,可负)
  double horizontal  = 0.0;   ///< Height:水平距离(米)
  std::vector<double> seg_len;  ///< Distance/Area:每段长(米)

  // Point readout(只有 Point 有效):
  double lat_deg = 0.0, lon_deg = 0.0, alt_m = 0.0;  ///< geo
  glm::dvec3 local{0.0};                              ///< 本地 x/y/z
};

/// 计算测量结果。`pts` 是世界坐标(geo=ECEF / local=米),`geo` 区分两种。
MeasureResult compute(MeasureKind kind, const std::vector<glm::dvec3>& pts, bool geo);

/// 两点「地表」距离(米)。geo=true:ECEF→经纬度后 Haversine 大圆距离;
/// geo=false:本地水平(XY 平面)欧氏距离。
double groundDistance(const glm::dvec3& a, const glm::dvec3& b, bool geo);

/// Terrain-referenced cut/fill volume options. The footprint is expressed in a
/// local metric XY frame (normally the EN plane of a WGS84 ENU anchor).
struct TerrainVolumeOptions {
  double reference_elevation_m = 0.0;  ///< Design/reference plane elevation.
  double sample_spacing_m = 10.0;      ///< Maximum XY grid cell width/height.
  std::size_t maximum_cells = 1'000'000;
};

struct TerrainVolumeResult {
  bool valid = false;
  double footprint_area_m2 = 0.0;
  double sampled_area_m2 = 0.0;
  double cut_volume_m3 = 0.0;   ///< Terrain above the reference plane.
  double fill_volume_m3 = 0.0;  ///< Terrain below the reference plane.
  double net_volume_m3 = 0.0;   ///< cut_volume_m3 - fill_volume_m3.
  double reference_elevation_m = 0.0;
  double min_terrain_elevation_m = 0.0;
  double max_terrain_elevation_m = 0.0;
  std::size_t grid_cell_count = 0;
  std::size_t contributing_cell_count = 0;
  std::size_t terrain_sample_count = 0;
};

using TerrainHeightSampler = std::function<double(double x_m, double y_m)>;

/// Integrate terrain cut/fill volume inside a simple (convex or concave)
/// polygon. The polygon is triangulated, clipped against a regular metric grid,
/// and each clipped fragment is sampled at its centroid. Constant and planar
/// height fields integrate exactly; curved/DEM fields converge as spacing is
/// reduced. Invalid/self-intersecting polygons, non-finite samples, or cell
/// budgets exceeded return valid=false.
TerrainVolumeResult computeTerrainVolume(const std::vector<glm::dvec2>& footprint,
                                         const TerrainVolumeOptions& options,
                                         const TerrainHeightSampler& terrain_height);

}  // namespace hongtu::measure
