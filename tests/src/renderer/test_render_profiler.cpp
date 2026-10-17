#include <gtest/gtest.h>
#include <hongtu/renderer/profiler/render_profiler.hpp>

using namespace hongtu::renderer;

TEST(TimeSeriesTest, PushAndRead) {
  TimeSeries ts(10);
  ts.push(1.0);
  ts.push(2.0);
  ts.push(3.0);
  EXPECT_DOUBLE_EQ(ts.latest(), 3.0);
  EXPECT_EQ(ts.count(), 3u);
  EXPECT_DOUBLE_EQ(ts.average(), 2.0);
}

TEST(TimeSeriesTest, MinMax) {
  TimeSeries ts(10);
  ts.push(5.0);
  ts.push(1.0);
  ts.push(9.0);
  ts.push(3.0);
  EXPECT_DOUBLE_EQ(ts.min(), 1.0);
  EXPECT_DOUBLE_EQ(ts.max(), 9.0);
}

TEST(TimeSeriesTest, RollingWindow) {
  TimeSeries ts(3);
  ts.push(1.0);
  ts.push(2.0);
  ts.push(3.0);
  ts.push(4.0); // Should evict 1.0
  EXPECT_EQ(ts.count(), 3u);
  EXPECT_DOUBLE_EQ(ts.min(), 2.0);
}

TEST(TimeSeriesTest, Empty) {
  TimeSeries ts;
  EXPECT_DOUBLE_EQ(ts.latest(), 0.0);
  EXPECT_DOUBLE_EQ(ts.average(), 0.0);
  EXPECT_EQ(ts.count(), 0u);
}

TEST(TimeSeriesTest, Clear) {
  TimeSeries ts;
  ts.push(1.0);
  ts.push(2.0);
  ts.clear();
  EXPECT_EQ(ts.count(), 0u);
}

// ============================================================================
// RenderProfiler
// ============================================================================

TEST(RenderProfilerTest, RecordFrame) {
  RenderProfiler profiler(100);
  profiler.beginFrame(0);
  profiler.recordPass("GBuffer", 1.5, 100, 50000);
  profiler.recordPass("Lighting", 0.8, 1, 6);
  profiler.recordPass("PostProcess", 0.3, 3, 18);
  profiler.endFrame(16.6);

  const auto& last = profiler.getLastFrame();
  EXPECT_EQ(last.frame_number, 0u);
  EXPECT_EQ(last.pass_timings.size(), 3u);
  EXPECT_NEAR(last.total_gpu_ms, 2.6, 0.01);
  EXPECT_EQ(last.total_draw_calls, 104u);
  EXPECT_NEAR(last.frame_time_ms, 16.6, 0.01);
}

TEST(RenderProfilerTest, FrameHistory) {
  RenderProfiler profiler(5);
  for (uint32_t i = 0; i < 10; ++i) {
    profiler.beginFrame(i);
    profiler.recordPass("Main", 1.0 + i * 0.1, 10, 1000);
    profiler.endFrame(16.0 + i * 0.5);
  }
  // History should be capped at 5
  EXPECT_EQ(profiler.getHistory().size(), 5u);
  EXPECT_EQ(profiler.getHistory().back().frame_number, 9u);
}

TEST(RenderProfilerTest, AverageFPS) {
  RenderProfiler profiler;
  for (int i = 0; i < 60; ++i) {
    profiler.beginFrame(static_cast<uint32_t>(i));
    profiler.endFrame(16.667); // ~60 FPS
  }
  EXPECT_NEAR(profiler.getAverageFPS(), 60.0, 1.0);
}

TEST(RenderProfilerTest, P99FrameTime) {
  RenderProfiler profiler;
  // 99 frames at 16ms, 1 frame at 50ms
  for (int i = 0; i < 99; ++i) {
    profiler.beginFrame(static_cast<uint32_t>(i));
    profiler.endFrame(16.0);
  }
  profiler.beginFrame(99);
  profiler.endFrame(50.0);

  double p99 = profiler.getP99FrameTime();
  EXPECT_GE(p99, 16.0);
}

TEST(RenderProfilerTest, MemoryUsage) {
  RenderProfiler profiler;
  MemoryUsage usage;
  usage.vertex_buffers = 1024 * 1024;
  usage.textures = 512 * 1024 * 1024;
  profiler.updateMemoryUsage(usage);

  EXPECT_EQ(profiler.getMemoryUsage().vertex_buffers, 1024u * 1024u);
  EXPECT_EQ(profiler.getMemoryUsage().total(),
            1024u * 1024u + 512u * 1024u * 1024u);
}

TEST(RenderProfilerTest, PerPassTimeSeries) {
  RenderProfiler profiler;
  for (int i = 0; i < 10; ++i) {
    profiler.beginFrame(static_cast<uint32_t>(i));
    profiler.recordPass("Shadow", 0.5 + i * 0.1);
    profiler.recordPass("GBuffer", 1.0);
    profiler.endFrame(16.0);
  }

  auto* shadow_ts = profiler.getPassTimeSeries("Shadow");
  ASSERT_NE(shadow_ts, nullptr);
  EXPECT_EQ(shadow_ts->count(), 10u);
  EXPECT_NEAR(shadow_ts->latest(), 1.4, 0.01); // 0.5 + 9*0.1

  EXPECT_EQ(profiler.getPassTimeSeries("NonExistent"), nullptr);
}

TEST(RenderProfilerTest, SummaryText) {
  RenderProfiler profiler;
  profiler.beginFrame(0);
  profiler.recordPass("Main", 5.0, 200, 100000);
  profiler.endFrame(16.0);

  auto text = profiler.getSummaryText();
  EXPECT_FALSE(text.empty());
  // Should contain FPS info
  EXPECT_NE(text.find("FPS"), std::string::npos);
}

TEST(RenderProfilerTest, Reset) {
  RenderProfiler profiler;
  profiler.beginFrame(0);
  profiler.endFrame(16.0);
  profiler.reset();
  EXPECT_EQ(profiler.getHistory().size(), 0u);
  EXPECT_DOUBLE_EQ(profiler.getFrameTimeSeries().latest(), 0.0);
}

TEST(MemoryUsageTest, Total) {
  MemoryUsage usage;
  usage.vertex_buffers = 100;
  usage.index_buffers = 200;
  usage.uniform_buffers = 300;
  usage.textures = 400;
  usage.render_targets = 500;
  usage.staging = 600;
  EXPECT_EQ(usage.total(), 2100u);
}
