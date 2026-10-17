#include <gtest/gtest.h>
#include <hongtu/scene/timeline/timeline.hpp>

#include <glm/glm.hpp>

using hongtu::scene::CallbackDriver;
using hongtu::scene::Interp;
using hongtu::scene::Timeline;
using hongtu::scene::Track;

TEST(TimelineTest, DefaultsToNotPlaying) {
  Timeline tl;
  EXPECT_FALSE(tl.isPlaying());
  EXPECT_FLOAT_EQ(tl.currentTime(), 0.0f);
}

TEST(TimelineTest, PlayAdvancesTime) {
  Timeline tl;
  tl.setDuration(10.0f);
  tl.play();
  tl.update(2.5f);
  EXPECT_FLOAT_EQ(tl.currentTime(), 2.5f);
}

TEST(TimelineTest, PauseStopsAdvance) {
  Timeline tl;
  tl.setDuration(10.0f);
  tl.play();
  tl.update(2.0f);
  tl.pause();
  tl.update(2.0f);
  EXPECT_FLOAT_EQ(tl.currentTime(), 2.0f);
}

TEST(TimelineTest, SeekJumpsToTime) {
  Timeline tl;
  tl.setDuration(10.0f);
  tl.seek(7.5f);
  EXPECT_FLOAT_EQ(tl.currentTime(), 7.5f);
}

TEST(TimelineTest, SpeedMultiplier) {
  Timeline tl;
  tl.setDuration(100.0f);
  tl.setSpeed(2.0f);
  tl.play();
  tl.update(1.0f);
  EXPECT_FLOAT_EQ(tl.currentTime(), 2.0f);
}

TEST(TimelineTest, LoopWrapsTime) {
  Timeline tl;
  tl.setDuration(5.0f);
  tl.setLoop(true);
  tl.play();
  tl.update(7.0f);     // 7 > duration → wrap to 2
  EXPECT_NEAR(tl.currentTime(), 2.0f, 1e-4f);
  EXPECT_TRUE(tl.isPlaying());
}

TEST(TimelineTest, EndStopsWhenNotLooping) {
  Timeline tl;
  tl.setDuration(5.0f);
  tl.play();
  tl.update(10.0f);
  EXPECT_FALSE(tl.isPlaying());
  EXPECT_FLOAT_EQ(tl.currentTime(), 5.0f);
}

// =============================================================================
// CallbackDriver — verifies setter is invoked with sampled value
// =============================================================================

TEST(CallbackDriverTest, FiresSetterEachUpdate) {
  Timeline tl;
  tl.setDuration(10.0f);

  Track<float> trk;
  trk.addKey(0.0f, 0.0f, Interp::Linear);
  trk.addKey(10.0f, 100.0f, Interp::Linear);

  float observed = -1.0f;
  tl.addDriver(std::make_unique<CallbackDriver<float>>(
      std::move(trk),
      [&observed](const float& v) { observed = v; }));

  tl.seek(5.0f);
  tl.update(0.0f);     // sample without advancing
  EXPECT_NEAR(observed, 50.0f, 1e-3f);
}

TEST(CallbackDriverTest, MultipleDriversAllFire) {
  Timeline tl;
  tl.setDuration(5.0f);

  Track<float> a, b;
  a.addKey(0.0f, 0.0f); a.addKey(5.0f, 10.0f);
  b.addKey(0.0f, 100.0f); b.addKey(5.0f, 200.0f);

  float va = 0, vb = 0;
  tl.addDriver(std::make_unique<CallbackDriver<float>>(std::move(a),
      [&va](const float& v) { va = v; }));
  tl.addDriver(std::make_unique<CallbackDriver<float>>(std::move(b),
      [&vb](const float& v) { vb = v; }));

  tl.seek(2.5f);
  tl.update(0.0f);
  EXPECT_NEAR(va, 5.0f, 1e-3f);
  EXPECT_NEAR(vb, 150.0f, 1e-3f);
}

TEST(CallbackDriverTest, BoolEnableTrack) {
  Timeline tl;
  tl.setDuration(10.0f);
  Track<bool> trk;
  trk.addKey(0.0f, false);
  trk.addKey(5.0f, true);

  bool observed = false;
  tl.addDriver(std::make_unique<CallbackDriver<bool>>(std::move(trk),
      [&observed](const bool& v) { observed = v; }));

  tl.seek(3.0f);
  tl.update(0.0f);
  EXPECT_FALSE(observed);   // step before key 5

  tl.seek(5.0f);
  tl.update(0.0f);
  EXPECT_TRUE(observed);
}
