// DynamicScene — 云渲染运行时动态场景(标绘四件套 + 图层)。
//
// datachannel 上的命令协议(见 viewer_hub 输入回调路由)增删要素,渲染循环
// 每帧从这里取 draw。要素 payload 直接复用编辑器的标注 JSON(kind/mode/
// vertices/style),服务端用 ht_scene AnnotationManager 反序列化成同样的
// Annotation 对象,走与编辑器同一批引擎渲染器(Polyline/Polygon/Billboard/
// LightWall)。线程安全:datachannel 线程写、渲染线程读,一把互斥锁。
//
// 协议(JSON):
//   {"ns":"polyline|polygon|marker|arrow", "op":"add",    "element":{...}}
//   {"ns":"...",                            "op":"update", "id":N, "element":{...}}
//   {"ns":"...",                            "op":"remove", "id":N}
//   {"ns":"scene",                          "op":"clear"}
//   {"ns":"layer",                          "op":"show|hide", "id":L}
// element = 标注 JSON;marker→symbol、其余 kind 同名。mode 默认 local。
#pragma once

#include <hongtu/scene/annotation/annotation_manager.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>
#include <hongtu/scene/annotation/polygon_smooth.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <hongtu/renderer/effects/polygon_renderer.hpp>
#include <hongtu/renderer/effects/billboard_renderer.hpp>
#include <hongtu/renderer/effects/light_wall_renderer.hpp>
#include <hongtu/renderer/vegetation/vegetation_layer.hpp>
#include <hongtu/renderer/effects/particle_renderer.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/asset/GltfLoader.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include "drill_models.hpp"
#include "drill_avatar.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crs {

using namespace HongTu::RHI;

class DynamicScene {
 public:
  /// `atlas_rgba`(aw×ah RGBA8,调用方用 stb 加载)+ icon json → marker atlas。
  /// atlas 为空仍可跑(marker 不显示)。
  bool initialize(RHIDevice* device, RHIRenderPassPtr hdr_pass,
                  const uint8_t* atlas_rgba, int aw, int ah,
                  const std::string& icon_json,
                  hongtu::renderer::PbrMaterialFactory* mf = nullptr,
                  RHIDescriptorSetLayoutPtr shadow_layout = nullptr,
                  RHIDescriptorSetLayoutPtr ibl_layout = nullptr) {
    device_ = device;
    if (!polyline_.initialize(device, hdr_pass)) return false;
    if (!polygon_.initialize(device, hdr_pass)) return false;
    if (!light_wall_.initialize(device, hdr_pass)) return false;
    // 动态模型(运行时 glb):自有 VegetationLayer,1 实例/模型(复用 project
    // 模型路径)。需要材质工厂 + 阴影集布局 + HDR pass + (可选)IBL 集布局
    // —— 与主 veg 层共享同一程序化 IBL(set=2),云端观感对齐编辑器。
    if (mf && models_veg_.initialize(device, mf, shadow_layout, hdr_pass, ibl_layout)) {
      models_ready_ = true;
    }
    fire_ready_ = fire_renderer_.initialize(device, hdr_pass);
    // 人员骨骼层(CesiumMan):带 anim 的人员要素走蒙皮渲染而非静态实例。
    // 引擎自带资产用绝对根路径定位(agent posix_spawn 不改 cwd,相对路径不可靠)。
    avatars_.initialize(device, hdr_pass,
                        std::string(HT_CRS_ENGINE_ROOT) + "/assets/characters/CesiumMan.glb");
    if (atlas_rgba && aw > 0 && ah > 0 && loadAtlas(atlas_rgba, aw, ah, icon_json) &&
        billboard_.initialize(device, hdr_pass, atlas_view_, atlas_sampler_)) {
      has_atlas_ = true;
    } else {
      spdlog::warn("[scene] icon atlas unavailable — markers won't render");
    }
    return true;
  }

  /// datachannel 命令(已 parse 的 JSON)。返回 true 表示是场景命令(已消费)。
  bool handleCommand(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("ns")) return false;
    const std::string ns = j.value("ns", "");
    const std::string op = j.value("op", "");
    std::lock_guard<std::mutex> lock(m_);
    if (ns == "scene" && op == "clear") {
      mgr_.clear();
      // 同时清运行时模型与火苗,否则 glb/粒子残留(图层显隐状态保留)。
      for (auto& [id, arch] : model_arch_) models_veg_.setInstances(arch, {});
      model_arch_.clear();
      model_tint_.clear();
      model_nozzle_.clear();
      unit_xform_.clear();
      avatars_.clear();
      fires_.clear();
      return true;
    }
    if (ns == "layer") {
      const uint32_t lid = j.value("id", 0u);
      if (op == "show") layer_hidden_.erase(lid);
      else if (op == "hide") layer_hidden_.insert(lid);
      return true;
    }
    if (ns == "fire") {
      const uint64_t id = j.value("id", 0ull);
      if (op == "remove") { fires_.erase(id); return true; }
      if ((op == "add" || op == "update") && fire_ready_ && j.contains("element")) {
        const auto& e = j["element"];
        glm::vec3 pos(0.0f);
        if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 3)
          pos = {e["pos"][0].get<float>(), e["pos"][1].get<float>(), e["pos"][2].get<float>()};
        // 灾害按 type 选真实粒子预设(池火/喷射火/烟雾/毒气/火花),scale/wind 生效。
        const std::string type = e.value("type", "pool_fire");
        auto& sysptr = fires_[id];
        // 2048 容量:高发射率(喷射火 220/s × scale)下不丢粒子。
        if (!sysptr) sysptr = std::make_unique<hongtu::renderer::ParticleSystem>(2048);
        if (type == "water") {
          // 喷水:车辆 glb 有命名炮口挂点则按其真实位置/朝向发射,否则用前端兜底偏移。
          glm::vec3 wpos = pos;
          nlohmann::json ee = e;
          applyNozzle(id, wpos, ee);
          sysptr->setEmitter(disasterEmitter(type, wpos, ee));
        } else {
          sysptr->setEmitter(disasterEmitter(type, pos, e));
        }
        return true;
      }
      return true;
    }
    if (ns == "model") {
      const uint64_t id = j.value("id", 0ull);
      // 预加载工程模型(几何已烘焙)→ 改变换 / 显隐,不走动态 archetype 路径。
      auto sit = static_models_.find(id);
      if (sit != static_models_.end()) {
        auto& sm = sit->second;
        if (op == "remove") {
          if (sm.set_visible) sm.set_visible(false);
          return true;
        }
        if (op == "add" || op == "update") {
          if (sm.set_visible) sm.set_visible(true);   // 再 add/update = 重新显示
          if (j.contains("element")) {
            const auto& e = j["element"];
            if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 3)
              sm.pos = {e["pos"][0].get<float>(), e["pos"][1].get<float>(),
                        e["pos"][2].get<float>()};
            if (e.contains("rot")) sm.rot_deg.z = e.value("rot", sm.rot_deg.z);  // 仅 yaw 覆盖
            if (e.contains("scale")) { float s = e.value("scale", 1.0f); sm.scale = glm::vec3(s); }
          }
          glm::mat4 M(1.0f);
          M = glm::translate(M, sm.pos);
          M = glm::rotate(M, glm::radians(sm.rot_deg.z), glm::vec3(0, 0, 1));
          M = glm::rotate(M, glm::radians(sm.rot_deg.y), glm::vec3(0, 1, 0));
          M = glm::rotate(M, glm::radians(sm.rot_deg.x), glm::vec3(1, 0, 0));
          M = glm::scale(M, sm.scale);
          // 渲染线程无锁读 model_matrix:最坏一帧撕裂变换,可接受(viewer 控制面)。
          if (sm.set_matrix) sm.set_matrix(M);
          return true;
        }
        return true;
      }
      if (op == "remove") {
        auto it = model_arch_.find(id);
        if (it != model_arch_.end()) { models_veg_.setInstances(it->second, {}); }
        model_tint_.erase(id);
        model_nozzle_.erase(id);
        unit_xform_.erase(id);
        avatars_.remove(id);
        return true;
      }
      if ((op == "add" || op == "update") && j.contains("element")) {
        const auto& e = j["element"];
        // 骨骼人物(element.anim 非空)→ 蒙皮人物层(CesiumMan walk),不走静态实例。
        if (!e.value("anim", std::string()).empty() && avatars_.ready()) {
          glm::vec3 p(0.0f);
          if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 3)
            p = {e["pos"][0].get<float>(), e["pos"][1].get<float>(), e["pos"][2].get<float>()};
          const float yaw = glm::radians(e.value("rot", 0.0f));
          if (avatars_.has(id)) avatars_.update(id, p, yaw);
          else                  avatars_.add(id, p, yaw);
          return true;
        }
        if (!models_ready_) return true;
        const std::string url = e.value("url", "");   // drill://kind/subtype(程序化身份/兜底)
        const std::string glb = e.value("glb", "");   // 工程 assets 相对路径(真模型,可选)
        hongtu::renderer::VegetationInstance v;
        if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 3)
          v.pos = {e["pos"][0].get<float>(), e["pos"][1].get<float>(), e["pos"][2].get<float>()};
        if (e.contains("rot")) v.yaw = glm::radians(e.value("rot", 0.0f));
        v.scale = e.value("scale", 1.0f);
        int arch = -1;
        auto it = model_arch_.find(id);
        if (it != model_arch_.end()) {
          arch = it->second;  // update:原型不变,只移动/缩放实例
        } else {
          std::optional<hongtu::asset::ModelData> res;
          bool procedural = false;
          // ① 优先工程内真模型(以工程根为基的相对路径,随工程分发)。
          if (!glb.empty()) {
            res = loader_.load(resolveModel(glb));
            if (!res) spdlog::warn("[scene] drill 模型 glb 缺失,回退程序化占位: {}", glb);
          }
          // ② 回退:drill:// 程序化占位(分色胶囊人/盒子车)。
          if (!res && url.rfind("drill://", 0) == 0) { res = drs::buildDrillModel(url); procedural = !!res; }
          // ③ 普通 glb url(非 drill 场景)。
          if (!res && !url.empty()) res = loader_.load(resolveModel(url));
          if (!res) { spdlog::warn("[scene] model not found: glb='{}' url='{}'", glb, url); return true; }
          arch = models_veg_.addArchetype(*res);
          if (arch < 0) return true;
          models_veg_.setSwayAmplitude(arch, 0.0f);  // 模型不摇摆
          model_arch_[id] = arch;
          // 程序化占位按岗位/车种上色(VegetationLayer 着色器只认实例色);
          // 真模型(glb)用自带 PBR 材质 → 实例色保持白不覆盖。
          model_tint_[id] = procedural ? drs::drillColor(url) : glm::vec4(1.0f);
          // 真 glb:按固定命名契约清点车辆 rig 挂点(nozzle 即驱动喷水,其余预留)。
          if (!procedural) inventoryVehicleRig(id, *res);
        }
        // 记录单位当前世界变换(喷水实体随车跟随时据此重算炮口)。
        unit_xform_[id] = UnitXform{v.pos, v.yaw, v.scale};
        auto tit = model_tint_.find(id);
        v.base_color = (tit != model_tint_.end()) ? tit->second : glm::vec4(1.0f);
        models_veg_.setInstances(arch, {v});
        return true;
      }
      return true;
    }
    // 要素命名空间 → 标注 kind。
    auto toKind = [](const std::string& n) -> const char* {
      if (n == "polyline") return "polyline";
      if (n == "polygon")  return "polygon";
      if (n == "marker")   return "symbol";
      if (n == "arrow")    return "arrow";
      return nullptr;
    };
    const char* kind = toKind(ns);
    if (!kind) return false;
    if (op == "remove") {
      mgr_.remove(j.value("id", 0ull));
      return true;
    }
    if (op == "add" || op == "update") {
      if (!j.contains("element")) return true;
      nlohmann::json el = j["element"];
      el["kind"] = kind;
      if (!el.contains("mode")) el["mode"] = "local";
      if (op == "update" && j.contains("id")) {
        const uint64_t id = j["id"].get<uint64_t>();
        mgr_.remove(id);
        el["id"] = id;
      }
      mgr_.addFromJson(el);
      return true;
    }
    return false;
  }

  /// 注册一个预加载工程模型,使动态接口能按 id update(改变换)/remove(隐藏)。
  /// `set_matrix` 把新 T·Rz·Ry·Rx·S 写回渲染端;`set_visible` 切显隐(几何已烘焙,
  /// remove 语义=隐藏而非删几何)。startup 阶段调用(datachannel 线程尚未起)。
  void registerStaticModel(uint64_t id, const glm::vec3& pos, const glm::vec3& rot_deg,
                           const glm::vec3& scale,
                           std::function<void(const glm::mat4&)> set_matrix,
                           std::function<void(bool)> set_visible) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(m_);
    static_models_[id] = StaticModelHook{pos, rot_deg, scale,
                                         std::move(set_matrix), std::move(set_visible)};
  }

  /// 预建一个工程火苗进 fires_(与运行时火苗同一渲染/寻址路径),动态接口可按 id
  /// update(移动/换参)/remove。只传 enabled 的火苗(presence in fires_ = 点燃)。
  void seedFire(uint64_t id, const std::string& type, const glm::vec3& pos, float scale) {
    if (!fire_ready_ || id == 0) return;
    std::lock_guard<std::mutex> lock(m_);
    auto& sysptr = fires_[id];
    if (!sysptr) sysptr = std::make_unique<hongtu::renderer::ParticleSystem>(2048);
    nlohmann::json e;
    e["scale"] = scale;
    sysptr->setEmitter(disasterEmitter(type, pos, e));
  }

  /// 每帧记录(在 ForwardSceneRenderer 的 HDR 钩子里调)。local 米制场景,
  /// view_proj=proj*view、eye 即相机位;无 RTC 原点。
  void record(const RHICommandBufferPtr& cmd, const glm::mat4& view_proj,
              const glm::vec3& eye, float time_s) {
    namespace S = hongtu::scene;
    std::vector<hongtu::renderer::PolylineRenderer::PolylineDraw> lines;
    std::vector<hongtu::renderer::PolygonRenderer::PolygonDraw>   fills;
    std::vector<hongtu::renderer::LightWallRenderer::WallDraw>    walls;
    std::vector<hongtu::renderer::BillboardRenderer::BillboardDraw> marks;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (mgr_.empty()) return;
      const hongtu::coordinate::PrecisionOrigin origin{};  // local 恒等
      using K = S::AnnotationKind;
      for (auto* a : mgr_.all()) {
        if (!a || !a->visible()) continue;
        if (layer_hidden_.count(a->layerId())) continue;
        auto payload = a->toRenderPayload(origin);
        const auto& st = payload.style;
        switch (a->getKind()) {
          case K::Polyline:
          case K::Arrow: {
            if (payload.rtc_vertices.size() < 2) break;
            if (st.corner_smooth > 0)
              payload.rtc_vertices =
                  S::smoothPolylineChaikin(payload.rtc_vertices, st.corner_smooth);
            if (st.line_type == S::LineType::Arc)
              payload.rtc_vertices = hongtu::renderer::buildArcPolyline(
                  payload.rtc_vertices, st.arc_height_ratio);
            lines.push_back(makeLine(payload.rtc_vertices, st,
                                     a->getKind() == K::Arrow, false));
            break;
          }
          case K::Polygon: {
            if (payload.rtc_vertices.size() < 3) break;
            if (st.corner_smooth > 0)
              payload.rtc_vertices =
                  S::smoothPolygonChaikin(payload.rtc_vertices, st.corner_smooth);
            if (st.filled || st.fill_type != S::FillType::Solid) {
              hongtu::renderer::PolygonRenderer::PolygonDraw d;
              d.points = payload.rtc_vertices;
              d.color  = st.color;
              d.fill_type   = static_cast<int>(st.fill_type);
              d.fill_params = glm::vec4(st.fill_param, 0, 0, 0);
              fills.push_back(std::move(d));
            }
            if (st.wall) {
              hongtu::renderer::LightWallRenderer::WallDraw w;
              w.boundary = payload.rtc_vertices;
              w.height = st.wall_height; w.color = st.color;
              w.closed = true; w.flow_speed = st.wall_flow;
              walls.push_back(std::move(w));
            }
            lines.push_back(makeLine(payload.rtc_vertices, st, false, true));
            break;
          }
          case K::Symbol: {
            if (!has_atlas_ || payload.rtc_vertices.empty()) break;
            uint32_t si = st.symbol_index; if (si > 15) si = 0;
            float s = std::max(0.5f, st.symbol_size_m);
            hongtu::renderer::BillboardRenderer::BillboardDraw bd;
            bd.world_pos = payload.rtc_vertices[0];
            bd.size_m    = glm::vec2(s, s);
            bd.atlas_uv  = atlas_.lookupUVRect(kIcon(si));
            bd.color     = st.color;
            bd.effect    = static_cast<float>(st.symbol_effect);
            bd.phase     = st.symbol_freq;
            marks.push_back(bd);
            if (st.symbol_effect == 3) {  // 涟漪:环 + 静态图标
              marks.back().effect = 3.0f;
              bd.effect = 0.0f;
              marks.push_back(bd);
            }
            break;
          }
          default: break;  // Text 云端无 SDF,跳过
        }
      }
    }
    // GPU 记录(锁外)。
    if (!fills.empty()) {
      hongtu::renderer::PolygonRenderer::Params pp; pp.view_proj = view_proj; pp.time_s = time_s;
      polygon_.record(cmd, pp, fills);
    }
    if (!walls.empty()) {
      hongtu::renderer::LightWallRenderer::Params wp; wp.view_proj = view_proj; wp.time_s = time_s;
      light_wall_.record(cmd, wp, walls);
    }
    if (!lines.empty()) {
      hongtu::renderer::PolylineRenderer::Params lp;
      lp.view_proj = view_proj; lp.camera_pos_rtc = eye; lp.time_s = time_s;
      polyline_.record(cmd, lp, lines);
    }
    if (!marks.empty() && has_atlas_) {
      hongtu::renderer::BillboardRenderer::Params bp;
      bp.view_proj = view_proj;
      bp.camera_right = glm::vec3(glm::transpose(glm::mat3(view_proj))[0]);  // 近似;下方覆盖
      bp.camera_up    = glm::vec3(0, 0, 1);
      bp.camera_pos_rtc = eye; bp.time_s = time_s;
      // billboard 朝相机:right/up 取自 view 矩阵行。view_proj 不含纯 view,
      // 这里用调用方传的 cam basis 更准 —— 见 setCameraBasis。
      bp.camera_right = cam_right_;
      bp.camera_up    = cam_up_;
      billboard_.record(cmd, bp, marks);
    }
  }

  /// 动态模型(运行时 glb)记录 —— 复用 park veg 的光照 push constants。
  /// 在 HDR 钩子里、annotation record 之后调。
  void recordModels(const RHICommandBufferPtr& cmd,
                    hongtu::renderer::VegetationPipeline::PushConstants vpc,
                    const RHIDescriptorSetPtr& shadow_set,
                    const glm::mat4& view_proj, const glm::vec3& eye,
                    const RHIDescriptorSetPtr& ibl_set = nullptr) {
    if (!models_ready_) return;
    std::lock_guard<std::mutex> lock(m_);
    if (model_arch_.empty()) return;
    models_veg_.update(view_proj, eye);
    vpc.view_proj = view_proj;
    models_veg_.record(cmd, vpc, shadow_set, ibl_set);
  }

  /// 灾害类型 → 粒子发射器预设(从已删的 drill_render_server presetFor 移植):
  /// 池火/喷射火/烟雾/毒气羽流/火花 各自色带+形态;scale 真正变大变猛(发射率+
  /// 尺寸+速度同放大,非仅变密),wind 把烟气吹斜。所有灾害走真实粒子,不再是符号。
  static hongtu::renderer::EmitterDesc disasterEmitter(const std::string& type,
                                                       glm::vec3 pos,
                                                       const nlohmann::json& e) {
    using namespace hongtu::renderer;
    const float scale = std::clamp(e.value("scale", 1.0f), 0.3f, 5.0f);
    glm::vec3 wind{0.0f};
    if (e.contains("wind") && e["wind"].is_array() && e["wind"].size() >= 2)
      wind = {e["wind"][0].get<float>(), e["wind"][1].get<float>(), 0.0f};

    EmitterDesc d;
    if (type == "water") {
      // 消防/洒水喷水:沿 dir 定向射流 + 重力抛物线 + 蓝白水珠。射程由初速²/g 决定
      // (~22 m/s ≈ 30~50 m);"喷多大"全在这些粒子参数,与模型尺寸解耦。
      glm::vec3 dir(1.0f, 0.0f, 0.5f);
      if (e.contains("dir") && e["dir"].is_array() && e["dir"].size() >= 3)
        dir = {e["dir"][0].get<float>(), e["dir"][1].get<float>(), e["dir"][2].get<float>()};
      if (glm::dot(dir, dir) < 1e-6f) dir = {1.0f, 0.0f, 0.5f};
      dir = glm::normalize(dir);
      // power=射程/水量(默认 1≈22m/s),spread=锥角(默认窄柱)。前端可调。
      const float power  = std::clamp(e.value("power", 1.0f), 0.2f, 3.0f);
      const float spread = std::clamp(e.value("spread", 0.08f), 0.02f, 0.6f);
      d = EmitterPresets::makeJet(pos, dir, 300.0f);
      d.initial_velocity = dir * (22.0f * power);        // 射程随 power
      d.acceleration     = glm::vec3(0.0f, 0.0f, -9.8f); // 抛物线下坠
      d.cone_half_angle  = spread;                       // 集中水柱 / 喷雾
      d.initial_size     = 0.18f;
      d.size_growth      = 0.25f;
      d.lifetime_seconds = 2.6f;
      d.drag             = 0.02f;
      d.color_ramp.colors[0] = {0.78f, 0.88f, 1.0f, 0.70f};  // 蓝白
      d.color_ramp.colors[1] = {0.62f, 0.76f, 0.96f, 0.50f};
      d.color_ramp.colors[2] = {0.50f, 0.66f, 0.90f, 0.28f};
      d.color_ramp.colors[3] = {0.42f, 0.58f, 0.85f, 0.0f};
    } else if (type == "jet_fire") {
      d = EmitterPresets::makeJet(pos, glm::normalize(glm::vec3(1.0f, 0.0f, 0.6f)), 220.0f);
    } else if (type == "smoke") {
      d = EmitterPresets::makeSmoke(pos, glm::vec3(0.0f), 60.0f);
    } else if (type == "gas_plume") {
      d = EmitterPresets::makeSmoke(pos, glm::vec3(0.0f), 80.0f);
      d.color_ramp.colors[0] = {0.55f, 0.75f, 0.25f, 0.55f};  // 黄绿毒气
      d.color_ramp.colors[1] = {0.45f, 0.65f, 0.20f, 0.40f};
      d.color_ramp.colors[2] = {0.35f, 0.50f, 0.18f, 0.22f};
      d.color_ramp.colors[3] = {0.25f, 0.35f, 0.15f, 0.0f};
    } else if (type == "sparks") {
      d = EmitterPresets::makeSparks(pos, 180.0f);
    } else {
      d = EmitterPresets::makeFire(pos, 90.0f);  // pool_fire(默认)
    }
    d.emit_rate        *= scale;
    d.sphere_radius    *= scale;
    d.half_extent      *= scale;
    d.initial_size     *= std::sqrt(scale);
    d.initial_velocity *= (0.55f + 0.45f * scale);
    d.size_growth      *= scale;
    d.acceleration.x   += wind.x;
    d.acceleration.y   += wind.y;
    return d;
  }

  /// 火苗:每帧推进所有粒子系统 + 合批记录(纯色 quad,无纹理)。
  void recordFires(const RHICommandBufferPtr& cmd, const glm::mat4& view_proj,
                   float dt) {
    if (!fire_ready_) return;
    std::vector<hongtu::renderer::ParticleInstance> all, buf;
    {
      std::lock_guard<std::mutex> lock(m_);
      if (fires_.empty()) return;
      for (auto& [id, sys] : fires_) {
        sys->update(dt);
        sys->buildInstanceBuffer(buf);
        all.insert(all.end(), buf.begin(), buf.end());
      }
    }
    if (all.empty()) return;
    hongtu::renderer::ParticleRenderer::Params pp;
    pp.view_proj = view_proj; pp.camera_right = cam_right_; pp.camera_up = cam_up_;
    fire_renderer_.record(cmd, pp, all);
  }

  /// 人员骨骼动画推进(主循环每帧)。
  void tickAvatars(float dt) {
    std::lock_guard<std::mutex> lock(m_);
    avatars_.tick(dt);
  }

  /// 人员骨骼绘制(CesiumMan walk)—— 在 HDR 钩子里、与 recordModels 同处调。
  void recordAvatars(const RHICommandBufferPtr& cmd, const glm::mat4& view_proj,
                     const glm::vec3& sun_dir, const glm::vec3& sun_color, float sun_intensity) {
    std::lock_guard<std::mutex> lock(m_);
    if (avatars_.empty()) return;
    avatars_.record(cmd, view_proj, sun_dir, sun_color, sun_intensity);
  }

  /// 每帧由主循环喂入相机右/上向量(billboard 朝向)。
  void setCameraBasis(const glm::vec3& right, const glm::vec3& up) {
    cam_right_ = right; cam_up_ = up;
  }

  size_t count() {
    std::lock_guard<std::mutex> lock(m_);
    return mgr_.size();
  }

 public:
  /// 设工程根(cloud_render_server 的 --open 目录)。要素模型 glb 随工程打包,
  /// catalog 配的是「以工程 assets 为基的相对路径」,故须优先按工程根解析。
  void setProjectRoot(const std::string& root) { project_root_ = root; }

 private:
  // 喷水发射点解析:水柱实体 id = 单位 id × 100000 + 5(见前端 sprayId)。若该单位的
  // 模型 glb 带命名挂点(nozzle 等),按【单位世界变换 · 节点局部】把炮口位置/朝向算到
  // 世界,覆盖前端基于固定偏移的兜底 pos/dir;无挂点则原样沿用(返回 false)。
  // 世界变换与 VegetationLayer::composeInstanceMatrix 完全一致:T·Rz(yaw)·Rx(+90°)·S。
  bool applyNozzle(uint64_t spray_id, glm::vec3& pos, nlohmann::json& e) {
    if (spray_id < 5 || spray_id % 100000 != 5) return false;
    const uint64_t unit = (spray_id - 5) / 100000;
    auto nit = model_nozzle_.find(unit);
    auto xit = unit_xform_.find(unit);
    if (nit == model_nozzle_.end() || xit == unit_xform_.end()) return false;
    const auto& nz = nit->second;
    const auto& xf = xit->second;
    glm::mat4 W(1.0f);
    W = glm::translate(W, xf.pos);
    W = glm::rotate(W, xf.yaw, glm::vec3(0, 0, 1));
    W = glm::rotate(W, glm::half_pi<float>(), glm::vec3(1, 0, 0));  // Y-up→Z-up
    W = glm::scale(W, glm::vec3(xf.scale));
    const glm::vec3 origin = glm::vec3(W * glm::vec4(nz.local_pos, 1.0f));
    glm::vec3 dir = glm::vec3(W * glm::vec4(nz.local_dir, 0.0f));
    if (glm::dot(dir, dir) < 1e-8f) return false;
    dir = glm::normalize(dir);
    pos = origin;
    e["dir"] = {dir.x, dir.y, dir.z};
    return true;
  }

  // 车辆 rig 命名契约:加载真 glb 时按【固定节点名】识别挂点,免每模型单独配置。
  //   nozzle / water_outlet / muzzle   —— 喷水发射点(节点 +X = 喷射方向)【已驱动】
  //   wheel_fl / wheel_fr / wheel_rl / wheel_rr —— 四轮(原点在轮心)【预留】
  //   turret —— 炮塔(水平回转)【预留】   cannon —— 炮管(俯仰)【预留】
  // 现状:nozzle 命中即用于喷水;其余仅清点 + 落日志,供建模核对命名是否到位。
  // 待 VegetationLayer 支持 sub-node articulation(轮子滚动/炮塔回转)后,今天按此
  // 命名好的模型直接生效,无需重做。约定先行 = 模型一次做对、能力逐步点亮。
  void inventoryVehicleRig(uint64_t id, const hongtu::asset::ModelData& m) {
    // 喷水挂点(已驱动):取局部位置 + 节点 +X 轴为喷射方向。
    for (const char* nn : {"nozzle", "water_outlet", "muzzle"}) {
      if (auto nw = m.findNodeWorldTransform(nn)) {
        Nozzle nz;
        nz.local_pos = glm::vec3((*nw)[3]);
        glm::vec3 d = glm::mat3(*nw) * glm::vec3(1.0f, 0.0f, 0.0f);
        nz.local_dir = glm::dot(d, d) > 1e-8f ? glm::normalize(d) : glm::vec3(1, 0, 0);
        model_nozzle_[id] = nz;
        break;
      }
    }
    // 预留挂点清点(命名契约核对;暂不驱动)。
    int wheels = 0;
    for (const char* wn : {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"})
      if (m.findNodeWorldTransform(wn)) ++wheels;
    const bool turret = m.findNodeWorldTransform("turret").has_value();
    const bool cannon = m.findNodeWorldTransform("cannon").has_value();
    if (model_nozzle_.count(id) || wheels || turret || cannon) {
      spdlog::info("[scene] 模型 {} rig: nozzle={} wheels={}/4 turret={} cannon={} "
                   "(nozzle 即驱动喷水,其余预留)",
                   id, model_nozzle_.count(id) ? "✓" : "—", wheels,
                   turret ? "✓" : "—", cannon ? "✓" : "—");
    }
  }

  // glb 路径解析:① 工程根相对(模型随工程分发)② 绝对/cwd ③ 引擎内置 assets 兜底。
  std::string resolveModel(const std::string& url) {
    namespace fs = std::filesystem;
    if (!project_root_.empty()) {
      const std::string p = project_root_ + "/" + url;   // 如 <工程>/assets/drill/x.glb
      if (fs::exists(p)) return p;
    }
    if (fs::exists(url)) return url;
    for (const char* base : {"assets/", "assets/park/props/", "assets/park/trees/"}) {
      const std::string p = base + url;
      if (fs::exists(p)) return p;
    }
    return url;
  }

  static const char* kIcon(uint32_t i) {
    static const char* k[16] = {"warning","danger","hazard","fire","hydrant",
      "extinguisher","camera","sensor","exit","assembly","first_aid","phone",
      "person","vehicle","tank","valve"};
    return k[i & 15];
  }

  hongtu::renderer::PolylineRenderer::PolylineDraw
  makeLine(const std::vector<glm::vec3>& pts, const hongtu::scene::AnnotationStyle& st,
           bool arrow_kind, bool closed) {
    hongtu::renderer::PolylineRenderer::PolylineDraw d;
    d.points = pts;
    d.color  = closed ? glm::vec4(st.color.r, st.color.g, st.color.b, 1.0f) : st.color;
    d.width_m = st.stroke_width_m;
    d.closed  = closed;
    d.dashed = st.dash_period_m > 0.0f;
    d.dash_size_m = st.dash_period_m; d.dash_gap_ratio = 0.5f;
    d.arrow_head = arrow_kind || st.arrow_head;
    d.arrow_tail = st.arrow_tail;
    d.line_type = static_cast<int>(st.line_type);
    d.outline_color = st.outline_color;
    d.glow_power = st.glow_power; d.flow_speed = st.flow_speed; d.tex_tiling = st.line_tex_tiling;
    const std::string& tx = st.line_texture;
    d.texture_slot = (tx == "dash") ? 1 : (tx == "dots") ? 2 : (tx == "band") ? 3 : 0;
    d.on_top = (st.line_placement == hongtu::scene::LinePlacement::OnTop);
    return d;
  }

  bool loadAtlas(const uint8_t* px, int w, int h, const std::string& json) {
    TextureDesc td;
    td.type = TextureType::Texture2D;
    td.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
    td.mipLevels = 1u; td.arrayLayers = 1u; td.format = Format::R8G8B8A8_UNORM;
    td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
    td.sampleCount = 1u;
    atlas_tex_ = device_->createTexture(td);
    if (atlas_tex_) {
      RHIDevice::MipData m0; m0.width = w; m0.height = h; m0.data = px;
      m0.dataSize = static_cast<size_t>(w) * h * 4;
      device_->uploadTextureData(atlas_tex_, {m0}, 0u);
      atlas_view_ = atlas_tex_->createView();
    }
    if (!atlas_view_) return false;
    SamplerDesc sd;
    sd.magFilter = SamplerFilter::Linear; sd.minFilter = SamplerFilter::Linear;
    sd.mipmapMode = SamplerMipmapMode::Linear;
    sd.addressModeU = SamplerAddressMode::ClampToEdge;
    sd.addressModeV = SamplerAddressMode::ClampToEdge;
    sd.addressModeW = SamplerAddressMode::ClampToEdge;
    atlas_sampler_ = device_->createSampler(sd);
    if (!atlas_sampler_) return false;
    return atlas_.loadFromFile(json);
  }

  RHIDevice* device_ = nullptr;
  std::mutex m_;
  hongtu::scene::AnnotationManager mgr_;
  std::unordered_set<uint32_t>     layer_hidden_;

  hongtu::renderer::PolylineRenderer  polyline_;
  hongtu::renderer::PolygonRenderer   polygon_;
  hongtu::renderer::LightWallRenderer light_wall_;
  hongtu::renderer::BillboardRenderer billboard_;
  hongtu::renderer::BillboardAtlas    atlas_;
  RHITexturePtr     atlas_tex_;
  RHITextureViewPtr atlas_view_;
  RHISamplerPtr     atlas_sampler_;
  bool has_atlas_ = false;
  glm::vec3 cam_right_{1, 0, 0}, cam_up_{0, 0, 1};

  // 动态模型(运行时 glb,1 实例/模型)。
  hongtu::renderer::VegetationLayer       models_veg_;
  hongtu::asset::GltfLoader               loader_;
  bool                                    models_ready_ = false;
  std::unordered_map<uint64_t, int>       model_arch_;  // id → archetype
  std::unordered_map<uint64_t, glm::vec4> model_tint_;  // id → 实例色(程序化=配色,真模型=白)
  std::string                             project_root_;  // --open 工程根(模型相对路径基准)

  // 喷水挂点:车辆 glb 若带命名节点(nozzle/water_outlet),加载时取其【glb 空间】
  // 局部位置 + +X 朝向(=喷射方向);喷水时与单位世界变换合成出真实炮口,覆盖前端兜底。
  struct Nozzle  { glm::vec3 local_pos{0.0f}; glm::vec3 local_dir{1.0f, 0.0f, 0.0f}; };
  struct UnitXform { glm::vec3 pos{0.0f}; float yaw = 0.0f; float scale = 1.0f; };
  std::unordered_map<uint64_t, Nozzle>    model_nozzle_;  // 单位 id → 模型炮口挂点(仅真 glb 命中)
  std::unordered_map<uint64_t, UnitXform> unit_xform_;    // 单位 id → 当前世界变换(喷水跟随)

  // 人员骨骼层(CesiumMan walk):带 anim 的人员要素走这条蒙皮渲染路。
  DrillAvatarLayer                        avatars_;

  // 动态火苗(CPU 粒子系统/个)。
  hongtu::renderer::ParticleRenderer      fire_renderer_;
  bool                                    fire_ready_ = false;
  std::unordered_map<uint64_t, std::unique_ptr<hongtu::renderer::ParticleSystem>> fires_;

  // 预加载工程模型(几何已烘焙进主 PBR 实体路径,不可加/删几何,但可改变换 +
  // 显隐)。注册 id→回调,动态接口 model:update/remove 命中先走这里。
  struct StaticModelHook {
    glm::vec3 pos{0.0f}, rot_deg{0.0f}, scale{1.0f};   // 当前变换(增量更新缺省)
    std::function<void(const glm::mat4&)> set_matrix;  // 写回渲染端 model_matrix
    std::function<void(bool)>             set_visible; // 切显隐(remove=隐藏)
  };
  std::unordered_map<uint64_t, StaticModelHook> static_models_;
};

}  // namespace crs
