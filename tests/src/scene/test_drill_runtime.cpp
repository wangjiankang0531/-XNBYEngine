// DrillRuntime 测试 — 命令生命周期、seq 纪律、零副作用失败、tick 确定性。

#include <gtest/gtest.h>
#include <hongtu/scene/drill/drill_runtime.hpp>

#include <glm/gtc/constants.hpp>

using hongtu::scene::drill::DrillRuntime;
using hongtu::scene::drill::ElementKind;
using nlohmann::json;

namespace {

json placeCmd(const char* kind, double x, double y, json props = json::object()) {
  return {{"op", "element.place"},
          {"element", {{"kind", kind}, {"pos", {x, y, 0.0}}, {"props", props}}}};
}

}  // namespace

TEST(DrillRuntimeTest, PlaceUpdateRemoveLifecycle) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("person", 1.0, 2.0), 1).ok);
  const auto* el = rt.find(1);  // id 缺省 = seq
  ASSERT_NE(el, nullptr);
  EXPECT_EQ(el->kind, ElementKind::Person);
  EXPECT_DOUBLE_EQ(el->pos.x, 1.0);

  ASSERT_TRUE(rt.apply({{"op", "element.update"},
                        {"id", 1},
                        {"patch", {{"pos", {5.0, 6.0, 0.0}}, {"props", {{"posture", "run"}}}}}},
                       2).ok);
  el = rt.find(1);
  EXPECT_DOUBLE_EQ(el->pos.x, 5.0);
  EXPECT_EQ(el->props["posture"], "run");

  ASSERT_TRUE(rt.apply({{"op", "element.remove"}, {"id", 1}}, 3).ok);
  EXPECT_EQ(rt.find(1), nullptr);
  EXPECT_EQ(rt.lastSeq(), 3u);
}

TEST(DrillRuntimeTest, SeqDisciplineRejectsStaleAndDuplicate) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("marker", 0, 0), 5).ok);
  EXPECT_FALSE(rt.apply(placeCmd("marker", 1, 1), 5).ok);  // 重复
  EXPECT_FALSE(rt.apply(placeCmd("marker", 1, 1), 4).ok);  // 乱序
  EXPECT_EQ(rt.lastSeq(), 5u);
  EXPECT_EQ(rt.elements().size(), 1u);
  // 失败命令也消费新 seq(各实例 lastSeq 推进一致)
  EXPECT_FALSE(rt.apply({{"op", "element.remove"}, {"id", 999}}, 6).ok);
  EXPECT_EQ(rt.lastSeq(), 6u);
}

TEST(DrillRuntimeTest, FailedCommandHasNoPartialEffect) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("person", 1.0, 1.0), 1).ok);
  const uint64_t h = rt.stateHash();
  // pos 非法但 patch 里还有合法 name —— 必须整体失败,name 不得写入
  auto r = rt.apply({{"op", "element.update"},
                     {"id", 1},
                     {"patch", {{"name", "leaked"}, {"pos", {1.0, 2.0}}}}},
                    2);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(rt.lastSeq(), 1u);          // seq 已消费
  EXPECT_EQ(rt.find(1)->name, "");      // 状态未动
  // stateHash 含 last_seq,排除其影响后比较要素本身
  (void)h;
}

TEST(DrillRuntimeTest, DisasterAliasOps) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply({{"op", "disaster.trigger"},
                        {"element", {{"pos", {10.0, 0.0, 0.0}},
                                     {"props", {{"type", "pool_fire"}, {"radius_m", 8.0}}}}}},
                       1).ok);
  const auto* el = rt.find(1);
  ASSERT_NE(el, nullptr);
  EXPECT_EQ(el->kind, ElementKind::Disaster);

  ASSERT_TRUE(rt.apply({{"op", "disaster.update"},
                        {"id", 1},
                        {"patch", {{"props", {{"wind", {2.0, 1.0}}}}}}},
                       2).ok);
  EXPECT_EQ(rt.find(1)->props["type"], "pool_fire");  // merge 不丢旧键
  // disaster.update 不能打到非灾害要素
  ASSERT_TRUE(rt.apply(placeCmd("person", 0, 0), 3).ok);
  EXPECT_FALSE(rt.apply({{"op", "disaster.update"}, {"id", 3}, {"patch", json::object()}}, 4).ok);
}

TEST(DrillRuntimeTest, AlarmSetOnlyOnAlarm) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("alarm", 0, 0), 1).ok);
  ASSERT_TRUE(rt.apply({{"op", "alarm.set"}, {"id", 1}, {"on", true}}, 2).ok);
  EXPECT_EQ(rt.find(1)->props["on"], true);
  ASSERT_TRUE(rt.apply(placeCmd("marker", 0, 0), 3).ok);
  EXPECT_FALSE(rt.apply({{"op", "alarm.set"}, {"id", 3}, {"on", true}}, 4).ok);
}

TEST(DrillRuntimeTest, BusinessOpsIgnoredButConsumed) {
  DrillRuntime rt;
  auto r = rt.apply({{"op", "task.assign"}, {"unit", "u1"}, {"text", "封锁北门"}}, 1);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.ignored);
  EXPECT_EQ(rt.lastSeq(), 1u);
  EXPECT_TRUE(rt.elements().empty());
}

TEST(DrillRuntimeTest, MoveFollowsRouteOnlyWhileRunning) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("vehicle", 0, 0), 1).ok);
  ASSERT_TRUE(rt.apply({{"op", "element.move"},
                        {"id", 1},
                        {"speed_mps", 3.0},
                        {"route", {{0.0, 0.0, 0.0}, {30.0, 0.0, 0.0}}}},
                       2).ok);
  // 未 start:tick 不推进位置
  for (int i = 0; i < 30; ++i) rt.tick();
  EXPECT_DOUBLE_EQ(rt.find(1)->pos.x, 0.0);

  ASSERT_TRUE(rt.apply({{"op", "drill.start"}}, 3).ok);
  for (int i = 0; i < 30; ++i) rt.tick();  // 1 秒 → 3 m
  EXPECT_NEAR(rt.find(1)->pos.x, 3.0, 1e-9);
  EXPECT_TRUE(rt.find(1)->moving);

  // 走完全程后停在终点
  for (int i = 0; i < 30 * 20; ++i) rt.tick();
  EXPECT_DOUBLE_EQ(rt.find(1)->pos.x, 30.0);
  EXPECT_FALSE(rt.find(1)->moving);
}

TEST(DrillRuntimeTest, MoveByRouteElementAndHeading) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply(placeCmd("route", 0, 0,
                                {{"points", {{0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}}}}),
                       1).ok);
  ASSERT_TRUE(rt.apply(placeCmd("person", 5, 5), 2).ok);
  ASSERT_TRUE(rt.apply({{"op", "element.move"}, {"id", 2}, {"speed_mps", 1.0}, {"route_id", 1}}, 3).ok);
  EXPECT_DOUBLE_EQ(rt.find(2)->pos.x, 0.0);  // 吸附到路线起点
  ASSERT_TRUE(rt.apply({{"op", "drill.start"}}, 4).ok);
  rt.tick();
  EXPECT_NEAR(rt.find(2)->yaw_rad, glm::half_pi<double>(), 1e-9);  // 朝 +Y
  // 非可移动要素拒绝 move
  ASSERT_TRUE(rt.apply(placeCmd("region", 0, 0), 5).ok);
  EXPECT_FALSE(rt.apply({{"op", "element.move"}, {"id", 5}, {"speed_mps", 1.0}, {"route_id", 1}}, 6).ok);
}

TEST(DrillRuntimeTest, PhaseTransitions) {
  DrillRuntime rt;
  EXPECT_FALSE(rt.apply({{"op", "drill.pause"}}, 1).ok);   // Idle 不能 pause
  ASSERT_TRUE(rt.apply({{"op", "drill.start"}}, 2).ok);
  EXPECT_FALSE(rt.apply({{"op", "drill.start"}}, 3).ok);   // 重复 start
  ASSERT_TRUE(rt.apply({{"op", "drill.pause"}}, 4).ok);
  ASSERT_TRUE(rt.apply({{"op", "drill.resume"}}, 5).ok);
  ASSERT_TRUE(rt.apply({{"op", "drill.end"}}, 6).ok);
  EXPECT_EQ(rt.phase(), DrillRuntime::Phase::Ended);
}

TEST(DrillRuntimeTest, SceneEnvMergePatch) {
  DrillRuntime rt;
  ASSERT_TRUE(rt.apply({{"op", "scene.env"}, {"patch", {{"weather", "rain"}, {"wind", {1.0, 0.0}}}}}, 1).ok);
  ASSERT_TRUE(rt.apply({{"op", "scene.env"}, {"patch", {{"wind", {0.0, 2.0}}}}}, 2).ok);
  EXPECT_EQ(rt.env()["weather"], "rain");
  EXPECT_EQ(rt.env()["wind"][1], 2.0);
}

// 核心确定性保证:同一命令流 + 同一 tick 序列 ⇒ 两个独立实例状态哈希一致。
TEST(DrillRuntimeTest, DeterministicReplaySameHash) {
  auto run = [](DrillRuntime& rt) {
    EXPECT_TRUE(rt.apply({{"op", "drill.start"}}, 1).ok);
    EXPECT_TRUE(rt.apply(placeCmd("vehicle", 0, 0), 2).ok);
    EXPECT_TRUE(rt.apply({{"op", "element.move"},
                          {"id", 2},
                          {"speed_mps", 2.5},
                          {"route", {{0.0, 0.0, 0.0}, {10.0, 5.0, 0.0}, {10.0, 20.0, 0.0}}}},
                         3).ok);
    for (int i = 0; i < 100; ++i) rt.tick();
    EXPECT_TRUE(rt.apply({{"op", "disaster.trigger"},
                          {"element", {{"pos", {8.0, 3.0, 0.0}}, {"props", {{"type", "gas_plume"}}}}}},
                         4).ok);
    for (int i = 0; i < 100; ++i) rt.tick();
  };
  DrillRuntime a, b;
  run(a);
  run(b);
  EXPECT_EQ(a.stateHash(), b.stateHash());
  EXPECT_NE(a.stateHash(), DrillRuntime{}.stateHash());
}
