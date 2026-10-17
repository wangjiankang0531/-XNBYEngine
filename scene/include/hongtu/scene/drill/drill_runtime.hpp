// DrillRuntime — 把一条有序 JSON 命令流确定性地变成演练场景状态。
//
// 同步模型(事件溯源):会话服务给每条命令赋全局递增 seq,所有引擎实例按
// seq 顺序 apply 同一命令流 + 相同的固定步长 tick 序列 ⇒ 状态一致。
// 回放 = 重放日志。确定性约束:
//   - apply() 校验全部通过后才改状态,失败零副作用;
//   - 校验逻辑本身确定(同一命令在所有实例上同判),失败也消费 seq;
//   - 连续行为(路线跟随)只由 tick 计数驱动,不读墙钟;
//   - stateHash() 供测试/对账:同命令流+同 tick 数 ⇒ 同 hash(同平台)。
//
// 命令集 v1(cmd.op):
//   drill.start | drill.pause | drill.resume | drill.end
//   element.place   {element:{id?,kind,name?,pos:[x,y,z],yaw_deg?,props?}}
//                   id 缺省取 seq(seq 全局唯一,天然防撞)
//   element.update  {id, patch:{name?,pos?,yaw_deg?,props?}}  props 为 RFC7386 merge
//   element.remove  {id}
//   element.move    {id, speed_mps, route:[[x,y,z],...] | route_id}
//   disaster.trigger{element:{...}}        = element.place 强制 kind=disaster
//   disaster.update {id, patch}            = element.update 且校验 kind
//   alarm.set       {id, on}
//   scene.env       {patch:{weather?,time_of_day_h?,wind?}}   RFC7386 merge
// 任务/汇报类业务事件(task.* / report.*)不属引擎,apply 返回 ignored。
#pragma once

#include <hongtu/scene/drill/drill_element.hpp>

#include <map>
#include <string>

namespace hongtu::scene::drill {

struct ApplyResult {
  bool ok      = false;
  bool ignored = false;  ///< 非场景类命令(task.*等),已消费 seq 但无状态变化
  std::string error;
};

class DrillRuntime {
 public:
  static constexpr double kTickDt = 1.0 / 30.0;

  enum class Phase { Idle, Running, Paused, Ended };

  /// 按 seq 严格递增应用命令;seq <= lastSeq() 直接拒(重复/乱序)。
  /// 无论命令成败,合法的新 seq 都会被消费(保证各实例 lastSeq 推进一致)。
  ApplyResult apply(const nlohmann::json& cmd, uint64_t seq);

  /// 固定步长推进连续行为;仅 Running 相位生效(计数器照常返回)。
  void tick();

  // ---- 查询 ----
  const DrillElement* find(uint64_t id) const;
  const std::map<uint64_t, DrillElement>& elements() const { return elements_; }
  Phase    phase() const     { return phase_; }
  uint64_t lastSeq() const   { return last_seq_; }
  uint64_t tickCount() const { return tick_count_; }
  double   clockSec() const  { return static_cast<double>(tick_count_) * kTickDt; }
  const nlohmann::json& env() const { return env_; }

  /// FNV-1a 64 状态摘要(要素按 id 序 + env + phase + tick 数)。
  uint64_t stateHash() const;

  /// 跨实例对账摘要:只含要素+env+phase+last_seq,不含 tick 数 ——
  /// 各实例启动时刻不同,tick 计数必然不同;静止状态下该摘要应一致
  /// (移动中的要素 progress 会瞬态不同,属预期)。
  uint64_t elementsHash() const;

  nlohmann::json toJson() const;

 private:
  ApplyResult cmdPlace(const nlohmann::json& cmd, uint64_t seq, bool force_disaster);
  ApplyResult cmdUpdate(const nlohmann::json& cmd, bool require_disaster);
  ApplyResult cmdRemove(const nlohmann::json& cmd);
  ApplyResult cmdMove(const nlohmann::json& cmd);
  ApplyResult cmdAlarmSet(const nlohmann::json& cmd);
  ApplyResult cmdEnv(const nlohmann::json& cmd);

  std::map<uint64_t, DrillElement> elements_;
  nlohmann::json env_ = nlohmann::json::object();
  Phase    phase_      = Phase::Idle;
  uint64_t last_seq_   = 0;
  uint64_t tick_count_ = 0;
};

}  // namespace hongtu::scene::drill
