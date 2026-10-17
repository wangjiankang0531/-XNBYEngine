#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/gis/geometry.hpp>
#include <hongtu/gis/geojson_reader.hpp>
#include <hongtu/gis/shapefile_reader.hpp>

using namespace HongTu::RHI;
using namespace HongTu::Asset;
using namespace hongtu::gis;

// ============================================================================
// GIS Vertex
// ============================================================================

struct GISVertex {
    glm::vec2 position; // lon, lat
    glm::vec3 color;

    static VertexInputBindingDescription getBindingDescription() {
        VertexInputBindingDescription b;
        b.binding = 0;
        b.stride = sizeof(GISVertex);
        b.perInstance = false;
        return b;
    }

    static std::vector<VertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VertexInputAttributeDescription> attrs(2);
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = Format::R32G32_SFLOAT;
        attrs[0].offset = offsetof(GISVertex, position);
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = Format::R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(GISVertex, color);
        return attrs;
    }
};

// ============================================================================
// Simple Ear-Clipping Triangulation (for convex/concave polygons, no holes)
// ============================================================================

static float crossZ(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool pointInTriangle(const glm::vec2& p, const glm::vec2& a,
                            const glm::vec2& b, const glm::vec2& c) {
    float d1 = crossZ(a, b, p);
    float d2 = crossZ(b, c, p);
    float d3 = crossZ(c, a, p);
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

static std::vector<uint32_t> triangulatePolygon(const std::vector<glm::vec2>& polygon) {
    std::vector<uint32_t> indices;
    if (polygon.size() < 3) return indices;

    std::vector<uint32_t> remaining;
    for (uint32_t i = 0; i < static_cast<uint32_t>(polygon.size()); ++i)
        remaining.push_back(i);

    // Determine winding (CW or CCW)
    float area = 0;
    for (size_t i = 0; i < polygon.size(); ++i) {
        size_t j = (i + 1) % polygon.size();
        area += polygon[i].x * polygon[j].y - polygon[j].x * polygon[i].y;
    }
    bool ccw = area > 0;

    int safety = static_cast<int>(polygon.size()) * 3;
    while (remaining.size() > 2 && safety-- > 0) {
        bool earFound = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            size_t prev = (i + remaining.size() - 1) % remaining.size();
            size_t next = (i + 1) % remaining.size();

            uint32_t ip = remaining[prev], ic = remaining[i], in = remaining[next];
            float cross = crossZ(polygon[ip], polygon[ic], polygon[in]);

            // Check ear orientation matches winding
            if ((ccw && cross <= 0) || (!ccw && cross >= 0)) continue;

            // Check no other vertex inside this triangle
            bool valid = true;
            for (size_t j = 0; j < remaining.size(); ++j) {
                if (j == prev || j == i || j == next) continue;
                if (pointInTriangle(polygon[remaining[j]], polygon[ip], polygon[ic], polygon[in])) {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                indices.push_back(ip);
                indices.push_back(ic);
                indices.push_back(in);
                remaining.erase(remaining.begin() + static_cast<ptrdiff_t>(i));
                earFound = true;
                break;
            }
        }
        if (!earFound) break;
    }
    return indices;
}

// ============================================================================
// GIS Geometry -> Vertex/Index Conversion
// ============================================================================

struct GISRenderData {
    std::vector<GISVertex> line_verts;
    std::vector<uint32_t> line_indices;
    std::vector<GISVertex> fill_verts;
    std::vector<uint32_t> fill_indices;
    double min_x, min_y, max_x, max_y;
};

static void addPointMarker(GISRenderData& data, double x, double y,
                           const glm::vec3& color, double size = 0.3) {
    uint32_t base = static_cast<uint32_t>(data.line_verts.size());
    float fx = static_cast<float>(x), fy = static_cast<float>(y);
    float s = static_cast<float>(size);

    // Cross shape: horizontal + vertical lines
    data.line_verts.push_back({{fx - s, fy}, color});
    data.line_verts.push_back({{fx + s, fy}, color});
    data.line_verts.push_back({{fx, fy - s}, color});
    data.line_verts.push_back({{fx, fy + s}, color});

    // Diamond shape
    data.line_verts.push_back({{fx, fy + s * 0.7f}, color});
    data.line_verts.push_back({{fx + s * 0.7f, fy}, color});
    data.line_verts.push_back({{fx + s * 0.7f, fy}, color});
    data.line_verts.push_back({{fx, fy - s * 0.7f}, color});
    data.line_verts.push_back({{fx, fy - s * 0.7f}, color});
    data.line_verts.push_back({{fx - s * 0.7f, fy}, color});
    data.line_verts.push_back({{fx - s * 0.7f, fy}, color});
    data.line_verts.push_back({{fx, fy + s * 0.7f}, color});

    for (uint32_t i = 0; i < 12; ++i) data.line_indices.push_back(base + i);
}

static void addLineString(GISRenderData& data, const std::vector<GeoPoint>& pts,
                          const glm::vec3& color) {
    if (pts.size() < 2) return;
    uint32_t base = static_cast<uint32_t>(data.line_verts.size());
    for (const auto& p : pts) {
        data.line_verts.push_back({{static_cast<float>(p.x), static_cast<float>(p.y)}, color});
    }
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        data.line_indices.push_back(base + static_cast<uint32_t>(i));
        data.line_indices.push_back(base + static_cast<uint32_t>(i + 1));
    }
}

static void addRingOutline(GISRenderData& data, const Ring& ring, const glm::vec3& color) {
    if (ring.size() < 2) return;
    uint32_t base = static_cast<uint32_t>(data.line_verts.size());
    for (const auto& p : ring) {
        data.line_verts.push_back({{static_cast<float>(p.x), static_cast<float>(p.y)}, color});
    }
    for (size_t i = 0; i < ring.size(); ++i) {
        data.line_indices.push_back(base + static_cast<uint32_t>(i));
        data.line_indices.push_back(base + static_cast<uint32_t>((i + 1) % ring.size()));
    }
}

static void addPolygonFill(GISRenderData& data, const Ring& ring, const glm::vec3& color) {
    if (ring.size() < 3) return;

    std::vector<glm::vec2> poly;
    for (const auto& p : ring) {
        poly.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
    }
    // Remove closing point if duplicate
    if (poly.size() > 1 && glm::length(poly.front() - poly.back()) < 1e-6f) {
        poly.pop_back();
    }

    auto tri_indices = triangulatePolygon(poly);
    if (tri_indices.empty()) return;

    uint32_t base = static_cast<uint32_t>(data.fill_verts.size());
    for (const auto& p : poly) {
        data.fill_verts.push_back({p, color});
    }
    for (auto idx : tri_indices) {
        data.fill_indices.push_back(base + idx);
    }
}

static void convertFeature(GISRenderData& data, const Feature& feat, double marker_size) {
    const auto& geom = feat.geometry;

    switch (geom.type) {
        case GeometryType::Point:
            addPointMarker(data, geom.point.x, geom.point.y, {1.0f, 0.2f, 0.2f}, marker_size);
            break;

        case GeometryType::MultiPoint:
            for (const auto& p : geom.points)
                addPointMarker(data, p.x, p.y, {1.0f, 0.3f, 0.1f}, marker_size);
            break;

        case GeometryType::LineString:
            addLineString(data, geom.points, {0.2f, 0.6f, 1.0f});
            break;

        case GeometryType::MultiLineString:
            for (const auto& ring : geom.rings)
                addLineString(data, ring, {0.2f, 0.6f, 1.0f});
            break;

        case GeometryType::Polygon:
            if (!geom.rings.empty()) {
                addPolygonFill(data, geom.rings[0], {0.2f, 0.7f, 0.3f});
                for (const auto& ring : geom.rings)
                    addRingOutline(data, ring, {0.9f, 0.9f, 0.9f});
            }
            break;

        case GeometryType::MultiPolygon:
            for (const auto& ring : geom.rings) {
                addPolygonFill(data, ring, {0.2f, 0.7f, 0.3f});
                addRingOutline(data, ring, {0.9f, 0.9f, 0.9f});
            }
            break;

        case GeometryType::GeometryCollection:
            for (const auto& sub : geom.geometries) {
                Feature sub_feat;
                sub_feat.geometry = sub;
                convertFeature(data, sub_feat, marker_size);
            }
            break;

        default:
            break;
    }
}

static GISRenderData convertFeatures(const FeatureCollection& fc) {
    GISRenderData data;
    fc.getBounds(data.min_x, data.min_y, data.max_x, data.max_y);

    // Marker size = ~1% of extent
    double extent = std::max(data.max_x - data.min_x, data.max_y - data.min_y);
    double marker_size = extent * 0.01;
    if (marker_size < 0.01) marker_size = 0.01;

    for (const auto& feat : fc.features) {
        convertFeature(data, feat, marker_size);
    }

    return data;
}

// ============================================================================
// GIS Demo Application
// ============================================================================

class GISDemoApp {
 public:
    bool initialize(const std::string& file_path);
    void run();
    void shutdown();

 private:
    // SDL
    SDL_Window* window = nullptr;
    int windowWidth = 1280;
    int windowHeight = 720;

    // Vulkan/RHI
    RHIDevice* device = nullptr;
    RHIShaderPtr vertexShader, fragmentShader;
    RHIBufferPtr lineVertexBuffer, lineIndexBuffer;
    RHIBufferPtr fillVertexBuffer, fillIndexBuffer;
    RHIPipelinePtr linePipeline, fillPipeline;
    RHIPipelineLayoutPtr pipelineLayout;
    RHIRenderPassPtr renderPass;
    std::vector<RHIFramebufferPtr> framebuffers;
    RHICommandBufferPtr commandBuffer;
    RHISemaphorePtr imageAvailableSemaphore, renderFinishedSemaphore;
    RHIFencePtr inFlightFence;

    // GIS data
    GISRenderData renderData;
    uint32_t lineIndexCount = 0;
    uint32_t fillIndexCount = 0;
    uint32_t featureCount = 0;

    // Camera
    glm::vec2 viewCenter = {0, 0};
    float viewScale = 1.0f;
    float aspectRatio = 16.0f / 9.0f;
    double dataWidth = 1, dataHeight = 1;

    // Input
    bool dragging = false;
    glm::vec2 lastMouse = {0, 0};

    // Timing
    std::chrono::high_resolution_clock::time_point lastTime;
    uint64_t frameCount = 0;
    double fpsTimer = 0;

    bool loadGISData(const std::string& path);
    bool loadBuiltinDemo();
    bool initWindow();
    bool initVulkan();
    bool createShaders();
    bool createBuffers();
    bool createRenderPass();
    bool createFramebuffers();
    bool createPipelines();
    bool createSync();
    void handleInput(const SDL_Event& e);
    void renderFrame();
    void fitView();
    glm::mat4 buildMVP() const;
};

bool GISDemoApp::loadGISData(const std::string& path) {
    if (path == "--demo") return loadBuiltinDemo();

    // Read file
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Cannot open: " << path << std::endl;
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    FeatureCollection fc;
    bool ok = false;

    // Detect format
    if (path.size() > 4 && path.substr(path.size() - 4) == ".shp") {
        ShapefileReader reader;
        std::string base = path.substr(0, path.size() - 4);
        ok = reader.open(base);
        if (ok) fc = reader.getFeatures();
        else std::cerr << "SHP error: " << reader.getError() << std::endl;
    } else {
        // GeoJSON
        GeoJSONReader reader;
        ok = reader.parse(content);
        if (ok) fc = reader.getFeatures();
        else std::cerr << "GeoJSON error: " << reader.getError() << std::endl;
    }

    if (!ok || fc.features.empty()) {
        std::cerr << "No features loaded" << std::endl;
        return false;
    }

    featureCount = static_cast<uint32_t>(fc.features.size());
    renderData = convertFeatures(fc);
    std::cout << "Loaded " << featureCount << " features ("
              << renderData.line_verts.size() << " line verts, "
              << renderData.fill_verts.size() << " fill verts)" << std::endl;
    return true;
}

bool GISDemoApp::loadBuiltinDemo() {
    std::string demo = R"({
      "type": "FeatureCollection",
      "features": [
        {"type":"Feature","geometry":{"type":"Point","coordinates":[116.407,39.904]},"properties":{"name":"Beijing"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[121.474,31.230]},"properties":{"name":"Shanghai"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[113.264,23.129]},"properties":{"name":"Guangzhou"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[114.058,22.543]},"properties":{"name":"Shenzhen"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[104.067,30.573]},"properties":{"name":"Chengdu"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[106.552,29.563]},"properties":{"name":"Chongqing"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[120.155,30.274]},"properties":{"name":"Hangzhou"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[108.954,34.266]},"properties":{"name":"Xian"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[126.650,45.750]},"properties":{"name":"Harbin"}},
        {"type":"Feature","geometry":{"type":"Point","coordinates":[87.617,43.817]},"properties":{"name":"Urumqi"}},
        {"type":"Feature","geometry":{"type":"Polygon","coordinates":[
          [[73.5,18],[97,18],[97,25],[108,18],[120,22],[122,30],[124,40],[135,48],[135,53.5],[120,53.5],[115,47],[110,42],[97,42],[87,48],[73.5,40],[73.5,18]]
        ]},"properties":{"name":"China Border"}},
        {"type":"Feature","geometry":{"type":"LineString","coordinates":[[104,30.5],[106,30],[108,30.5],[110,31],[112,30.8],[114,30.5],[116,31],[118,31.5],[120,31],[121.5,31.2]]},"properties":{"name":"Yangtze River"}},
        {"type":"Feature","geometry":{"type":"LineString","coordinates":[[96,36],[100,37],[104,35.5],[107,34.5],[110,35],[113,35],[116,38],[118,37.5],[119,36],[119.5,35]]},"properties":{"name":"Yellow River"}}
      ]
    })";

    GeoJSONReader reader;
    if (!reader.parse(demo)) return false;

    auto fc = reader.getFeatures();
    featureCount = static_cast<uint32_t>(fc.features.size());
    renderData = convertFeatures(fc);
    std::cout << "Loaded built-in demo: " << featureCount << " features" << std::endl;
    return true;
}

bool GISDemoApp::initialize(const std::string& file_path) {
    if (!loadGISData(file_path)) return false;
    if (!initWindow()) return false;
    if (!initVulkan()) { SDL_DestroyWindow(window); SDL_Quit(); return false; }
    if (!createShaders()) { shutdown(); return false; }
    if (!createBuffers()) { shutdown(); return false; }
    if (!createRenderPass()) { shutdown(); return false; }
    if (!createFramebuffers()) { shutdown(); return false; }
    if (!createPipelines()) { shutdown(); return false; }
    if (!createSync()) { shutdown(); return false; }

    fitView();
    lastTime = std::chrono::high_resolution_clock::now();
    std::cout << "GIS Demo initialized successfully" << std::endl;
    return true;
}

bool GISDemoApp::initWindow() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    window = SDL_CreateWindow("XNBY GIS Demo",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              windowWidth,
                              windowHeight,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (!window) { std::cerr << "Window failed: " << SDL_GetError() << std::endl; return false; }
    return true;
}

bool GISDemoApp::initVulkan() {
    auto factory = Backend::createFactory(Backend::Type::Vulkan);
    if (!factory) return false;

    unsigned int extCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, exts.data());

    SwapchainDesc sc;
    sc.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
    sc.format = Format::R8G8B8A8_SRGB;
    sc.imageCount = 3;
    sc.vsync = true;

    device = factory->createDeviceWithSwapchain(window, sc, false, exts);
    Backend::destroyFactory(factory);

    if (!device) { std::cerr << "Vulkan device failed" << std::endl; return false; }
    std::cout << "GPU: " << device->getProperties().deviceName << std::endl;
    return true;
}

bool GISDemoApp::createShaders() {
    ShaderCompiler compiler;

    const char* vertSrc = R"glsl(
        layout(location = 0) in vec2 inPosition;
        layout(location = 1) in vec3 inColor;
        layout(location = 0) out vec3 outColor;
        layout(push_constant) uniform PC { mat4 mvp; } pc;
        void main() {
            gl_Position = pc.mvp * vec4(inPosition, 0.0, 1.0);
            outColor = inColor;
        }
    )glsl";

    const char* fragSrc = R"glsl(
        layout(location = 0) in vec3 inColor;
        layout(location = 0) out vec4 outColor;
        void main() { outColor = vec4(inColor, 1.0); }
    )glsl";

    auto vr = compiler.compileGLSLToSPIRV(vertSrc, "main", "vertex");
    auto fr = compiler.compileGLSLToSPIRV(fragSrc, "main", "fragment");
    if (!vr.success) { std::cerr << "VS: " << vr.errorMessage << std::endl; return false; }
    if (!fr.success) { std::cerr << "FS: " << fr.errorMessage << std::endl; return false; }

    ShaderDesc vd; vd.stage = ShaderStageFlagBits::Vertex; vd.spirvCode = vr.spirvCode;
    ShaderDesc fd; fd.stage = ShaderStageFlagBits::Fragment; fd.spirvCode = fr.spirvCode;
    vertexShader = device->createShader(vd);
    fragmentShader = device->createShader(fd);
    return vertexShader && fragmentShader;
}

bool GISDemoApp::createBuffers() {
    auto makeBuffer = [&](const auto& data, BufferUsageFlagBits usage) -> RHIBufferPtr {
        if (data.empty()) return nullptr;
        BufferDesc desc;
        desc.size = data.size() * sizeof(data[0]);
        desc.usage = static_cast<BufferUsageFlags>(usage);
        desc.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
        auto buf = device->createBuffer(desc);
        if (buf) {
            void* ptr = buf->map();
            std::memcpy(ptr, data.data(), desc.size);
            buf->unmap();
        }
        return buf;
    };

    lineVertexBuffer = makeBuffer(renderData.line_verts, BufferUsageFlagBits::Vertex);
    lineIndexBuffer = makeBuffer(renderData.line_indices, BufferUsageFlagBits::Index);
    fillVertexBuffer = makeBuffer(renderData.fill_verts, BufferUsageFlagBits::Vertex);
    fillIndexBuffer = makeBuffer(renderData.fill_indices, BufferUsageFlagBits::Index);

    lineIndexCount = static_cast<uint32_t>(renderData.line_indices.size());
    fillIndexCount = static_cast<uint32_t>(renderData.fill_indices.size());

    std::cout << "Lines: " << lineIndexCount / 2 << " segments, Fill: "
              << fillIndexCount / 3 << " triangles" << std::endl;
    return true;
}

bool GISDemoApp::createRenderPass() {
    RenderPassDesc rpd;
    AttachmentDescription ca;
    ca.format = device->getSwapchainFormat();
    ca.sampleCount = 1;
    ca.initialLayout = ImageLayout::Undefined;
    ca.finalLayout = ImageLayout::PresentSrc;
    ca.loadOpClear = true;
    ca.storeOpStore = true;
    rpd.attachments.push_back(ca);

    SubpassDescription sp;
    AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
    sp.colorAttachments.push_back(cr);
    rpd.subpasses.push_back(sp);

    SubpassDependency dep;
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dep.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput);
    dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite);
    rpd.dependencies.push_back(dep);

    renderPass = device->createRenderPass(rpd);
    return renderPass != nullptr;
}

bool GISDemoApp::createFramebuffers() {
    uint32_t count = device->getSwapchainImageCount();
    for (uint32_t i = 0; i < count; ++i) {
        auto img = device->getSwapchainImage(i);
        if (!img) return false;
        auto view = img->createView();
        auto fb = device->createFramebuffer(renderPass, {view}, device->getSwapchainExtent(), 1);
        if (!fb) return false;
        framebuffers.push_back(fb);
    }
    return true;
}

bool GISDemoApp::createPipelines() {
    // Shared layout with push constant for MVP
    PipelineLayoutDesc ld;
    PushConstantRange pcr;
    pcr.stageFlags = static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex);
    pcr.offset = 0; pcr.size = sizeof(glm::mat4);
    ld.pushConstantRanges.push_back(pcr);
    pipelineLayout = device->createPipelineLayout(ld);
    if (!pipelineLayout) return false;

    auto bindDesc = GISVertex::getBindingDescription();
    auto attrDesc = GISVertex::getAttributeDescriptions();

    // Line pipeline
    {
        GraphicsPipelineDesc pd;
        pd.shaders = {vertexShader, fragmentShader};
        pd.vertexInput.bindings = {bindDesc};
        pd.vertexInput.attributes = attrDesc;
        pd.primitiveTopology = PrimitiveTopology::LineList;
        pd.rasterization.cullMode = CullMode::None;
        pd.rasterization.fillMode = FillMode::Solid;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.renderPass = renderPass;
        pd.pipelineLayout = pipelineLayout;
        linePipeline = device->createGraphicsPipeline(pd);
        if (!linePipeline) return false;
    }

    // Fill pipeline
    {
        GraphicsPipelineDesc pd;
        pd.shaders = {vertexShader, fragmentShader};
        pd.vertexInput.bindings = {bindDesc};
        pd.vertexInput.attributes = attrDesc;
        pd.primitiveTopology = PrimitiveTopology::TriangleList;
        pd.rasterization.cullMode = CullMode::None;
        pd.rasterization.fillMode = FillMode::Solid;
        pd.depthStencil.depthTestEnable = false;
        pd.depthStencil.depthWriteEnable = false;
        pd.renderPass = renderPass;
        pd.pipelineLayout = pipelineLayout;
        fillPipeline = device->createGraphicsPipeline(pd);
        if (!fillPipeline) return false;
    }

    commandBuffer = device->createCommandBuffer();
    return commandBuffer != nullptr;
}

bool GISDemoApp::createSync() {
    imageAvailableSemaphore = device->createSemaphore();
    renderFinishedSemaphore = device->createSemaphore();
    inFlightFence = device->createFence(true);
    return imageAvailableSemaphore && renderFinishedSemaphore && inFlightFence;
}

void GISDemoApp::fitView() {
    viewCenter = {
        static_cast<float>((renderData.min_x + renderData.max_x) * 0.5),
        static_cast<float>((renderData.min_y + renderData.max_y) * 0.5)
    };
    dataWidth = renderData.max_x - renderData.min_x;
    dataHeight = renderData.max_y - renderData.min_y;
    if (dataWidth < 1e-6) dataWidth = 1;
    if (dataHeight < 1e-6) dataHeight = 1;

    // Fit with 10% padding
    aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    float scaleX = static_cast<float>(dataWidth) * 1.1f;
    float scaleY = static_cast<float>(dataHeight) * 1.1f;
    viewScale = std::max(scaleX / aspectRatio, scaleY) * 0.5f;
}

glm::mat4 GISDemoApp::buildMVP() const {
    float halfW = viewScale * aspectRatio;
    float halfH = viewScale;
    return glm::ortho(viewCenter.x - halfW, viewCenter.x + halfW,
                      viewCenter.y - halfH, viewCenter.y + halfH,
                      -1.0f, 1.0f);
}

void GISDemoApp::handleInput(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEWHEEL: {
            float factor = (e.wheel.y > 0) ? 0.9f : 1.1f;
            viewScale *= factor;
            viewScale = glm::clamp(viewScale, 0.001f, 10000.0f);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                lastMouse = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) dragging = false;
            break;
        case SDL_MOUSEMOTION:
            if (dragging) {
                glm::vec2 cur = {static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)};
                glm::vec2 delta = cur - lastMouse;
                // Convert pixel delta to world delta
                float worldPerPixelX = viewScale * aspectRatio * 2.0f / static_cast<float>(windowWidth);
                float worldPerPixelY = viewScale * 2.0f / static_cast<float>(windowHeight);
                viewCenter.x -= delta.x * worldPerPixelX;
                viewCenter.y += delta.y * worldPerPixelY; // Y is flipped
                lastMouse = cur;
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_SPACE) fitView();
            if (e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_EQUALS) viewScale *= 0.9f;
            if (e.key.keysym.sym == SDLK_MINUS) viewScale *= 1.1f;
            break;
    }
}

void GISDemoApp::renderFrame() {
    inFlightFence->wait();
    inFlightFence->reset();

    uint32_t imageIndex = device->acquireNextImage(imageAvailableSemaphore);

    commandBuffer->reset();
    commandBuffer->begin();

    Rect2D area;
    area.offset = {0, 0};
    area.extent = device->getSwapchainExtent();
    std::vector<std::array<float, 4>> clearColors = {{0.08f, 0.08f, 0.12f, 1.0f}};
    commandBuffer->beginRenderPass(renderPass, framebuffers[imageIndex], area, clearColors);

    Viewport vp;
    vp.x = 0; vp.y = 0;
    vp.width = static_cast<float>(area.extent.width);
    vp.height = static_cast<float>(area.extent.height);
    vp.minDepth = 0; vp.maxDepth = 1;
    commandBuffer->setViewport(vp);
    commandBuffer->setScissor(area);

    glm::mat4 mvp = buildMVP();
    auto pushMVP = [&]() {
        commandBuffer->pushConstants(pipelineLayout,
            static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex),
            0, sizeof(glm::mat4), &mvp);
    };

    // Draw filled polygons first (background)
    if (fillIndexCount > 0 && fillVertexBuffer && fillIndexBuffer) {
        commandBuffer->bindPipeline(fillPipeline);
        pushMVP();
        commandBuffer->bindVertexBuffer(0, fillVertexBuffer);
        commandBuffer->bindIndexBuffer(fillIndexBuffer);
        commandBuffer->drawIndexed(fillIndexCount);
    }

    // Draw lines on top (outlines, line strings, point markers)
    if (lineIndexCount > 0 && lineVertexBuffer && lineIndexBuffer) {
        commandBuffer->bindPipeline(linePipeline);
        pushMVP();
        commandBuffer->bindVertexBuffer(0, lineVertexBuffer);
        commandBuffer->bindIndexBuffer(lineIndexBuffer);
        commandBuffer->drawIndexed(lineIndexCount);
    }

    commandBuffer->endRenderPass();
    commandBuffer->end();

    device->submit({commandBuffer}, {imageAvailableSemaphore}, {renderFinishedSemaphore}, inFlightFence);
    device->present(renderFinishedSemaphore);
}

void GISDemoApp::run() {
    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { running = false; break; }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) { running = false; break; }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
            handleInput(event);
        }
        if (!running) break;

        renderFrame();
        frameCount++;

        // Update window title with FPS
        auto now = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        fpsTimer += dt;

        if (fpsTimer >= 1.0) {
            double fps = frameCount / fpsTimer;
            std::string title = "XNBY GIS Demo | " + std::to_string(featureCount) + " features | " +
                                std::to_string(static_cast<int>(fps)) + " FPS";
            SDL_SetWindowTitle(window, title.c_str());
            frameCount = 0;
            fpsTimer = 0;
        }
    }

    device->waitIdle();
}

void GISDemoApp::shutdown() {
    if (device) {
        device->waitIdle();
        commandBuffer.reset();
        inFlightFence.reset();
        imageAvailableSemaphore.reset();
        renderFinishedSemaphore.reset();
        linePipeline.reset();
        fillPipeline.reset();
        pipelineLayout.reset();
        framebuffers.clear();
        renderPass.reset();
        lineVertexBuffer.reset();
        lineIndexBuffer.reset();
        fillVertexBuffer.reset();
        fillIndexBuffer.reset();
        vertexShader.reset();
        fragmentShader.reset();
        delete device;
        device = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::string path = "--demo";
    if (argc > 1) path = argv[1];

    if (path == "--help" || path == "-h") {
      std::cout << "XNBY GIS Rendering Demo\n\n"
                << "Usage: gis_demo [file.geojson|file.shp|--demo]\n\n"
                << "Controls:\n"
                << "  Mouse drag  - Pan\n"
                << "  Scroll      - Zoom\n"
                << "  Space       - Reset view\n"
                << "  +/-         - Zoom in/out\n"
                << "  Escape      - Quit\n";
      return 0;
    }

    GISDemoApp app;
    if (!app.initialize(path)) {
        std::cerr << "Failed to initialize GIS demo" << std::endl;
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
