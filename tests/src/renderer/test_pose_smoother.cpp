// lane ⑫ T1:动态实体显示位姿平滑(renderer/motion/pose_smoother.hpp)。
#include <gtest/gtest.h>
#include <hongtu/renderer/motion/pose_smoother.hpp>

#include <cmath>
#include <vector>

using namespace hongtu::renderer::motion;

namespace {

  constexpr double kDt = 1.0 / 30.0;

  MotionPose at(double x, double y = 0.0, double z = 0.0, double yaw = 0.0, bool ground = false) {
    MotionPose p;
    p.pos = {x, y, z};
    p.yaw_deg = yaw;
    p.ground_clamped = ground;
    return p;
  }

  SmoothSpec fixedSpec(double s, bool face = false) {
    SmoothSpec sp;
    sp.mode = SmoothMode::kFixed;
    sp.duration_s = s;
    sp.face_motion = face;
    return sp;
  }

  SmoothSpec autoSpec(bool face = false) {
    SmoothSpec sp;
    sp.mode = SmoothMode::kAuto;
    sp.face_motion = face;
    return sp;
  }

  SmoothSpec noneSpec() {
    return SmoothSpec{};
  }

  // 逐帧推进一个「时钟」:每帧 now += dt,再 step。
  struct Clock {
    double now = 0.0;
    void run(PoseSmoother& s, int frames, std::vector<double>* xs = nullptr) {
      for (int i = 0; i < frames; ++i) {
        now += kDt;
        s.step(kDt);
        if (xs)
          xs->push_back(s.displayed().pos.x);
      }
    }
  };

} // namespace

TEST(PoseSmoother, FixedDurationIsLinearAndEndsExactly) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(10), false, fixedSpec(1.0), 0.0);
  for (int i = 0; i < 15; ++i)
    s.step(kDt);
  EXPECT_NEAR(s.displayed().pos.x, 5.0, 1e-9);
  EXPECT_TRUE(s.active());
  for (int i = 0; i < 15; ++i)
    s.step(kDt);
  EXPECT_NEAR(s.displayed().pos.x, 10.0, 1e-9);
  EXPECT_FALSE(s.active());
  EXPECT_NEAR(s.speed(), 10.0, 1e-6); // 最后一步仍在走
  s.step(kDt);
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, 10.0);
  EXPECT_NEAR(s.speed(), 0.0, 1e-12); // 停下
}

TEST(PoseSmoother, FixedRetargetStartsFromDisplayedPose) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(10), false, fixedSpec(1.0), 0.0);
  for (int i = 0; i < 15; ++i)
    s.step(kDt); // x = 5
  s.push(at(5, 10), false, fixedSpec(2.0), 0.5);
  EXPECT_NEAR(s.displayed().pos.x, 5.0, 1e-9); // push 本身不动显示位姿
  for (int i = 0; i < 30; ++i)
    s.step(kDt); // 新段一半
  EXPECT_NEAR(s.displayed().pos.x, 5.0, 1e-9);
  EXPECT_NEAR(s.displayed().pos.y, 5.0, 1e-9);
  EXPECT_EQ(s.queued(), 1u); // 定时长不排队
}

TEST(PoseSmoother, AutoIntervalEstimateMedianAndClamp) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(1), false, autoSpec(), 1.0);
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 1.0); // 首个间隔直接用
  s.push(at(2), false, autoSpec(), 4.0);       // 测值 [1,3] → 中位数 2
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 2.0);
  s.push(at(3), false, autoSpec(), 4.0); // 同一时刻(同帧)到达:不计间隔(R2 M2)
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 2.0);
  s.push(at(4), false, autoSpec(), 4.02); // [1,3,0.02] → 中位数 1(偶发挨着到的包不拖垮估计)
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 1.0);
  double t = 4.02;
  for (int i = 0; i < 8; ++i)
    s.push(at(5 + i), false, autoSpec(), t += 0.01); // 窗口全是 0.01 → 夹到下限 0.05
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 0.05);
  PoseSmoother u;
  u.reset(at(0), 0.0);
  u.push(at(1), false, autoSpec(), 4.9);
  EXPECT_DOUBLE_EQ(u.intervalEstimate(), 4.9);
  u.push(at(2), false, autoSpec(), 4.9 + 60.0); // 断流 60 s:不进估计
  EXPECT_DOUBLE_EQ(u.intervalEstimate(), 4.9);
}

TEST(PoseSmoother, AutoFirstSegmentWithoutHistoryUsesDefault) {
  PoseSmoother s; // 没 reset 过:没有上次命令时刻
  s.push(at(0), false, noneSpec(), 0.0);
  EXPECT_DOUBLE_EQ(s.intervalEstimate(), 1.0);
  s.push(at(2), false, autoSpec(), 100.0); // 间隔 100 s → 断流,段时长 = 缺省 1 s
  for (int i = 0; i < 15; ++i)
    s.step(kDt);
  EXPECT_NEAR(s.displayed().pos.x, 1.0, 1e-9);
}

// 红证类:段间丢余量 → 每段首帧位移偏小,稳定 1 Hz 流下每秒一顿。
TEST(PoseSmoother, AutoSteadyStreamPlaysAtConstantSpeed) {
  PoseSmoother s;
  Clock c;
  s.reset(at(0), -0.02); // 首个间隔 = 1 s(命令在 0.98 / 1.98 / … 到达)
  std::vector<double> xs;
  double next = 1.0;
  int k = 0;
  // 定位系统 1 Hz 发点(每次前进 3 m),渲染 30 fps;命令在帧边界之间到达(0.98 / 1.98 / …)。
  for (int f = 0; f < 300; ++f) {
    c.now += kDt;
    if (c.now >= next - 0.02) {
      ++k;
      s.push(at(3.0 * k), false, autoSpec(), next - 0.02);
      next += 1.0;
    }
    s.step(kDt);
    xs.push_back(s.displayed().pos.x);
  }
  // 稳态段(第 2 秒起,首段起步后):逐帧位移恒定 = 3 m/s × dt。
  double mx = 0.0, mn = 1e9;
  for (std::size_t i = 90; i + 1 < xs.size(); ++i) {
    const double d = xs[i + 1] - xs[i];
    mx = std::max(mx, d);
    mn = std::min(mn, d);
  }
  EXPECT_NEAR(mx, 3.0 * kDt, 1e-9);
  EXPECT_NEAR(mn, 3.0 * kDt, 1e-9);
  // 延迟约一个间隔:最后一个点在 ~1 s 后显示到位,不更多。
  EXPECT_LT(s.remaining(), 1.0 + 1e-9);
}

TEST(PoseSmoother, AutoBacklogCatchesUp) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(1), false, autoSpec(), 1.0); // 估计 1 s
  // 卡顿后 2 个点同帧到达:积压 3 段 = 3 个间隔(未超收拢上界)→ 加速回放(倍率 = 剩余 / 1.5 间隔)。
  s.push(at(2), false, autoSpec(), 1.0);
  s.push(at(3), false, autoSpec(), 1.0);
  ASSERT_EQ(s.queued(), 3u);
  const double rem0 = s.remaining();
  ASSERT_GT(rem0, 1.5 * s.intervalEstimate());
  s.step(kDt);
  EXPECT_GT(rem0 - s.remaining(), kDt * 1.01); // 一帧推进的时长 > dt
  int frames = 1;
  while (s.active() && frames < 1000) {
    s.step(kDt);
    ++frames;
  }
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, 3.0);
  EXPECT_LT(frames, static_cast<int>(rem0 / kDt) - 5);
  EXPECT_EQ(s.collapses(), 0u);
}

TEST(PoseSmoother, AutoBacklogBeyondCapCollapsesToNewestTarget) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(1), false, autoSpec(), 1.0); // 估计 1 s
  for (int i = 2; i <= 5; ++i)
    s.push(at(i), false, autoSpec(), 1.0); // 同帧 4 包:剩余 5 s > 3 个间隔 → 收拢
  EXPECT_GE(s.collapses(), 1u);
  EXPECT_LE(s.remaining(), PoseSmoother::kMaxLagIntervals * s.intervalEstimate() + 1e-9);
  EXPECT_DOUBLE_EQ(s.finalTarget().pos.x, 5.0); // 最新目标保留
  int frames = 0;
  while (s.active() && frames < 1000) {
    s.step(kDt);
    ++frames;
  }
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, 5.0);
  EXPECT_LE(frames, 3 * 30 + 1); // 延迟有界
}

// 红证类:中途落位(smooth:"none")必须立即停在新位姿,不能继续往旧目标走。
TEST(PoseSmoother, NoneMidInterpolationSnapsAndStops) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(10), false, fixedSpec(1.0), 0.0);
  for (int i = 0; i < 10; ++i)
    s.step(kDt);
  s.push(at(-4, 2, 0, 33.0), true, noneSpec(), 0.33);
  EXPECT_FALSE(s.active());
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, -4.0);
  EXPECT_DOUBLE_EQ(s.displayed().yaw_deg, 33.0);
  EXPECT_DOUBLE_EQ(s.speed(), 0.0);
  EXPECT_FALSE(s.step(kDt)); // 之后不再动
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, -4.0);
}

TEST(PoseSmoother, FixedZeroDurationIsSnap) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(7), false, fixedSpec(0.0), 0.0);
  EXPECT_FALSE(s.active());
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, 7.0);
}

// 红证类:朝向必须走最短弧(350° → 10° 经过 0°,不是倒转 340°)。
TEST(PoseSmoother, YawInterpolatesAlongShortestArc) {
  PoseSmoother s;
  s.reset(at(0, 0, 0, 350.0), 0.0);
  s.push(at(0, 0, 0, 10.0), true, fixedSpec(1.0), 0.0);
  for (int i = 0; i < 15; ++i)
    s.step(kDt);
  EXPECT_NEAR(std::remainder(s.displayed().yaw_deg, 360.0), 0.0, 1e-9);
  for (int i = 0; i < 15; ++i)
    s.step(kDt);
  EXPECT_NEAR(std::remainder(s.displayed().yaw_deg - 10.0, 360.0), 0.0, 1e-9);
  EXPECT_NEAR(shortestArcDeg(350.0, 10.0), 20.0, 1e-12);
  EXPECT_NEAR(shortestArcDeg(10.0, 350.0), -20.0, 1e-12);
}

TEST(PoseSmoother, FaceMotionTurnsTowardTravelWithRateLimit) {
  PoseSmoother s;
  s.reset(at(0, 0, 0, 0.0), 0.0);
  // 沿 +Y 走(方位 90°),起始朝 0°:转向受限速 360°/s 约束,且逐步逼近 90°。
  s.push(at(0, 30), false, fixedSpec(10.0, /*face=*/true), 0.0);
  double prev = 0.0;
  for (int i = 0; i < 60; ++i) {
    s.step(kDt);
    const double y = s.displayed().yaw_deg;
    EXPECT_LE(std::fabs(shortestArcDeg(prev, y)), 360.0 * kDt + 1e-9);
    EXPECT_GE(shortestArcDeg(prev, y), -1e-9); // 单调转向目标
    prev = y;
  }
  EXPECT_NEAR(s.displayed().yaw_deg, 90.0, 0.5);
  // face_offset 叠加。
  PoseSmoother o;
  o.reset(at(0), 0.0);
  SmoothSpec sp = fixedSpec(10.0, true);
  sp.face_offset_deg = 90.0;
  o.push(at(30, 0), false, sp, 0.0);
  for (int i = 0; i < 90; ++i)
    o.step(kDt);
  EXPECT_NEAR(o.displayed().yaw_deg, 90.0, 0.5);
}

TEST(PoseSmoother, FaceMotionIgnoresSlowMotionAndExplicitYawWins) {
  PoseSmoother s;
  s.reset(at(0, 0, 0, 45.0), 0.0);
  s.push(at(0, 0.5), false, fixedSpec(10.0, true), 0.0); // 0.05 m/s < 0.1
  for (int i = 0; i < 30; ++i)
    s.step(kDt);
  EXPECT_DOUBLE_EQ(s.displayed().yaw_deg, 45.0);
  PoseSmoother e;
  e.reset(at(0, 0, 0, 0.0), 0.0);
  e.push(at(0, 30, 0, 180.0), true, fixedSpec(1.0, true), 0.0); // 给了 yaw:按 yaw 插值,不跟运动方向
  for (int i = 0; i < 15; ++i)
    e.step(kDt);
  EXPECT_NEAR(std::fabs(e.displayed().yaw_deg), 90.0, 1e-9);
}

TEST(PoseSmoother, LerpHookReceivesGroundFlags) {
  PoseSmoother s;
  s.reset(at(0, 0, 5, 0, /*ground=*/true), 0.0);
  s.push(at(10, 0, 9, 0, /*ground=*/false), false, fixedSpec(1.0), 0.0);
  int calls = 0;
  bool ga = false, gb = true;
  const MotionLerpFn hook = [&](const glm::dvec3& a, const glm::dvec3& b, double t, bool a_g, bool b_g) {
    ++calls;
    ga = a_g;
    gb = b_g;
    glm::dvec3 p = linearMotionLerp(a, b, t);
    p.z = 100.0; // 钩子输出原样采用
    return p;
  };
  s.step(kDt, hook);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(ga);
  EXPECT_FALSE(gb);
  EXPECT_DOUBLE_EQ(s.displayed().pos.z, 100.0);
  EXPECT_FALSE(s.displayed().ground_clamped);
}

TEST(PoseSmoother, StepWithZeroDtDoesNothing) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  s.push(at(10), false, fixedSpec(1.0), 0.0);
  EXPECT_FALSE(s.step(0.0));
  EXPECT_DOUBLE_EQ(s.displayed().pos.x, 0.0);
  EXPECT_TRUE(s.active());
}

// 红证类:段间丢余量 —— 段边界落在帧中间时,丢掉余量的实现在边界帧位移偏小(1 Hz 下每秒一顿)。
TEST(PoseSmoother, QueueCarriesLeftoverAcrossSegmentBoundaries) {
  MotionTuning t;
  t.backlog_intervals = 100.0; // 关掉积压加速,只看段间衔接
  PoseSmoother s(t);
  s.reset(at(0), 0.0);
  // 间隔 0.37 s(= 11.1 帧,段边界落在帧中间),每段 +1 m:一次性排好 3 段。
  s.push(at(1), false, autoSpec(), 0.37);
  s.push(at(2), false, autoSpec(), 0.74);
  s.push(at(3), false, autoSpec(), 1.11);
  ASSERT_DOUBLE_EQ(s.intervalEstimate(), 0.37);
  std::vector<double> xs{0.0};
  for (int i = 0; i < 30; ++i) {
    s.step(kDt);
    xs.push_back(s.displayed().pos.x);
  }
  const double want = kDt / 0.37; // 匀速 1 m / 0.37 s
  for (std::size_t i = 1; i < xs.size(); ++i)
    EXPECT_NEAR(xs[i] - xs[i - 1], want, 1e-9) << "frame " << i;
}

// R2 I2 红证类:auto 队列无界 —— 100 Hz 推送、30 fps 渲染(运动时钟逐帧推进,同帧命令同一时刻)跑 2
// 分钟,积压与落后必须有界。
TEST(PoseSmoother, AutoFloodKeepsQueueAndLagBounded) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  double clock = 0.0, wall = 0.0, next_cmd = 0.0, x = 0.0;
  std::size_t max_q = 0;
  double max_rem_over_est = 0.0, max_lag = 0.0;
  for (int f = 0; f < 30 * 120; ++f) {
    wall += kDt;
    while (next_cmd <= wall) {
      x += 0.1; // 10 m/s
      s.push(at(x), false, autoSpec(), clock);
      next_cmd += 0.01;
    }
    s.step(kDt);
    clock += kDt;
    max_q = std::max(max_q, s.queued());
    max_rem_over_est = std::max(max_rem_over_est, s.remaining() / s.intervalEstimate());
    if (f > 60)
      max_lag = std::max(max_lag, x - s.displayed().pos.x);
  }
  EXPECT_LE(max_q, PoseSmoother::kMaxQueued);
  EXPECT_LE(max_rem_over_est, PoseSmoother::kMaxLagIntervals + 1e-9);
  EXPECT_LT(max_lag, 2.0); // 10 m/s × 不到 0.2 s
  // remaining() 与逐段求和一致(O(1) 维护的总和没有漂移)
  EXPECT_NEAR(s.remaining(), s.remainingSlowForTest(), 1e-9);
}

// R2 M2 红证类:同帧突发(间隔 ≈ 0)不进间隔估计 —— 断流后一帧内补发 6 包,稳定 1 Hz 的估计不被压到 0.05 s。
TEST(PoseSmoother, SameFrameBurstDoesNotCollapseIntervalEstimate) {
  PoseSmoother s;
  s.reset(at(0), 0.0);
  double clock = 0.0, x = 0.0;
  auto push = [&] {
    x += 1.0;
    s.push(at(x), false, autoSpec(), clock);
  };
  auto run = [&](double secs) {
    const int n = static_cast<int>(secs / kDt + 0.5);
    for (int i = 0; i < n; ++i) {
      s.step(kDt);
      clock += kDt;
    }
  };
  for (int i = 0; i < 10; ++i) {
    push();
    run(1.0);
  }
  run(8.0); // 断流
  for (int i = 0; i < 6; ++i)
    push(); // 一帧内补发 6 包
  EXPECT_NEAR(s.intervalEstimate(), 1.0, 0.05);
  for (int i = 0; i < 4; ++i) {
    run(1.0);
    push();
    EXPECT_NEAR(s.intervalEstimate(), 1.0, 0.05) << "补发后第 " << i + 1 << " 秒";
  }
  // 补发的积压被收拢(> 3 个间隔 → 从显示位姿直奔最新目标):之后照 1 Hz 匀速走,不会一直落后几秒
  int moving = 0;
  for (int k = 0; k < 3; ++k) {
    for (int i = 0; i < 30; ++i) {
      moving += s.step(kDt) ? 1 : 0;
      clock += kDt;
    }
    push();
  }
  EXPECT_GE(moving, 85);
  EXPECT_LT(x - s.displayed().pos.x, 2.5);
}
