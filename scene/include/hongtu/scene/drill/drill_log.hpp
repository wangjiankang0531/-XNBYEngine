// DrillLog — 演练事件日志:同步协议的"录像格式"。
//
// 每条 = {seq, t_ms, actor, cmd},JSON Lines 落盘(一行一事件,追加友好)。
// 回放:ReplayCursor 按 t_ms 把命令泵给 DrillRuntime;seek 用"从头快进"。
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hongtu::scene::drill {

struct DrillLogEntry {
  uint64_t       seq  = 0;
  uint64_t       t_ms = 0;  ///< 演练时刻(会话服务定序时盖章;standalone 取 runtime 时钟)
  std::string    actor;
  nlohmann::json cmd;

  nlohmann::json toJson() const;
  static bool fromJson(const nlohmann::json& j, DrillLogEntry& out);
};

class DrillLog {
 public:
  /// 追加;seq 必须严格递增,否则拒绝返回 false。
  bool record(DrillLogEntry entry);

  const std::vector<DrillLogEntry>& entries() const { return entries_; }
  uint64_t lastSeq() const { return entries_.empty() ? 0 : entries_.back().seq; }
  uint64_t durationMs() const { return entries_.empty() ? 0 : entries_.back().t_ms; }

  std::string toJsonLines() const;
  /// 解析 JSON Lines(坏行整体拒绝,不部分加载);替换现有内容。
  bool fromJsonLines(const std::string& text);

  bool saveFile(const std::string& path) const;
  bool loadFile(const std::string& path);

 private:
  std::vector<DrillLogEntry> entries_;
};

/// 回放游标:按时间推进,把到点的事件按序交给回调。
class ReplayCursor {
 public:
  explicit ReplayCursor(const DrillLog& log) : log_(log) {}

  /// 把所有 t_ms <= t 且尚未消费的事件按序送入 fn;返回本次消费条数。
  size_t advanceTo(uint64_t t_ms, const std::function<void(const DrillLogEntry&)>& fn);

  void reset() { next_ = 0; }
  bool done() const { return next_ >= log_.entries().size(); }
  size_t nextIndex() const { return next_; }

 private:
  const DrillLog& log_;
  size_t next_ = 0;
};

}  // namespace hongtu::scene::drill
