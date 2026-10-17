#include <hongtu/scene/drill/drill_log.hpp>

#include <fstream>
#include <sstream>

namespace hongtu::scene::drill {

nlohmann::json DrillLogEntry::toJson() const {
  return {{"seq", seq}, {"t_ms", t_ms}, {"actor", actor}, {"cmd", cmd}};
}

bool DrillLogEntry::fromJson(const nlohmann::json& j, DrillLogEntry& out) {
  if (!j.is_object()) return false;
  if (!j.contains("seq") || !j["seq"].is_number_unsigned()) return false;
  if (!j.contains("cmd") || !j["cmd"].is_object()) return false;
  out.seq   = j["seq"].get<uint64_t>();
  out.t_ms  = j.value("t_ms", 0ull);
  out.actor = j.value("actor", std::string{});
  out.cmd   = j["cmd"];
  return true;
}

bool DrillLog::record(DrillLogEntry entry) {
  if (!entries_.empty() && entry.seq <= entries_.back().seq) return false;
  entries_.push_back(std::move(entry));
  return true;
}

std::string DrillLog::toJsonLines() const {
  std::ostringstream os;
  for (const auto& e : entries_) os << e.toJson().dump() << '\n';
  return os.str();
}

bool DrillLog::fromJsonLines(const std::string& text) {
  std::vector<DrillLogEntry> parsed;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (line.empty()) continue;
    nlohmann::json j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
    DrillLogEntry e;
    if (j.is_discarded() || !DrillLogEntry::fromJson(j, e)) return false;
    if (!parsed.empty() && e.seq <= parsed.back().seq) return false;
    parsed.push_back(std::move(e));
  }
  entries_ = std::move(parsed);
  return true;
}

bool DrillLog::saveFile(const std::string& path) const {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << toJsonLines();
  return static_cast<bool>(f);
}

bool DrillLog::loadFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream os;
  os << f.rdbuf();
  return fromJsonLines(os.str());
}

size_t ReplayCursor::advanceTo(uint64_t t_ms,
                               const std::function<void(const DrillLogEntry&)>& fn) {
  size_t n = 0;
  const auto& es = log_.entries();
  while (next_ < es.size() && es[next_].t_ms <= t_ms) {
    fn(es[next_]);
    ++next_;
    ++n;
  }
  return n;
}

}  // namespace hongtu::scene::drill
