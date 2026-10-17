#include <hongtu/renderer/globe/vector_layer_renderer.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace hongtu::renderer {

using namespace HongTu::RHI;
using namespace HongTu::Asset;

// Minimum elevation above the ellipsoid to avoid z-fighting with the globe surface.
static constexpr double kVectorAltitude = 500.0;  // metres

// ============================================================================
// Lifecycle
// ============================================================================

bool VectorLayerRenderer::init(RHIDevice* device, RHIRenderPassPtr render_pass) {
  device_ = device;
  if (!createPipelines(render_pass)) return false;
  initialized_ = true;
  spdlog::info("VectorLayerRenderer initialized");
  return true;
}

void VectorLayerRenderer::cleanup() {
  layers_.clear();
  fill_pipeline_.reset();
  line_pipeline_.reset();
  pipeline_layout_.reset();
  fs_.reset();
  vs_.reset();
  initialized_ = false;
}

// ============================================================================
// Layer management
// ============================================================================

int VectorLayerRenderer::addLayer(const gis::FeatureCollection& fc, const VectorStyle& style) {
  GPULayer layer;
  layer.id = next_id_++;
  layer.features = fc;
  layer.style = style;

  if (initialized_) {
    buildLayer(layer, cached_rtc_origin_);
  }

  int id = layer.id;
  layers_.push_back(std::move(layer));
  spdlog::info("VectorLayerRenderer: added layer {} ('{}', {} features)",
               id, fc.name, fc.features.size());
  return id;
}

void VectorLayerRenderer::removeLayer(int layer_id) {
  layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                               [layer_id](const GPULayer& l) { return l.id == layer_id; }),
                layers_.end());
}

void VectorLayerRenderer::clearLayers() {
  layers_.clear();
}

// ============================================================================
// Update
// ============================================================================

void VectorLayerRenderer::update(const GlobeCamera& camera) {
  if (!initialized_) return;

  const glm::dvec3& origin = camera.getRTCOrigin().getOriginECEF();
  if (origin == cached_rtc_origin_) return;

  cached_rtc_origin_ = origin;
  for (auto& layer : layers_) {
    buildLayer(layer, origin);
  }
}

// ============================================================================
// Render
// ============================================================================

void VectorLayerRenderer::render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect) {
  if (!initialized_ || layers_.empty()) return;

  glm::mat4 mvp = camera.getProjectionMatrix(aspect) * camera.getRTCViewMatrix();
  const ShaderStageFlags both = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;

  for (const auto& layer : layers_) {
    PushData push;
    push.mvp = mvp;

    // Lines
    if (layer.line_vbuf && layer.line_vertex_count > 0) {
      push.color = layer.style.line_color;
      cmd->bindPipeline(line_pipeline_);
      cmd->pushConstants(pipeline_layout_, both, 0u, sizeof(PushData), &push);
      cmd->bindVertexBuffer(0u, layer.line_vbuf);
      cmd->draw(layer.line_vertex_count, 1u, 0u, 0u);
    }

    // Fill (optional)
    if (layer.style.show_fill && layer.fill_vbuf && layer.fill_vertex_count > 0) {
      push.color = layer.style.fill_color;
      cmd->bindPipeline(fill_pipeline_);
      cmd->pushConstants(pipeline_layout_, both, 0u, sizeof(PushData), &push);
      cmd->bindVertexBuffer(0u, layer.fill_vbuf);
      cmd->draw(layer.fill_vertex_count, 1u, 0u, 0u);
    }
  }
}

// ============================================================================
// Geometry tessellation
// ============================================================================

static glm::vec3 geoPointToRTC(const gis::GeoPoint& pt, const glm::dvec3& rtc_origin) {
  using namespace hongtu::coordinate;
  double alt = (pt.has_z && pt.z > kVectorAltitude) ? pt.z : kVectorAltitude;
  glm::dvec3 ecef = geodeticToECEF(Geodetic::fromDegrees(pt.y, pt.x, alt));
  return glm::vec3(ecef - rtc_origin);
}

// Emit a closed ring as LineList segment pairs.
static void emitRingLines(const gis::Ring& ring, const glm::dvec3& rtc_origin,
                          std::vector<glm::vec3>& line_verts) {
  if (ring.size() < 2) return;
  for (size_t i = 1; i < ring.size(); ++i) {
    line_verts.push_back(geoPointToRTC(ring[i - 1], rtc_origin));
    line_verts.push_back(geoPointToRTC(ring[i],     rtc_origin));
  }
  // Close the ring if not already closed (first != last)
  const auto& f = ring.front();
  const auto& b = ring.back();
  if (f.x != b.x || f.y != b.y) {
    line_verts.push_back(geoPointToRTC(b, rtc_origin));
    line_verts.push_back(geoPointToRTC(f, rtc_origin));
  }
}

// Tessellate outer ring of a polygon into triangles using a triangle fan.
// Works correctly for convex polygons; produces acceptable results for most
// administrative boundary shapes when viewed from space.
static void emitRingFill(const gis::Ring& ring, const glm::dvec3& rtc_origin,
                         std::vector<glm::vec3>& fill_verts) {
  if (ring.size() < 4) return;  // closed ring needs >=4 pts (first==last)
  glm::vec3 v0 = geoPointToRTC(ring[0], rtc_origin);
  for (size_t i = 1; i + 1 < ring.size(); ++i) {
    fill_verts.push_back(v0);
    fill_verts.push_back(geoPointToRTC(ring[i],     rtc_origin));
    fill_verts.push_back(geoPointToRTC(ring[i + 1], rtc_origin));
  }
}

void VectorLayerRenderer::buildGeometry(const gis::Geometry& geom,
                                         const glm::dvec3& rtc_origin,
                                         std::vector<glm::vec3>& line_verts,
                                         std::vector<glm::vec3>& fill_verts) {
  using gis::GeometryType;

  switch (geom.type) {
    case GeometryType::Point: {
      // Render as tiny cross (two perpendicular line segments in world space).
      // The cross is sized relative to a typical globe viewing distance.
      static constexpr double kCrossHalfLen = 5000.0;  // 5 km
      using namespace hongtu::coordinate;
      double alt = geom.point.has_z ? geom.point.z : kVectorAltitude;
      glm::dvec3 ecef = geodeticToECEF(Geodetic::fromDegrees(geom.point.y, geom.point.x, alt));
      glm::vec3 center = glm::vec3(ecef - rtc_origin);
      // Offset slightly in X and Y (world space) for the cross arms
      glm::vec3 dx(static_cast<float>(kCrossHalfLen), 0.0f, 0.0f);
      glm::vec3 dy(0.0f, static_cast<float>(kCrossHalfLen), 0.0f);
      line_verts.push_back(center - dx); line_verts.push_back(center + dx);
      line_verts.push_back(center - dy); line_verts.push_back(center + dy);
      break;
    }

    case GeometryType::MultiPoint:
      for (const auto& child : geom.geometries) {
        buildGeometry(child, rtc_origin, line_verts, fill_verts);
      }
      break;

    case GeometryType::LineString:
      for (size_t i = 1; i < geom.points.size(); ++i) {
        line_verts.push_back(geoPointToRTC(geom.points[i - 1], rtc_origin));
        line_verts.push_back(geoPointToRTC(geom.points[i],     rtc_origin));
      }
      break;

    case GeometryType::MultiLineString:
      for (const auto& child : geom.geometries) {
        buildGeometry(child, rtc_origin, line_verts, fill_verts);
      }
      break;

    case GeometryType::Polygon:
      // Outline all rings; fill only the outer (first) ring
      for (size_t r = 0; r < geom.rings.size(); ++r) {
        emitRingLines(geom.rings[r], rtc_origin, line_verts);
        if (r == 0) emitRingFill(geom.rings[r], rtc_origin, fill_verts);
      }
      break;

    case GeometryType::MultiPolygon:
      for (const auto& child : geom.geometries) {
        buildGeometry(child, rtc_origin, line_verts, fill_verts);
      }
      break;

    case GeometryType::GeometryCollection:
      for (const auto& child : geom.geometries) {
        buildGeometry(child, rtc_origin, line_verts, fill_verts);
      }
      break;

    default:
      break;
  }
}

void VectorLayerRenderer::buildLayer(GPULayer& layer, const glm::dvec3& rtc_origin) {
  std::vector<glm::vec3> line_verts;
  std::vector<glm::vec3> fill_verts;
  line_verts.reserve(1024);
  fill_verts.reserve(1024);

  for (const auto& feature : layer.features.features) {
    buildGeometry(feature.geometry, rtc_origin, line_verts, fill_verts);
  }

  layer.line_vbuf.reset();
  layer.line_vertex_count = 0;
  layer.fill_vbuf.reset();
  layer.fill_vertex_count = 0;

  if (!line_verts.empty()) {
    layer.line_vbuf = uploadBuffer(line_verts);
    layer.line_vertex_count = static_cast<u32>(line_verts.size());
  }
  if (!fill_verts.empty()) {
    layer.fill_vbuf = uploadBuffer(fill_verts);
    layer.fill_vertex_count = static_cast<u32>(fill_verts.size());
  }
}

RHIBufferPtr VectorLayerRenderer::uploadBuffer(const std::vector<glm::vec3>& verts) {
  BufferDesc bd;
  bd.size = verts.size() * sizeof(glm::vec3);
  bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  auto buf = device_->createBuffer(bd);
  if (buf) {
    void* ptr = buf->map();
    std::memcpy(ptr, verts.data(), bd.size);
    buf->unmap();
  }
  return buf;
}

// ============================================================================
// Pipeline creation
// ============================================================================

bool VectorLayerRenderer::createPipelines(RHIRenderPassPtr render_pass) {
  ShaderCompiler compiler;

  const char* vs_src = R"glsl(
    layout(location = 0) in vec3 inPos;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 color;
    } pc;
    void main() {
        gl_Position = pc.mvp * vec4(inPos, 1.0);
    }
  )glsl";

  const char* fs_src = R"glsl(
    layout(location = 0) out vec4 outColor;
    layout(push_constant) uniform PC {
        mat4 mvp;
        vec4 color;
    } pc;
    void main() {
        outColor = pc.color;
    }
  )glsl";

  auto vs_result = compiler.compileGLSLToSPIRV(vs_src, "main", "vertex");
  auto fs_result = compiler.compileGLSLToSPIRV(fs_src, "main", "fragment");

  if (!vs_result.success || !fs_result.success) {
    spdlog::error("VectorLayerRenderer: shader compilation failed: vert='{}' frag='{}'",
                  vs_result.errorMessage, fs_result.errorMessage);
    return false;
  }

  ShaderDesc vs_desc;
  vs_desc.stage = ShaderStageFlagBits::Vertex;
  vs_desc.spirvCode = vs_result.spirvCode;
  vs_ = device_->createShader(vs_desc);

  ShaderDesc fs_desc;
  fs_desc.stage = ShaderStageFlagBits::Fragment;
  fs_desc.spirvCode = fs_result.spirvCode;
  fs_ = device_->createShader(fs_desc);

  if (!vs_ || !fs_) {
    spdlog::error("VectorLayerRenderer: failed to create shader modules");
    return false;
  }

  // Pipeline layout: push constants for MVP (mat4) + color (vec4) = 80 bytes
  {
    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = ShaderStageFlagBits::Vertex | ShaderStageFlagBits::Fragment;
    pcr.offset = 0u;
    pcr.size = sizeof(PushData);
    ld.pushConstantRanges.push_back(pcr);
    pipeline_layout_ = device_->createPipelineLayout(ld);
    if (!pipeline_layout_) {
      spdlog::error("VectorLayerRenderer: failed to create pipeline layout");
      return false;
    }
  }

  // Vertex input: position only (vec3, location 0)
  VertexInputBindingDescription bind;
  bind.binding = 0u;
  bind.stride = sizeof(glm::vec3);
  bind.perInstance = false;

  VertexInputAttributeDescription attr;
  attr.location = 0u;
  attr.binding = 0u;
  attr.format = Format::R32G32B32_SFLOAT;
  attr.offset = 0u;

  // Alpha blending for fill transparency
  ColorBlendAttachmentState blend;
  blend.blendEnable = true;
  blend.srcColorBlendFactor = BlendFactor::SrcAlpha;
  blend.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
  blend.colorBlendOp = BlendOp::Add;
  blend.srcAlphaBlendFactor = BlendFactor::One;
  blend.dstAlphaBlendFactor = BlendFactor::Zero;
  blend.alphaBlendOp = BlendOp::Add;

  auto makeDesc = [&](PrimitiveTopology topo) {
    GraphicsPipelineDesc pd;
    pd.shaders = {vs_, fs_};
    pd.vertexInput.bindings = {bind};
    pd.vertexInput.attributes = {attr};
    pd.primitiveTopology = topo;
    pd.rasterization.cullMode = CullMode::None;
    pd.rasterization.fillMode = FillMode::Solid;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthWriteEnable = false;
    pd.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
    pd.colorBlendAttachments.push_back(blend);
    pd.renderPass = render_pass;
    pd.pipelineLayout = pipeline_layout_;
    return pd;
  };

  line_pipeline_ = device_->createGraphicsPipeline(makeDesc(PrimitiveTopology::LineList));
  if (!line_pipeline_) {
    spdlog::error("VectorLayerRenderer: failed to create line pipeline");
    return false;
  }

  fill_pipeline_ = device_->createGraphicsPipeline(makeDesc(PrimitiveTopology::TriangleList));
  if (!fill_pipeline_) {
    spdlog::error("VectorLayerRenderer: failed to create fill pipeline");
    return false;
  }

  return true;
}

}  // namespace hongtu::renderer
