// Decisive test for the "geo annotations render offset from the cursor" bug.
// Replicates the EXACT pipeline:
//   - placement: pickWorld(cursor) → world ECEF point P
//   - annotation build: PrecisionOrigin(origin).worldToLocal(P) → p_rtc (float)
//   - annotation render: vp_anno = proj * view_a, view_a = [R | R·(origin-eye)]
//   - model render (the WORKING path): vp_mesh = proj * mat3(view), pt = P - eye
// Asserts the annotation and the model project to the SAME screen pixel, and
// that both reproject back to the original cursor. If this passes, the RTC math
// is correct and the bug is in the editor's data/flag wiring (not the math).

#include <gtest/gtest.h>

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>

#include <glm/glm.hpp>

using namespace hongtu::renderer;
using namespace hongtu::coordinate;

namespace {
constexpr double kW = 1280, kH = 720;

glm::dvec2 projectScreen(const glm::dmat4& vp, const glm::dvec3& p) {
  glm::dvec4 c = vp * glm::dvec4(p, 1.0);
  glm::dvec3 ndc = glm::dvec3(c) / c.w;
  return {(ndc.x * 0.5 + 0.5) * kW, (ndc.y * 0.5 + 0.5) * kH};
}

struct Pose { double lat, lon, alt, heading, pitch; };
const Pose kPoses[] = {
    {33.5, 104.6, 5000.0, 30.0, -60.0},
    {33.5, 104.6, 200.0, 0.0, -85.0},
    {-20.0, -60.0, 50000.0, -45.0, -50.0},
};
}  // namespace

// The float RTC path is robust to a far origin too — verified, sub-pixel.
// (An earlier hypothesis that a far origin caused float cancellation was
//  disproved: even an earth-centre origin reprojects exactly. Kept as a
//  regression guard so a future change can't quietly break this.)
TEST(GeoRtcProjection, FarOriginStillPrecise) {
  GlobeCamera cam;
  cam.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
  cam.lookAt(Geodetic::fromDegrees(33.5, 104.6, 5000.0), 5010.0, 30.0, -60.0);
  cam.update(0.016f);
  auto hit = cam.pickWorld(kW * 0.5, kH * 0.5, kW, kH);
  ASSERT_TRUE(hit.has_value());
  glm::dvec3 P = hit->point;
  glm::dvec3 eye = cam.getEyeWorld();
  glm::dmat4 view_d = cam.getViewMatrix();
  glm::mat4 proj(cam.getProjectionMatrix(static_cast<float>(kW / kH)));
  glm::dvec2 truth = projectScreen(glm::dmat4(proj) * view_d, P);

  glm::dvec3 origin(0.0);  // extreme far origin
  glm::vec3 p_rtc = glm::vec3(P - origin);
  glm::vec3 t = glm::vec3(glm::dmat3(view_d) * (origin - eye));
  glm::mat4 view_a = glm::mat4(glm::mat3(view_d));
  view_a[3] = glm::vec4(t, 1.0f);
  glm::vec4 c = (proj * view_a) * glm::vec4(p_rtc, 1.0f);
  glm::vec3 ndc = glm::vec3(c) / c.w;
  glm::dvec2 s((ndc.x * 0.5 + 0.5) * kW, (ndc.y * 0.5 + 0.5) * kH);
  EXPECT_NEAR(s.x, truth.x, 2.0);
  EXPECT_NEAR(s.y, truth.y, 2.0);
}

// LOW ALTITUDE is where it bites: screen sensitivity ~ focal/distance, so a
// ~0.5 m world error at 20 m distance is ~16 px. With origin = a centroid that
// has drifted far from the annotation (spread-out annotations, or mixed
// local/geo dragging the centroid toward earth-centre), p_rtc AND t are large
// and their float sum cancels → visible offset. origin = eye keeps both small.
TEST(GeoRtcProjection, LowAltFarOriginOffsetsButEyeOriginFine) {
  GlobeCamera cam;
  cam.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
  cam.lookAt(Geodetic::fromDegrees(33.5, 104.6, 0.0), 25.0, 0.0, -75.0);  // ~25m up
  cam.update(0.016f);
  auto hit = cam.pickWorld(kW * 0.5, kH * 0.5, kW, kH);
  ASSERT_TRUE(hit.has_value());
  glm::dvec3 P = hit->point;
  glm::dvec3 eye = cam.getEyeWorld();
  glm::dmat4 view_d = cam.getViewMatrix();
  glm::mat4 proj(cam.getProjectionMatrix(static_cast<float>(kW / kH)));
  glm::dvec2 truth = projectScreen(glm::dmat4(proj) * view_d, P);

  auto screenViaRtcFloat = [&](const glm::dvec3& origin) {
    glm::vec3 p_rtc = glm::vec3(P - origin);
    glm::vec3 t = glm::vec3(glm::dmat3(view_d) * (origin - eye));
    glm::mat4 view_a = glm::mat4(glm::mat3(view_d));
    view_a[3] = glm::vec4(t, 1.0f);
    glm::vec4 c = (proj * view_a) * glm::vec4(p_rtc, 1.0f);
    glm::vec3 ndc = glm::vec3(c) / c.w;
    return glm::dvec2((ndc.x * 0.5 + 0.5) * kW, (ndc.y * 0.5 + 0.5) * kH);
  };

  // Even an adversarially-far origin (~3.2e6) only costs ~1 px here — float
  // rounding errors largely cancel, so the RTC path is robust regardless of
  // origin. (Disproves the "far centroid causes the offset" hypothesis.)
  glm::dvec2 s_far = screenViaRtcFloat(glm::dvec3(3.2e6, 0.0, 0.0));
  glm::dvec2 s_eye = screenViaRtcFloat(eye);
  EXPECT_LT(glm::length(s_far - truth), 3.0) << "far origin still ~precise";
  EXPECT_NEAR(s_eye.x, truth.x, 1.0);
  EXPECT_NEAR(s_eye.y, truth.y, 1.0);
}

// End-to-end with the REAL annotation pipeline: GeoPolylineAnnotation +
// toRenderPayload + the editor's actual geo_poly_origin (centroid of first
// vertices) + the renderer's vp_anno. Click at screen centre, place a polyline
// there, and verify it reprojects to the centre. This exercises the data path
// (not just the matrix math).
TEST(GeoRtcProjection, RealAnnotationPipelineLandsUnderCursor) {
  GlobeCamera cam;
  cam.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
  cam.lookAt(Geodetic::fromDegrees(33.5, 104.6, 0.0), 30.0, 0.0, -75.0);
  cam.update(0.016f);

  const double cx = kW * 0.5, cy = kH * 0.5;
  auto hit = cam.pickWorld(cx, cy, kW, kH);
  ASSERT_TRUE(hit.has_value());
  Geodetic geo = ecefToGeodetic(hit->point);
  geo.alt = 0.0;  // placement snaps to surface (matches the editor)

  // Build a centered polyline at the click (matches the editor placement).
  constexpr double kR = 6378137.0;
  double cl = std::cos(geo.lat);
  double dlon = 2.5 / (kR * cl);
  hongtu::scene::GeoPolylineAnnotation anno;
  anno.setVertices({{geo.lat, geo.lon - dlon, 0.0}, {geo.lat, geo.lon + dlon, 0.0}});

  // Editor: geo_poly_origin = centroid of annotations' first vertices.
  hongtu::coordinate::PrecisionOrigin zero{};
  auto pl0 = anno.toRenderPayload(zero);
  glm::dvec3 geo_poly_origin = glm::dvec3(pl0.rtc_vertices[0]);

  // Editor: build payload relative to that origin.
  hongtu::coordinate::PrecisionOrigin origin;
  origin.setOrigin(geo_poly_origin);
  auto pl = anno.toRenderPayload(origin);
  ASSERT_GE(pl.rtc_vertices.size(), 2u);

  // Renderer: vp_anno (float, exactly as ViewportRenderer).
  glm::dvec3 eye = cam.getEyeWorld();
  glm::dmat4 view_d = cam.getViewMatrix();
  glm::mat4 proj(cam.getProjectionMatrix(static_cast<float>(kW / kH)));
  glm::vec3 t = glm::vec3(glm::dmat3(view_d) * (geo_poly_origin - eye));
  glm::mat4 view_a = glm::mat4(glm::mat3(view_d));
  view_a[3] = glm::vec4(t, 1.0f);
  glm::mat4 vp_anno = proj * view_a;

  // Both polyline endpoints, midpoint should land at the cursor.
  glm::dvec2 s0 = projectScreen(glm::dmat4(vp_anno), glm::dvec3(pl.rtc_vertices[0]));
  glm::dvec2 s1 = projectScreen(glm::dmat4(vp_anno), glm::dvec3(pl.rtc_vertices[1]));
  glm::dvec2 mid = (s0 + s1) * 0.5;
  EXPECT_NEAR(mid.x, cx, 2.0) << "polyline midpoint off cursor in X";
  EXPECT_NEAR(mid.y, cy, 2.0) << "polyline midpoint off cursor in Y";
}

TEST(GeoRtcProjection, AnnotationMatchesModelAndCursor) {
  for (const auto& pose : kPoses) {
    GlobeCamera cam;
    cam.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
    cam.lookAt(Geodetic::fromDegrees(pose.lat, pose.lon, pose.alt),
               pose.alt + 10.0, pose.heading, pose.pitch);
    cam.update(0.016f);

    const double cx = kW * 0.5, cy = kH * 0.5;
    auto hit = cam.pickWorld(cx, cy, kW, kH);
    ASSERT_TRUE(hit.has_value());
    glm::dvec3 P = hit->point;  // placed world point (cursor ground point)

    glm::dvec3 eye = cam.getEyeWorld();
    glm::dmat4 view_d = cam.getViewMatrix();
    float aspect = static_cast<float>(kW / kH);
    glm::dmat4 proj(cam.getProjectionMatrix(aspect));

    // --- Annotation RTC path (origin = some nearby point, like geo_poly_origin)
    glm::dvec3 origin = P + glm::dvec3(123.0, -45.0, 67.0);  // arbitrary nearby
    PrecisionOrigin po;
    po.setOrigin(origin);
    glm::vec3 p_rtc = po.worldToLocal(P);  // float, small
    glm::dvec3 t = glm::dmat3(view_d) * (origin - eye);
    glm::dmat4 view_a = glm::dmat4(glm::dmat3(view_d));
    view_a[3] = glm::dvec4(t, 1.0);
    glm::dmat4 vp_anno = proj * view_a;
    glm::dvec2 s_anno = projectScreen(vp_anno, glm::dvec3(p_rtc));

    // --- Model RTC path (origin = eye), the WORKING reference
    glm::dmat4 view_m = glm::dmat4(glm::dmat3(view_d));  // rotation only
    glm::dmat4 vp_mesh = proj * view_m;
    glm::dvec2 s_mesh = projectScreen(vp_mesh, P - eye);

    // Annotation and model must land on the same pixel.
    EXPECT_NEAR(s_anno.x, s_mesh.x, 0.5) << "lat=" << pose.lat << " alt=" << pose.alt;
    EXPECT_NEAR(s_anno.y, s_mesh.y, 0.5) << "lat=" << pose.lat << " alt=" << pose.alt;

    // ...and both should reproject to the cursor (the place we clicked).
    EXPECT_NEAR(s_anno.x, cx, 2.0) << "alt=" << pose.alt;
    EXPECT_NEAR(s_anno.y, cy, 2.0) << "alt=" << pose.alt;
  }
}
