#pragma once
#include <hongtu/renderer/effects/billboard_renderer.hpp>
#include <hongtu/renderer/effects/polygon_renderer.hpp>

#include "landscape_document.hpp"

#include <cstring>
#include <map>
#ifdef _WIN32
#  include <windows.h>
#endif

namespace hongtu::landscape {
  // Annotations deliberately draw as a map overlay. Saved coordinates are real
  // world XYZ; icon sizes and line widths are screen pixels in both applications.
  class Overlay {
  public:
    hongtu::renderer::PolygonRenderer polygons;
    hongtu::renderer::BillboardRenderer icons;
    HongTu::RHI::RHITexturePtr atlas;
    HongTu::RHI::RHITextureViewPtr atlasView;
    HongTu::RHI::RHISamplerPtr sampler;
    size_t renderedFeatures = 0;
    struct TextTexture {
      int slot = -1, width = 0, height = 0;
    };
    std::map<std::string, TextTexture> textTextures;
    TextTexture textTexture(const std::string& text, int fontPx) {
      const auto key = "label:" + std::to_string(fontPx) + ":" + text;
      if (auto found = textTextures.find(key); found != textTextures.end())
        return found->second;
      TextTexture result;
#ifdef _WIN32
      int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
      std::wstring wide(count, 0);
      MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), count);
      HDC dc = CreateCompatibleDC(nullptr);
      HFONT font = CreateFontW(-fontPx,
                               0,
                               0,
                               0,
                               FW_MEDIUM,
                               FALSE,
                               FALSE,
                               FALSE,
                               DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY,
                               DEFAULT_PITCH,
                               L"Microsoft YaHei");
      auto oldFont = SelectObject(dc, font);
      SIZE size{};
      GetTextExtentPoint32W(dc, wide.data(), count, &size);
      result.width = std::clamp(int(size.cx) + 4, 4, 4096);
      result.height = std::max(int(size.cy) + 4, 4);
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = result.width;
      info.bmiHeader.biHeight = -result.height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void* bits = nullptr;
      auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
      if (bitmap && bits) {
        auto oldBitmap = SelectObject(dc, bitmap);
        std::memset(bits, 0, size_t(result.width) * result.height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 2, 2, wide.data(), count);
        GdiFlush();
        auto bytes = static_cast<uint8_t*>(bits);
        std::vector<uint8_t> rgba(size_t(result.width) * result.height * 4);
        for (size_t k = 0; k < rgba.size(); k += 4) {
          rgba[k] = rgba[k + 1] = rgba[k + 2] = 255;
          rgba[k + 3] = std::max({bytes[k], bytes[k + 1], bytes[k + 2]});
        }
        result.slot = icons.ensureCustomTexture(key, rgba.data(), result.width, result.height);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
      }
      SelectObject(dc, oldFont);
      DeleteObject(font);
      DeleteDC(dc);
#endif
      if (result.slot >= 0)
        textTextures.emplace(key, result);
      return result;
    }
    bool initialize(HongTu::RHI::RHIDevice* device,
                    HongTu::RHI::RHIRenderPassPtr pass,
                    const std::filesystem::path& atlasFile) {
      using namespace HongTu::RHI;
      int w = 0, h = 0, c = 0;
      auto bytes = stbi_load(atlasFile.string().c_str(), &w, &h, &c, 4);
      if (!bytes)
        throw std::runtime_error("无法读取标注图标集");
      TextureDesc td;
      td.extent = {uint32_t(w), uint32_t(h), 1};
      td.format = Format::R8G8B8A8_UNORM;
      td.usage = TextureUsageFlagBits::Sampled | TextureUsageFlagBits::TransferDst;
      atlas = device->createTexture(td);
      if (!atlas) {
        stbi_image_free(bytes);
        return false;
      }
      RHIDevice::MipData mip;
      mip.width = w;
      mip.height = h;
      mip.depth = 1;
      mip.data = bytes;
      mip.dataSize = size_t(w) * h * 4;
      device->uploadTextureData(atlas, {mip}, 0);
      stbi_image_free(bytes);
      atlasView = atlas->createView();
      SamplerDesc sd;
      sd.addressModeU = SamplerAddressMode::ClampToEdge;
      sd.addressModeV = SamplerAddressMode::ClampToEdge;
      sampler = device->createSampler(sd);
      return polygons.initialize(device, pass) && icons.initialize(device, pass, atlasView, sampler);
    }
    static glm::vec4 color(const std::string& s, float opacity) {
      unsigned v = unsigned(std::stoul(s.substr(1), nullptr, 16));
      return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, opacity};
    }
    static std::optional<glm::vec2> project(glm::vec3 p, const glm::mat4& vp, int width, int height) {
      auto clip = vp * glm::vec4(p, 1);
      if (clip.w <= .001f || clip.z < 0)
        return {};
      glm::vec2 ndc = glm::vec2(clip) / clip.w;
      return (ndc + 1.f) * glm::vec2(width, height) * .5f;
    }
    void record(const HongTu::RHI::RHICommandBufferPtr& cmd,
                const Json& doc,
                const glm::mat4& vp,
                int width,
                int height,
                const Json* draft = nullptr,
                const std::string& selected = "") {
      using hongtu::renderer::PolygonVertex;
      std::vector<PolygonVertex> triangles;
      std::vector<hongtu::renderer::BillboardRenderer::BillboardDraw> points;
      renderedFeatures = 0;
      auto ndc = [&](glm::vec2 p) {
        return glm::vec3(p.x / width * 2 - 1, p.y / height * 2 - 1, 0);
      };
      auto triangle = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color) {
        triangles.push_back({ndc(a), color});
        triangles.push_back({ndc(b), color});
        triangles.push_back({ndc(c), color});
      };
      auto draw = [&](const Json& f, bool isDraft) {
        auto p = positions(f);
        auto type = f.at("type").get<std::string>();
        const auto& s = f.at("style");
        auto col = color(s.at("color"), s.at("opacity"));
        bool highlight = f.value("id", "") == selected;
        if (f.contains("label")) {
          const auto& label = f["label"];
          if (label["visible"].get<bool>() && label["opacity"].get<float>() > 0) {
            const auto text =
                label["text"].get<std::string>().empty() ? f.value("name", "") : label["text"].get<std::string>();
            auto pixel = project(isDraft ? p[0] : labelAnchor(f), vp, width, height);
            if (pixel && !text.empty()) {
              auto texture = textTexture(text, int(label["fontPx"].get<float>()));
              if (texture.slot >= 0) {
                const glm::vec2 start =
                    *pixel + glm::vec2(label["offset"][0].get<float>(), label["offset"][1].get<float>());
                const glm::vec2 end = start + glm::vec2(texture.width, texture.height);
                const auto background = glm::vec4(.035f, .07f, .1f, .8f * label["opacity"].get<float>());
                triangle(start - glm::vec2(4, 2), {end.x + 4, start.y - 2}, end + glm::vec2(4, 2), background);
                triangle(start - glm::vec2(4, 2), end + glm::vec2(4, 2), {start.x - 4, end.y + 2}, background);
                hongtu::renderer::BillboardRenderer::BillboardDraw b;
                b.world_pos = ndc((start + end) * .5f);
                b.size_m = {2.f * texture.width / width, 2.f * texture.height / height};
                b.atlas_uv = {0, 0, 1, 1};
                b.tex_slot = texture.slot;
                b.color = color(label["color"], label["opacity"]);
                points.push_back(b);
              }
            }
          }
        }
        if (highlight)
          for (const auto& vertex : p)
            if (auto pixel = project(vertex, vp, width, height)) {
              auto a = *pixel - glm::vec2(4), b = *pixel + glm::vec2(4);
              triangle(a, {b.x, a.y}, b, {1, .8f, .1f, 1});
              triangle(a, b, {a.x, b.y}, {1, .8f, .1f, 1});
            }
        if (type == "point") {
          auto pixel = project(p[0], vp, width, height);
          if (!pixel)
            return;
          std::string icon = s.at("icon");
          int slot = 0;
          if (icon == "circle")
            slot = 1;
          else if (icon == "square")
            slot = 2;
          else if (icon == "star")
            slot = 3;
          else if (icon == "warning")
            slot = 4;
          hongtu::renderer::BillboardRenderer::BillboardDraw b;
          float size = s.at("sizePx");
          b.world_pos = ndc(*pixel);
          b.size_m = {2 * size / width, 2 * size / height};
          b.atlas_uv = {slot / 5.f, 0, (slot + 1) / 5.f, 1};
          b.color = col;
          if (icon == "custom") {
            auto data = s.at("iconData").get<std::string>();
            int custom = icons.getCustomTextureSlot(data);
            if (custom < 0) {
              auto bytes = decode64(data.substr(22));
              int w = 0, h = 0, c = 0;
              if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c) || w > 2048 || h > 2048)
                throw std::runtime_error("PNG 图标尺寸不能超过 2048×2048");
              auto rgba = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c, 4);
              if (!rgba)
                throw std::runtime_error("PNG 图标无法解码");
              custom = icons.ensureCustomTexture(data, rgba, w, h);
              stbi_image_free(rgba);
              if (custom < 0)
                throw std::runtime_error("PNG 图标上传失败");
            }
            b.tex_slot = custom;
            b.atlas_uv = {0, 0, 1, 1};
            b.color = {1, 1, 1, col.a};
          }
          points.push_back(b);
          ++renderedFeatures;
          return;
        }
        if (type == "region" && !isDraft) {
          auto indices = triangulate(p);
          auto fill = color(s.at("fillColor"), s.at("fillOpacity"));
          for (size_t k = 0; k < indices.size(); k += 3) {
            auto a = project(p[indices[k]], vp, width, height), b = project(p[indices[k + 1]], vp, width, height),
                 c = project(p[indices[k + 2]], vp, width, height);
            if (a && b && c)
              triangle(*a, *b, *c, fill);
          }
        }
        float thickness = s.at("widthPx");
        std::string pattern = s.at("pattern");
        float period = pattern == "dot" ? 8.f : 20.f;
        float dash = pattern == "dot" ? 2.f : 12.f;
        float accumulated = 0;
        size_t edges = p.size() - 1 + (type == "region" && !isDraft ? 1 : 0);
        for (size_t k = 0; k < edges; ++k) {
          auto a = project(p[k], vp, width, height), b = project(p[(k + 1) % p.size()], vp, width, height);
          if (!a || !b)
            continue;
          auto diff = *b - *a;
          float len = glm::length(diff);
          if (len < .001f)
            continue;
          auto direction = diff / len;
          auto normal = glm::vec2(-direction.y, direction.x) * thickness * .5f;
          auto quad = [&](float begin, float end) {
            auto u = *a + direction * begin, v = *a + direction * end;
            triangle(u - normal, u + normal, v + normal, col);
            triangle(u - normal, v + normal, v - normal, col);
          };
          if (pattern == "solid")
            quad(0, len);
          else
            for (float t = -std::fmod(accumulated, period); t < len; t += period) {
              float lo = std::max(0.f, t), hi = std::min(len, t + dash);
              if (hi > lo)
                quad(lo, hi);
            }
          accumulated += len;
        }
        ++renderedFeatures;
      };
      for (const auto& l : doc.at("layers"))
        if (l.at("visible"))
          for (const auto& f : l.at("features"))
            draw(f, false);
      if (draft && !draft->at("positions").empty())
        draw(*draft, true);
      polygons.recordTriangles(cmd, {glm::mat4(1), 0}, triangles);
      hongtu::renderer::BillboardRenderer::Params params;
      params.view_proj = glm::mat4(1);
      params.camera_right = {1, 0, 0};
      // Identity VP already uses Vulkan's top-down screen coordinates. Positive
      // Y maps atlas row zero to the top of the quad, including rasterized text.
      params.camera_up = {0, 1, 0};
      params.camera_pos_rtc = {0, 0, 1};
      icons.record(cmd, params, points);
    }
  };
} // namespace hongtu::landscape
