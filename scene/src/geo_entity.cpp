#include <hongtu/scene/geo_entity.hpp>

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::scene {

void GeoEntity::setPosition(const Geodetic& pos) { position_ = pos; }

Geodetic GeoEntity::getPosition() const { return position_; }

void GeoEntity::setScale(f32 uniform) { scale_ = glm::vec3(uniform); }

void GeoEntity::setScale(const glm::vec3& s) { scale_ = s; }

void GeoEntity::setHeading(f32 degrees) { heading_degrees_ = degrees; }

glm::dmat4 GeoEntity::getModelMatrix(const Ellipsoid& ellipsoid) const {
  using namespace hongtu::coordinate;

  // Step 1: Convert geodetic to ECEF position
  glm::dvec3 ecef = geodeticToECEF(position_, ellipsoid);

  // Step 2: Build ENU basis vectors at the geodetic position
  f64 sin_lat = std::sin(position_.lat);
  f64 cos_lat = std::cos(position_.lat);
  f64 sin_lon = std::sin(position_.lon);
  f64 cos_lon = std::cos(position_.lon);

  glm::dvec3 east(-sin_lon, cos_lon, 0.0);
  glm::dvec3 north(-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat);
  glm::dvec3 up(cos_lat * cos_lon, cos_lat * sin_lon, sin_lat);

  // Step 3: Construct 4x4 matrix with ENU columns + ECEF translation
  glm::dmat4 enu_matrix(1.0);
  enu_matrix[0] = glm::dvec4(east, 0.0);
  enu_matrix[1] = glm::dvec4(north, 0.0);
  enu_matrix[2] = glm::dvec4(up, 0.0);
  enu_matrix[3] = glm::dvec4(ecef, 1.0);

  // Step 4: Apply heading rotation around Z (up) axis in ENU space
  if (heading_degrees_ != 0.0f) {
    f64 heading_rad = glm::radians(static_cast<f64>(heading_degrees_));
    glm::dmat4 heading_rot = glm::rotate(glm::dmat4(1.0), heading_rad, glm::dvec3(0.0, 0.0, 1.0));
    enu_matrix = enu_matrix * heading_rot;
  }

  // Step 5: Apply scale
  glm::dmat4 scale_mat = glm::scale(glm::dmat4(1.0), glm::dvec3(scale_));
  enu_matrix = enu_matrix * scale_mat;

  return enu_matrix;
}

glm::mat4 GeoEntity::getRTCModelMatrix(const RTCOrigin& rtc_origin,
                                        const Ellipsoid& ellipsoid) const {
  using namespace hongtu::coordinate;

  // Step 1: Get the full ECEF model matrix
  glm::dmat4 model = getModelMatrix(ellipsoid);

  // Step 2: Convert ECEF position to RTC-relative float32 position
  glm::dvec3 ecef_pos = geodeticToECEF(position_, ellipsoid);
  glm::vec3 rtc_pos = rtc_origin.ecefToRTC(ecef_pos);

  // Step 3: Cast rotation/scale part to float32, use RTC translation
  glm::mat4 rtc_model;
  for (int col = 0; col < 3; ++col) {
    for (int row = 0; row < 4; ++row) {
      rtc_model[col][row] = static_cast<f32>(model[col][row]);
    }
  }
  rtc_model[3] = glm::vec4(rtc_pos, 1.0f);

  return rtc_model;
}

}  // namespace hongtu::scene
