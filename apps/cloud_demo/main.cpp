/// HongTu Cloud Rendering Demo - Interactive
/// CPU software rasterizer with mouse/keyboard interaction via browser.
/// Browser sends input events → Server updates camera → Re-renders → MJPEG stream.

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ============================================================================
// Camera State (shared between render thread and HTTP input handler)
// ============================================================================

struct Camera {
    std::mutex mtx;
    float pan_x = 0, pan_y = 0;     // Pan offset (pixels → world)
    float zoom = 1.0f;               // Zoom level
    float rotate = 0.0f;             // Rotation angle (radians)
    bool auto_rotate = true;

    void applyPan(float dx, float dy) {
        std::lock_guard<std::mutex> lk(mtx);
        pan_x += dx / zoom;
        pan_y += dy / zoom;
    }
    void applyZoom(float factor, float cx, float cy) {
        std::lock_guard<std::mutex> lk(mtx);
        float old_zoom = zoom;
        zoom *= factor;
        zoom = std::clamp(zoom, 0.1f, 20.0f);
        // Zoom toward cursor: adjust pan so point under cursor stays fixed
        float zr = 1.0f - old_zoom / zoom;
        pan_x += (cx - 320.0f) * zr / zoom;
        pan_y += (cy - 240.0f) * zr / zoom;
    }
    void applyRotate(float delta) {
        std::lock_guard<std::mutex> lk(mtx);
        rotate += delta;
        auto_rotate = false;
    }
    void reset() {
        std::lock_guard<std::mutex> lk(mtx);
        pan_x = pan_y = rotate = 0;
        zoom = 1.0f;
        auto_rotate = true;
    }
    struct Snapshot { float px, py, zm, rot; bool ar; };
    Snapshot snapshot() {
        std::lock_guard<std::mutex> lk(mtx);
        return {pan_x, pan_y, zoom, rotate, auto_rotate};
    }
};

static Camera g_camera;

// ============================================================================
// Framebuffer + Software Rasterizer
// ============================================================================

struct Framebuffer {
    std::vector<uint8_t> pixels;
    int width, height;
    void resize(int w, int h) { width = w; height = h; pixels.assign(w * h * 4, 0); }
    void clear(uint8_t r, uint8_t g, uint8_t b) {
        for (int i = 0; i < width * height; ++i) {
            pixels[i*4]=r; pixels[i*4+1]=g; pixels[i*4+2]=b; pixels[i*4+3]=255;
        }
    }
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x<0||x>=width||y<0||y>=height) return;
        int i=(y*width+x)*4; pixels[i]=r; pixels[i+1]=g; pixels[i+2]=b; pixels[i+3]=255;
    }
};

static void drawTriangle(Framebuffer& fb, glm::vec2 v0, glm::vec2 v1, glm::vec2 v2,
                          glm::vec3 c0, glm::vec3 c1, glm::vec3 c2) {
    int minX=std::max(0,(int)std::min({v0.x,v1.x,v2.x}));
    int minY=std::max(0,(int)std::min({v0.y,v1.y,v2.y}));
    int maxX=std::min(fb.width-1,(int)std::max({v0.x,v1.x,v2.x})+1);
    int maxY=std::min(fb.height-1,(int)std::max({v0.y,v1.y,v2.y})+1);
    float area=(v1.x-v0.x)*(v2.y-v0.y)-(v2.x-v0.x)*(v1.y-v0.y);
    if(std::abs(area)<1e-6f) return;
    for(int y=minY;y<=maxY;++y) for(int x=minX;x<=maxX;++x) {
        float px=x+0.5f, py=y+0.5f;
        float w0=((v1.x-px)*(v2.y-py)-(v2.x-px)*(v1.y-py))/area;
        float w1=((v2.x-px)*(v0.y-py)-(v0.x-px)*(v2.y-py))/area;
        float w2=1-w0-w1;
        if(w0>=0&&w1>=0&&w2>=0) {
            auto c=c0*w0+c1*w1+c2*w2;
            fb.setPixel(x,y,(uint8_t)(std::clamp(c.r,0.f,1.f)*255),
                            (uint8_t)(std::clamp(c.g,0.f,1.f)*255),
                            (uint8_t)(std::clamp(c.b,0.f,1.f)*255));
        }
    }
}

static void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    int dx=std::abs(x1-x0), dy=std::abs(y1-y0), sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
    while(true) {
        fb.setPixel(x0,y0,r,g,b);
        if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>-dy){err-=dy;x0+=sx;}
        if(e2<dx){err+=dx;y0+=sy;}
    }
}

static void drawCircle(Framebuffer& fb, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    for (int a = 0; a < 64; ++a) {
        float t0 = a * 6.2832f / 64.0f, t1 = (a+1) * 6.2832f / 64.0f;
        drawLine(fb, cx+(int)(radius*cosf(t0)), cy+(int)(radius*sinf(t0)),
                     cx+(int)(radius*cosf(t1)), cy+(int)(radius*sinf(t1)), r, g, b);
    }
}

// ============================================================================
// Transform a world point through camera
// ============================================================================

static glm::vec2 worldToScreen(glm::vec2 p, const Camera::Snapshot& cam, float w, float h) {
    // Center, rotate, zoom, pan
    float cx = w * 0.5f, cy = h * 0.5f;
    p -= glm::vec2(cx, cy);
    // Rotate
    float cs = cosf(cam.rot), sn = sinf(cam.rot);
    p = glm::vec2(p.x * cs - p.y * sn, p.x * sn + p.y * cs);
    // Zoom + pan
    p = p * cam.zm + glm::vec2(cam.px, cam.py) * cam.zm;
    p += glm::vec2(cx, cy);
    return p;
}

// ============================================================================
// Render scene with camera transforms
// ============================================================================

static void renderScene(Framebuffer& fb, float time) {
    fb.clear(25, 25, 40);
    float w = (float)fb.width, h = (float)fb.height;
    float cx = w * 0.5f, cy = h * 0.5f;
    auto cam = g_camera.snapshot();

    // Auto-rotate
    float rot_offset = cam.ar ? time * 0.5f : 0.0f;

    // Grid (in world space, transformed by camera)
    float grid_step = 40.0f;
    float extent = 800.0f / cam.zm;
    float gx0 = -cam.px - extent, gy0 = -cam.py - extent;
    float gx1 = -cam.px + extent, gy1 = -cam.py + extent;
    gx0 = floorf(gx0 / grid_step) * grid_step;
    gy0 = floorf(gy0 / grid_step) * grid_step;

    for (float gx = gx0; gx <= gx1; gx += grid_step) {
        auto a = worldToScreen({cx + gx, cy + gy0}, cam, w, h);
        auto b = worldToScreen({cx + gx, cy + gy1}, cam, w, h);
        drawLine(fb, (int)a.x, (int)a.y, (int)b.x, (int)b.y, 38, 38, 52);
    }
    for (float gy = gy0; gy <= gy1; gy += grid_step) {
        auto a = worldToScreen({cx + gx0, cy + gy}, cam, w, h);
        auto b = worldToScreen({cx + gx1, cy + gy}, cam, w, h);
        drawLine(fb, (int)a.x, (int)a.y, (int)b.x, (int)b.y, 38, 38, 52);
    }

    // Axis lines
    auto o  = worldToScreen({cx, cy}, cam, w, h);
    auto ax = worldToScreen({cx + 80, cy}, cam, w, h);
    auto ay = worldToScreen({cx, cy + 80}, cam, w, h);
    drawLine(fb, (int)o.x, (int)o.y, (int)ax.x, (int)ax.y, 200, 60, 60);
    drawLine(fb, (int)o.x, (int)o.y, (int)ay.x, (int)ay.y, 60, 200, 60);

    // Main triangle (rotates over time + camera rotation)
    float radius = 120.0f;
    glm::vec2 verts[3];
    glm::vec3 colors[3] = {{1,0.15f,0.15f},{0.15f,1,0.15f},{0.2f,0.4f,1}};
    for (int i = 0; i < 3; ++i) {
        float a = rot_offset + cam.rot + (float)i * 2.0944f;
        glm::vec2 wp = {cx + radius * cosf(a), cy + radius * sinf(a)};
        verts[i] = worldToScreen(wp, cam, w, h);
    }
    drawTriangle(fb, verts[0], verts[1], verts[2], colors[0], colors[1], colors[2]);
    for (int i = 0; i < 3; ++i) {
        int j = (i+1)%3;
        drawLine(fb, (int)verts[i].x, (int)verts[i].y, (int)verts[j].x, (int)verts[j].y, 230,230,230);
    }

    // Orbiting smaller triangle
    float r2 = 45.0f;
    float ox = cx + 180 * cosf(time * 0.4f);
    float oy = cy + 180 * sinf(time * 0.4f);
    glm::vec3 c2[3] = {{1,0.8f,0.1f},{0.1f,0.8f,1},{1,0.2f,0.8f}};
    glm::vec2 v2[3];
    for (int i = 0; i < 3; ++i) {
        float a = -time * 2.0f + (float)i * 2.0944f;
        v2[i] = worldToScreen({ox + r2*cosf(a), oy + r2*sinf(a)}, cam, w, h);
    }
    drawTriangle(fb, v2[0], v2[1], v2[2], c2[0], c2[1], c2[2]);

    // A few circles
    for (int i = 0; i < 5; ++i) {
        float angle = time * 0.3f + (float)i * 1.2566f;
        float dist = 200.0f + 30.0f * sinf(time + (float)i);
        auto cp = worldToScreen({cx + dist*cosf(angle), cy + dist*sinf(angle)}, cam, w, h);
        int cr = std::max(3, (int)(15 * cam.zm));
        drawCircle(fb, (int)cp.x, (int)cp.y, cr,
                   (uint8_t)(150 + i*20), (uint8_t)(100 + i*30), (uint8_t)(200 - i*20));
    }

    // HUD (not affected by camera)
    // Zoom indicator
    char hud[64];
    snprintf(hud, sizeof(hud), "%.1fx", (double)cam.zm);
    // Simple HUD text as colored blocks
    for (int i = 0; hud[i] && i < 10; ++i) {
        int bx = 10 + i * 8, by = fb.height - 20;
        for (int dy = 0; dy < 10; ++dy)
            for (int dx = 0; dx < 6; ++dx)
                fb.setPixel(bx+dx, by+dy, 20, 20, 35);
        // digit rendering (simplified)
        if (hud[i] >= '0' && hud[i] <= '9') {
            int d = hud[i] - '0';
            for (int s = 0; s < d+1; ++s)
                fb.setPixel(bx+s%3*2, by+2+s/3*2, 200, 200, 200);
        } else if (hud[i] == '.') {
            fb.setPixel(bx+2, by+8, 200, 200, 200);
        } else if (hud[i] == 'x') {
            drawLine(fb, bx, by+2, bx+4, by+8, 200, 200, 200);
            drawLine(fb, bx+4, by+2, bx, by+8, 200, 200, 200);
        }
    }
}

// ============================================================================
// JPEG Encoding
// ============================================================================

struct JPEGBuffer { std::vector<uint8_t> data; };
static void stb_cb(void* ctx, void* data, int size) {
    auto* b = (JPEGBuffer*)ctx;
    b->data.insert(b->data.end(), (uint8_t*)data, (uint8_t*)data + size);
}
static JPEGBuffer encodeJPEG(const Framebuffer& fb, int quality = 80) {
    JPEGBuffer buf;
    std::vector<uint8_t> rgb(fb.width * fb.height * 3);
    for (int i = 0; i < fb.width * fb.height; ++i) {
        rgb[i*3]=fb.pixels[i*4]; rgb[i*3+1]=fb.pixels[i*4+1]; rgb[i*3+2]=fb.pixels[i*4+2];
    }
    stbi_write_jpg_to_func(stb_cb, &buf, fb.width, fb.height, 3, rgb.data(), quality);
    return buf;
}

// ============================================================================
// Shared frame state
// ============================================================================

static std::mutex g_frame_mutex;
static std::vector<uint8_t> g_latest_jpeg;
static std::atomic<bool> g_running{true};
static std::atomic<uint32_t> g_frame_number{0};
static std::atomic<uint32_t> g_client_count{0};

// ============================================================================
// HTML Page with interactive mouse/wheel/keyboard handlers
// ============================================================================

static const char* HTML_PAGE = R"html(<!DOCTYPE html>
<html>
<head>
<title>XNBY Cloud Rendering</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { background: #0f0f1a; color: #eee; font-family: 'Segoe UI', system-ui, sans-serif;
         display: flex; flex-direction: column; align-items: center; padding: 15px; }
  h1 { color: #e94560; font-size: 22px; margin-bottom: 3px; }
  .sub { color: #666; font-size: 13px; margin-bottom: 12px; }
  .badge { background: #e94560; color: white; padding: 1px 8px; border-radius: 8px; font-size: 11px; }
  .viewport { position: relative; border-radius: 10px; overflow: hidden;
              box-shadow: 0 4px 24px rgba(0,0,0,0.5); cursor: grab; user-select: none; }
  .viewport:active { cursor: grabbing; }
  .viewport img { display: block; }
  .overlay { position: absolute; top: 8px; right: 8px; background: rgba(0,0,0,0.6);
             padding: 6px 10px; border-radius: 6px; font-size: 12px; color: #aaa; pointer-events: none; }
  .controls { margin-top: 12px; display: flex; gap: 8px; flex-wrap: wrap; justify-content: center; }
  .controls button { background: #1a1a2e; color: #ccc; border: 1px solid #333; padding: 6px 14px;
                     border-radius: 6px; cursor: pointer; font-size: 12px; }
  .controls button:hover { background: #e94560; color: white; border-color: #e94560; }
  .help { margin-top: 10px; color: #555; font-size: 11px; text-align: center; line-height: 1.6; }
</style>
</head>
<body>
  <h1>XNBY Engine <span class="badge">Cloud Rendering</span></h1>
  <div class="sub">Interactive server-side rendering with real-time input</div>

  <div class="viewport" id="viewport">
    <img id="stream" src="/stream" width="640" height="480" draggable="false" />
    <div class="overlay" id="hud">Connecting...</div>
  </div>

  <div class="controls">
    <button onclick="send('reset','')">Reset View</button>
    <button onclick="send('zoom_in','')">Zoom +</button>
    <button onclick="send('zoom_out','')">Zoom -</button>
    <button onclick="send('rotate_cw','')">Rotate CW</button>
    <button onclick="send('rotate_ccw','')">Rotate CCW</button>
  </div>

  <div class="help">
    Drag to pan | Scroll to zoom | Shift+Drag to rotate | Buttons or keyboard shortcuts (R=reset, +/- zoom)
  </div>

<script>
const img = document.getElementById('stream');
const hud = document.getElementById('hud');
const vp = document.getElementById('viewport');
let dragging = false, lastX = 0, lastY = 0, shiftDown = false;
let frames = 0, lastTime = performance.now();

function send(type, data) {
  fetch('/input', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({type, data})
  }).catch(() => {});
}

// Mouse drag
vp.addEventListener('mousedown', e => {
  dragging = true; lastX = e.clientX; lastY = e.clientY;
  shiftDown = e.shiftKey;
  e.preventDefault();
});
window.addEventListener('mousemove', e => {
  if (!dragging) return;
  const dx = e.clientX - lastX, dy = e.clientY - lastY;
  lastX = e.clientX; lastY = e.clientY;
  if (shiftDown || e.shiftKey) {
    send('rotate', String(dx * 0.01));
  } else {
    send('pan', dx + ',' + dy);
  }
});
window.addEventListener('mouseup', () => { dragging = false; });

// Scroll to zoom
vp.addEventListener('wheel', e => {
  e.preventDefault();
  const rect = img.getBoundingClientRect();
  const cx = e.clientX - rect.left, cy = e.clientY - rect.top;
  const factor = e.deltaY < 0 ? 1.15 : 0.87;
  send('zoom', factor + ',' + cx + ',' + cy);
}, {passive: false});

// Touch support
let lastTouchDist = 0;
vp.addEventListener('touchstart', e => {
  if (e.touches.length === 1) {
    dragging = true; lastX = e.touches[0].clientX; lastY = e.touches[0].clientY;
  } else if (e.touches.length === 2) {
    const dx = e.touches[0].clientX - e.touches[1].clientX;
    const dy = e.touches[0].clientY - e.touches[1].clientY;
    lastTouchDist = Math.sqrt(dx*dx + dy*dy);
  }
  e.preventDefault();
}, {passive: false});
vp.addEventListener('touchmove', e => {
  if (e.touches.length === 1 && dragging) {
    const dx = e.touches[0].clientX - lastX, dy = e.touches[0].clientY - lastY;
    lastX = e.touches[0].clientX; lastY = e.touches[0].clientY;
    send('pan', dx + ',' + dy);
  } else if (e.touches.length === 2) {
    const dx = e.touches[0].clientX - e.touches[1].clientX;
    const dy = e.touches[0].clientY - e.touches[1].clientY;
    const dist = Math.sqrt(dx*dx + dy*dy);
    if (lastTouchDist > 0) {
      send('zoom', (dist/lastTouchDist) + ',320,240');
    }
    lastTouchDist = dist;
  }
  e.preventDefault();
}, {passive: false});
vp.addEventListener('touchend', () => { dragging = false; lastTouchDist = 0; });

// Keyboard
window.addEventListener('keydown', e => {
  if (e.key === 'r' || e.key === 'R') send('reset','');
  if (e.key === '+' || e.key === '=') send('zoom_in','');
  if (e.key === '-') send('zoom_out','');
  if (e.key === '[') send('rotate_ccw','');
  if (e.key === ']') send('rotate_cw','');
});

// FPS counter
img.onload = () => {
  frames++;
  const now = performance.now();
  if (now - lastTime >= 1000) {
    hud.textContent = frames + ' FPS | Interactive Cloud Rendering';
    frames = 0; lastTime = now;
  }
};
</script>
</body>
</html>)html";

// ============================================================================
// HTTP request parsing helper
// ============================================================================

static std::string getRequestBody(const char* buf, size_t len) {
    const char* body = strstr(buf, "\r\n\r\n");
    if (!body) return "";
    return std::string(body + 4, buf + len - body - 4);
}

// ============================================================================
// Handle input commands from browser
// ============================================================================

static void handleInput(const std::string& body) {
    // Simple JSON parsing (no library needed for this format)
    auto getVal = [&](const std::string& key) -> std::string {
        auto pos = body.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = body.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = body.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        auto end = body.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return body.substr(pos + 1, end - pos - 1);
    };

    std::string type = getVal("type");
    std::string data = getVal("data");

    if (type == "pan") {
        float dx = 0, dy = 0;
        sscanf(data.c_str(), "%f,%f", &dx, &dy);
        g_camera.applyPan(dx, dy);
    } else if (type == "zoom") {
        float factor = 1.0f, cx = 320, cy = 240;
        sscanf(data.c_str(), "%f,%f,%f", &factor, &cx, &cy);
        g_camera.applyZoom(factor, cx, cy);
    } else if (type == "rotate") {
        float delta = 0;
        sscanf(data.c_str(), "%f", &delta);
        g_camera.applyRotate(delta);
    } else if (type == "reset") {
        g_camera.reset();
    } else if (type == "zoom_in") {
        g_camera.applyZoom(1.3f, 320, 240);
    } else if (type == "zoom_out") {
        g_camera.applyZoom(0.77f, 320, 240);
    } else if (type == "rotate_cw") {
        g_camera.applyRotate(0.15f);
    } else if (type == "rotate_ccw") {
        g_camera.applyRotate(-0.15f);
    }
}

// ============================================================================
// HTTP Handler
// ============================================================================

static void handleClient(int fd) {
    char buf[8192];
    ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';

    std::string req(buf, n);

    // Route
    if (req.find("POST /input") != std::string::npos) {
        handleInput(getRequestBody(buf, n));
        const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                           "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\nok";
        send(fd, resp, strlen(resp), 0);
        close(fd);
    } else if (req.find("GET /stream") != std::string::npos) {
        g_client_count++;
        std::string hdr = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                          "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
        send(fd, hdr.c_str(), hdr.size(), 0);

        uint32_t last = 0;
        while (g_running) {
            uint32_t cur = g_frame_number.load();
            if (cur == last) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            last = cur;
            std::vector<uint8_t> jpg;
            { std::lock_guard<std::mutex> lk(g_frame_mutex); jpg = g_latest_jpeg; }
            if (jpg.empty()) continue;
            std::ostringstream p;
            p << "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " << jpg.size() << "\r\n\r\n";
            std::string ph = p.str();
            if (send(fd, ph.c_str(), ph.size(), MSG_NOSIGNAL) <= 0) break;
            if (send(fd, jpg.data(), jpg.size(), MSG_NOSIGNAL) <= 0) break;
            if (send(fd, "\r\n", 2, MSG_NOSIGNAL) <= 0) break;
        }
        g_client_count--;
        close(fd);
    } else if (req.find("GET /snapshot") != std::string::npos) {
        std::lock_guard<std::mutex> lk(g_frame_mutex);
        std::ostringstream r;
        r << "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: "
          << g_latest_jpeg.size() << "\r\nConnection: close\r\n\r\n";
        std::string h = r.str();
        send(fd, h.c_str(), h.size(), 0);
        if (!g_latest_jpeg.empty()) send(fd, g_latest_jpeg.data(), g_latest_jpeg.size(), 0);
        close(fd);
    } else {
        // HTML page or OPTIONS preflight
        if (req.find("OPTIONS") != std::string::npos) {
            const char* cors = "HTTP/1.1 204 No Content\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: POST\r\n"
                               "Access-Control-Allow-Headers: Content-Type\r\n"
                               "Connection: close\r\n\r\n";
            send(fd, cors, strlen(cors), 0);
        } else {
            std::string body(HTML_PAGE);
            std::ostringstream r;
            r << "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
              << body.size() << "\r\nConnection: close\r\n\r\n" << body;
            std::string s = r.str();
            send(fd, s.c_str(), s.size(), 0);
        }
        close(fd);
    }
}

static void httpServer(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(sfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind() failed on port " << port << "\n"; close(sfd); return;
    }
    listen(sfd, 10);
    std::cout << "\n  Open in browser:  http://localhost:" << port << "\n" << std::endl;
    while (g_running) {
        sockaddr_in ca{}; socklen_t cl = sizeof(ca);
        int cfd = accept(sfd, (sockaddr*)&ca, &cl);
        if (cfd >= 0) std::thread(handleClient, cfd).detach();
    }
    close(sfd);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int port = 18080, width = 640, height = 480, fps = 30;
    if (argc > 1) port = std::atoi(argv[1]);

    std::cout << "=== XNBY Cloud Rendering Demo (Interactive) ===" << std::endl;
    std::cout << "Resolution: " << width << "x" << height << " @ " << fps << " FPS" << std::endl;

    std::thread(httpServer, port).detach();

    Framebuffer fb; fb.resize(width, height);
    auto t0 = std::chrono::steady_clock::now();
    auto interval = std::chrono::microseconds(1000000 / fps);
    uint32_t frame = 0;

    while (g_running) {
        auto fs = std::chrono::steady_clock::now();
        float time = std::chrono::duration<float>(fs - t0).count();

        renderScene(fb, time);
        auto jpeg = encodeJPEG(fb, 82);
        { std::lock_guard<std::mutex> lk(g_frame_mutex); g_latest_jpeg = std::move(jpeg.data); }
        g_frame_number.store(++frame);

        auto fe = std::chrono::steady_clock::now();
        auto el = fe - fs;
        if (el < interval) std::this_thread::sleep_for(interval - el);

        if (frame % (fps * 5) == 0) {
            float sec = std::chrono::duration<float>(fe - t0).count();
            std::cout << "Frame " << frame << " | " << (int)(frame/sec) << " FPS | "
                      << g_client_count.load() << " clients | "
                      << g_latest_jpeg.size()/1024 << " KB/frame" << std::endl;
        }
    }
    return 0;
}
