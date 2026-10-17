// DrillLog / ReplayCursor 测试 — JSONL 往返、seq 纪律、回放泵 + 日志重放等价性。

#include <gtest/gtest.h>
#include <hongtu/scene/drill/drill_log.hpp>
#include <hongtu/scene/drill/drill_runtime.hpp>

using hongtu::scene::drill::DrillLog;
using hongtu::scene::drill::DrillLogEntry;
using hongtu::scene::drill::DrillRuntime;
using hongtu::scene::drill::ReplayCursor;
using nlohmann::json;

TEST(DrillLogTest, RecordEnforcesSeqMonotonic) {
  DrillLog log;
  EXPECT_TRUE(log.record({1, 0, "d", {{"op", "drill.start"}}}));
  EXPECT_TRUE(log.record({2, 100, "d", {{"op", "drill.end"}}}));
  EXPECT_FALSE(log.record({2, 200, "d", {{"op", "drill.end"}}}));
  EXPECT_EQ(log.entries().size(), 2u);
  EXPECT_EQ(log.durationMs(), 100u);
}

TEST(DrillLogTest, JsonLinesRoundTrip) {
  DrillLog log;
  log.record({1, 0, "director", {{"op", "drill.start"}}});
  log.record({2, 1500, "director",
              {{"op", "element.place"},
               {"element", {{"kind", "person"}, {"pos", {1.0, 2.0, 0.0}}}}}});
  const std::string text = log.toJsonLines();

  DrillLog back;
  ASSERT_TRUE(back.fromJsonLines(text));
  ASSERT_EQ(back.entries().size(), 2u);
  EXPECT_EQ(back.entries()[1].t_ms, 1500u);
  EXPECT_EQ(back.entries()[1].actor, "director");
  EXPECT_EQ(back.entries()[1].cmd["op"], "element.place");
}

TEST(DrillLogTest, BadLinesRejectedAtomically) {
  DrillLog log;
  log.record({1, 0, "d", {{"op", "drill.start"}}});
  ASSERT_TRUE(log.fromJsonLines(log.toJsonLines()));
  // 坏行 ⇒ 整体拒绝,原内容不被破坏
  DrillLog target;
  target.record({7, 0, "x", {{"op", "drill.start"}}});
  EXPECT_FALSE(target.fromJsonLines("{\"seq\":1,\"cmd\":{\"op\":\"a\"}}\nnot-json\n"));
  EXPECT_EQ(target.entries().size(), 1u);
  EXPECT_EQ(target.entries()[0].seq, 7u);
}

TEST(DrillLogTest, ReplayCursorPumpsByTime) {
  DrillLog log;
  log.record({1, 0,    "d", {{"op", "drill.start"}}});
  log.record({2, 1000, "d", {{"op", "element.place"}, {"element", {{"kind", "marker"}}}}});
  log.record({3, 5000, "d", {{"op", "drill.end"}}});

  ReplayCursor cur(log);
  std::vector<uint64_t> seen;
  auto fn = [&](const DrillLogEntry& e) { seen.push_back(e.seq); };

  EXPECT_EQ(cur.advanceTo(999, fn), 1u);   // 只有 t=0 的 start
  EXPECT_EQ(cur.advanceTo(1000, fn), 1u);  // 边界含等于
  EXPECT_EQ(cur.advanceTo(1000, fn), 0u);  // 不重复消费
  EXPECT_FALSE(cur.done());
  EXPECT_EQ(cur.advanceTo(99999, fn), 1u);
  EXPECT_TRUE(cur.done());
  EXPECT_EQ(seen, (std::vector<uint64_t>{1, 2, 3}));

  cur.reset();
  EXPECT_EQ(cur.advanceTo(99999, fn), 3u);
}

// 闭环:live 实例边跑边记日志,回放实例按日志驱动 ⇒ 终态哈希一致。
// 这是"回放=重放事件日志"架构的根本保证。
TEST(DrillLogTest, LiveAndReplayConverge) {
  DrillLog log;
  DrillRuntime live;
  uint64_t seq = 0;

  // 模拟会话:命令在指定 tick 时刻进入,t_ms = tick * dt
  auto issue = [&](uint64_t at_tick, json cmd) {
    while (live.tickCount() < at_tick) live.tick();
    ++seq;
    const auto t_ms = static_cast<uint64_t>(at_tick * DrillRuntime::kTickDt * 1000.0);
    live.apply(cmd, seq);
    log.record({seq, t_ms, "director", std::move(cmd)});
  };

  issue(0,  {{"op", "drill.start"}});
  issue(0,  {{"op", "element.place"},
             {"element", {{"kind", "vehicle"}, {"pos", {0.0, 0.0, 0.0}}}}});
  issue(30, {{"op", "element.move"}, {"id", 2}, {"speed_mps", 4.0},
             {"route", {{0.0, 0.0, 0.0}, {40.0, 0.0, 0.0}}}});
  issue(90, {{"op", "disaster.trigger"},
             {"element", {{"pos", {20.0, 0.0, 0.0}}, {"props", {{"type", "pool_fire"}}}}}});
  while (live.tickCount() < 300) live.tick();
  issue(300, {{"op", "drill.end"}});

  // 回放:cursor 按每 tick 的时钟泵命令
  DrillRuntime replay;
  ReplayCursor cur(log);
  auto pump = [&](const DrillLogEntry& e) { replay.apply(e.cmd, e.seq); };
  while (replay.tickCount() <= 300) {
    const auto t_ms = static_cast<uint64_t>(replay.tickCount() * DrillRuntime::kTickDt * 1000.0);
    cur.advanceTo(t_ms, pump);
    if (replay.tickCount() == 300) break;
    replay.tick();
  }
  EXPECT_TRUE(cur.done());
  EXPECT_EQ(live.stateHash(), replay.stateHash());
}
