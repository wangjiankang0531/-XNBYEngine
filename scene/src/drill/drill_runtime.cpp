#include <hongtu/scene/drill/drill_runtime.hpp>

#include <cmath>
#include <cstring>

namespace hongtu::scene::drill {

namespace {

// ---- 解析助手:全部"先验证后取值",apply 失败必须零副作用 -------------------

bool parseVec3(const nlohmann::json& j, glm::dvec3& out) {
  if (!j.is_array() || j.size() != 3) return false;
  for (size_t i = 0; i < 3; ++i) {
    if (!j[i].is_number()) return false;
  }
  out = {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
  return true;
}

bool parseRoutePoints(const nlohmann::json& j, std::vector<glm::dvec3>& out) {
  if (!j.is_array() || j.size() < 2) return false;
  std::vector<glm::dvec3> pts;
  pts.reserve(j.size());
  for (const auto& p : j) {
    glm::dvec3 v;
    if (!parseVec3(p, v)) return false;
    pts.push_back(v);
  }
  out = std::move(pts);
  return true;
}

/// id 字段:接受任意非负整数(nlohmann 把 C++ int 字面量存成有符号,
/// is_number_unsigned 只认显式 uint64,直接用会把 id:1 拒掉)。
bool parseId(const nlohmann::json& cmd, const char* key, uint64_t& out) {
  if (!cmd.contains(key)) return false;
  const auto& j = cmd[key];
  if (j.is_number_unsigned()) { out = j.get<uint64_t>(); return true; }
  if (j.is_number_integer()) {
    const int64_t v = j.get<int64_t>();
    if (v < 0) return false;
    out = static_cast<uint64_t>(v);
    return true;
  }
  return false;
}

ApplyResult fail(std::string msg) { return {false, false, std::move(msg)}; }
ApplyResult okResult() { return {true, false, {}}; }

// ---- FNV-1a 64 ----------------------------------------------------------

struct Fnv {
  uint64_t h = 1469598103934665603ull;
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
    }
  }
  void u64(uint64_t v) { bytes(&v, sizeof v); }
  void f64(double v)   { bytes(&v, sizeof v); }  // 同平台 bit 级一致即可
  void str(const std::string& s) { u64(s.size()); bytes(s.data(), s.size()); }
};

}  // namespace

ApplyResult DrillRuntime::apply(const nlohmann::json& cmd, uint64_t seq) {
  if (seq <= last_seq_) {
    return fail("stale seq " + std::to_string(seq) + " (last " + std::to_string(last_seq_) + ")");
  }
  // 合法的新 seq 一律消费——成败在所有实例上同判,lastSeq 推进保持一致。
  last_seq_ = seq;

  if (!cmd.is_object() || !cmd.contains("op") || !cmd["op"].is_string()) {
    return fail("command missing op");
  }
  const std::string op = cmd["op"].get<std::string>();

  if (op == "drill.start") {
    if (phase_ != Phase::Idle) return fail("drill.start in phase != Idle");
    phase_ = Phase::Running;
    return okResult();
  }
  if (op == "drill.pause") {
    if (phase_ != Phase::Running) return fail("drill.pause in phase != Running");
    phase_ = Phase::Paused;
    return okResult();
  }
  if (op == "drill.resume") {
    if (phase_ != Phase::Paused) return fail("drill.resume in phase != Paused");
    phase_ = Phase::Running;
    return okResult();
  }
  if (op == "drill.end") {
    if (phase_ == Phase::Ended) return fail("drill already ended");
    phase_ = Phase::Ended;
    return okResult();
  }

  if (op == "element.place")   return cmdPlace(cmd, seq, /*force_disaster=*/false);
  if (op == "disaster.trigger") return cmdPlace(cmd, seq, /*force_disaster=*/true);
  if (op == "element.update")  return cmdUpdate(cmd, /*require_disaster=*/false);
  if (op == "disaster.update") return cmdUpdate(cmd, /*require_disaster=*/true);
  if (op == "element.remove")  return cmdRemove(cmd);
  if (op == "element.move")    return cmdMove(cmd);
  if (op == "alarm.set")       return cmdAlarmSet(cmd);
  if (op == "scene.env")       return cmdEnv(cmd);

  // 业务事件(task.* / report.* 等):引擎不消费,但不算错误。
  if (op.rfind("task.", 0) == 0 || op.rfind("report.", 0) == 0) {
    return {true, true, {}};
  }
  return fail("unknown op: " + op);
}

ApplyResult DrillRuntime::cmdPlace(const nlohmann::json& cmd, uint64_t seq, bool force_disaster) {
  if (!cmd.contains("element") || !cmd["element"].is_object()) return fail("place: missing element");
  const auto& e = cmd["element"];

  DrillElement el;
  el.id = seq;  // id 缺省取 seq:全局唯一,各实例一致
  if (e.contains("id")) {
    if (!parseId(e, "id", el.id)) return fail("place: bad id");
  }
  if (elements_.count(el.id)) return fail("place: duplicate id " + std::to_string(el.id));

  if (force_disaster) {
    el.kind = ElementKind::Disaster;
  } else {
    if (!e.contains("kind") || !e["kind"].is_string() ||
        !kindFromString(e["kind"].get<std::string>(), el.kind)) {
      return fail("place: bad kind");
    }
  }
  if (e.contains("pos") && !parseVec3(e["pos"], el.pos)) return fail("place: bad pos");
  el.name    = e.value("name", std::string{});
  el.yaw_rad = glm::radians(e.value("yaw_deg", 0.0));
  if (e.contains("props")) {
    if (!e["props"].is_object()) return fail("place: props must be object");
    el.props = e["props"];
  }
  // 路线/区域要素的几何放 props.points,这里仅校验格式(若给了)。
  if ((el.kind == ElementKind::Route || el.kind == ElementKind::Region) &&
      el.props.contains("points")) {
    std::vector<glm::dvec3> tmp;
    if (!parseRoutePoints(el.props["points"], tmp)) return fail("place: bad props.points");
  }
  elements_.emplace(el.id, std::move(el));
  return okResult();
}

ApplyResult DrillRuntime::cmdUpdate(const nlohmann::json& cmd, bool require_disaster) {
  uint64_t id = 0;
  if (!parseId(cmd, "id", id)) return fail("update: missing id");
  auto it = elements_.find(id);
  if (it == elements_.end()) return fail("update: no element " + std::to_string(id));
  if (require_disaster && it->second.kind != ElementKind::Disaster) {
    return fail("disaster.update on non-disaster element");
  }
  if (!cmd.contains("patch") || !cmd["patch"].is_object()) return fail("update: missing patch");
  const auto& p = cmd["patch"];

  // 先全部解析,再一次性落账。
  glm::dvec3 new_pos = it->second.pos;
  bool has_pos = p.contains("pos");
  if (has_pos && !parseVec3(p["pos"], new_pos)) return fail("update: bad pos");
  if (p.contains("props") && !p["props"].is_object()) return fail("update: props must be object");

  DrillElement& el = it->second;
  if (has_pos) el.pos = new_pos;
  if (p.contains("yaw_deg") && p["yaw_deg"].is_number()) {
    el.yaw_rad = glm::radians(p["yaw_deg"].get<double>());
  }
  if (p.contains("name") && p["name"].is_string()) el.name = p["name"].get<std::string>();
  if (p.contains("props")) el.props.merge_patch(p["props"]);
  return okResult();
}

ApplyResult DrillRuntime::cmdRemove(const nlohmann::json& cmd) {
  uint64_t id = 0;
  if (!parseId(cmd, "id", id)) return fail("remove: missing id");
  if (elements_.erase(id) == 0) return fail("remove: no element " + std::to_string(id));
  return okResult();
}

ApplyResult DrillRuntime::cmdMove(const nlohmann::json& cmd) {
  uint64_t id = 0;
  if (!parseId(cmd, "id", id)) return fail("move: missing id");
  auto it = elements_.find(id);
  if (it == elements_.end()) return fail("move: no element " + std::to_string(id));
  DrillElement& el = it->second;
  if (el.kind != ElementKind::Person && el.kind != ElementKind::Vehicle) {
    return fail("move: element kind cannot move");
  }
  const double speed = cmd.value("speed_mps", 0.0);
  if (!(speed > 0.0)) return fail("move: speed_mps must be > 0");

  std::vector<glm::dvec3> route;
  if (cmd.contains("route")) {
    if (!parseRoutePoints(cmd["route"], route)) return fail("move: bad route");
  } else if (cmd.contains("route_id")) {
    uint64_t route_id = 0;
    if (!parseId(cmd, "route_id", route_id)) return fail("move: bad route_id");
    auto rit = elements_.find(route_id);
    if (rit == elements_.end() || rit->second.kind != ElementKind::Route) {
      return fail("move: route_id is not a route element");
    }
    if (!rit->second.props.contains("points") ||
        !parseRoutePoints(rit->second.props["points"], route)) {
      return fail("move: route element has no valid points");
    }
  } else {
    return fail("move: need route or route_id");
  }

  el.moving           = true;
  el.route            = std::move(route);
  el.route_progress_m = 0.0;
  el.speed_mps        = speed;
  el.pos              = el.route.front();
  return okResult();
}

ApplyResult DrillRuntime::cmdAlarmSet(const nlohmann::json& cmd) {
  uint64_t id = 0;
  if (!parseId(cmd, "id", id)) return fail("alarm.set: missing id");
  auto it = elements_.find(id);
  if (it == elements_.end()) return fail("alarm.set: no element " + std::to_string(id));
  if (it->second.kind != ElementKind::Alarm) return fail("alarm.set on non-alarm element");
  if (!cmd.contains("on") || !cmd["on"].is_boolean()) return fail("alarm.set: missing on");
  it->second.props["on"] = cmd["on"].get<bool>();
  return okResult();
}

ApplyResult DrillRuntime::cmdEnv(const nlohmann::json& cmd) {
  if (!cmd.contains("patch") || !cmd["patch"].is_object()) return fail("scene.env: missing patch");
  env_.merge_patch(cmd["patch"]);
  return okResult();
}

void DrillRuntime::tick() {
  ++tick_count_;
  if (phase_ != Phase::Running) return;

  for (auto& [id, el] : elements_) {
    if (!el.moving || el.route.size() < 2) continue;
    el.route_progress_m += el.speed_mps * kTickDt;

    // 沿折线走到 progress 对应点;越过终点则停在终点。
    double remaining = el.route_progress_m;
    bool   arrived   = true;
    for (size_t i = 0; i + 1 < el.route.size(); ++i) {
      const glm::dvec3 a = el.route[i], b = el.route[i + 1];
      const glm::dvec3 d = b - a;
      const double     len = glm::length(d);
      if (len <= 1e-12) continue;
      if (remaining <= len) {
        el.pos     = a + d * (remaining / len);
        el.yaw_rad = std::atan2(d.y, d.x);
        arrived    = false;
        break;
      }
      remaining -= len;
    }
    if (arrived) {
      const glm::dvec3 d = el.route.back() - el.route[el.route.size() - 2];
      el.pos    = el.route.back();
      if (glm::length(d) > 1e-12) el.yaw_rad = std::atan2(d.y, d.x);
      el.moving = false;
      el.route.clear();
      el.route_progress_m = 0.0;
      el.speed_mps        = 0.0;
    }
  }
}

const DrillElement* DrillRuntime::find(uint64_t id) const {
  auto it = elements_.find(id);
  return it == elements_.end() ? nullptr : &it->second;
}

namespace {
void hashElements(Fnv& f, const std::map<uint64_t, DrillElement>& elements,
                  const nlohmann::json& env, DrillRuntime::Phase phase, uint64_t last_seq) {
  f.u64(static_cast<uint64_t>(phase));
  f.u64(last_seq);
  f.str(env.dump());  // nlohmann object 按 key 排序,dump 确定
  f.u64(elements.size());
  for (const auto& [id, el] : elements) {  // std::map ⇒ id 升序
    f.u64(id);
    f.u64(static_cast<uint64_t>(el.kind));
    f.str(el.name);
    f.f64(el.pos.x); f.f64(el.pos.y); f.f64(el.pos.z);
    f.f64(el.yaw_rad);
    f.str(el.props.dump());
    f.u64(el.moving ? 1 : 0);
    f.f64(el.route_progress_m);
    f.f64(el.speed_mps);
    f.u64(el.route.size());
    for (const auto& p : el.route) { f.f64(p.x); f.f64(p.y); f.f64(p.z); }
  }
}
}  // namespace

uint64_t DrillRuntime::stateHash() const {
  Fnv f;
  f.u64(tick_count_);
  hashElements(f, elements_, env_, phase_, last_seq_);
  return f.h;
}

uint64_t DrillRuntime::elementsHash() const {
  Fnv f;
  hashElements(f, elements_, env_, phase_, last_seq_);
  return f.h;
}

nlohmann::json DrillRuntime::toJson() const {
  nlohmann::json j;
  switch (phase_) {
    case Phase::Idle:    j["phase"] = "idle";    break;
    case Phase::Running: j["phase"] = "running"; break;
    case Phase::Paused:  j["phase"] = "paused";  break;
    case Phase::Ended:   j["phase"] = "ended";   break;
  }
  j["last_seq"]   = last_seq_;
  j["tick_count"] = tick_count_;
  j["env"]        = env_;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& [id, el] : elements_) arr.push_back(el.toJson());
  j["elements"] = std::move(arr);
  return j;
}

}  // namespace hongtu::scene::drill
