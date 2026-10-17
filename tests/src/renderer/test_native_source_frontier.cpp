#include <gtest/gtest.h>
#include <hongtu/renderer/native_source_frontier.hpp>
#include <hongtu/renderer/native_source_refinement.hpp>
using namespace hongtu::renderer;
namespace {
  std::vector<NativeSourceNode> nodes() {
    return {{"root", {"a", "b"}}, {"a", {"a0", "a1"}}, {"b", {}}, {"a0", {}}, {"a1", {}}};
  }
} // namespace
TEST(NativeSourceFrontier, EveryUploadMustFinishBeforeParentDisappears) {
  NativeSourceFrontier cut(nodes(), "root", {"a", "b"}, 300, 512);
  auto ticket = cut.prepare("a", {"a1", "a0"}, 48, 2);
  ASSERT_TRUE(ticket);
  EXPECT_EQ(cut.concurrentBytes(), 348);
  EXPECT_FALSE(cut.commit(*ticket));
  ASSERT_TRUE(cut.markPartReady(*ticket, 1));
  EXPECT_FALSE(cut.commit(*ticket));
  EXPECT_EQ(cut.visible(), (NativeSourceFrontier::Cut{"a", "b"}));
  ASSERT_TRUE(cut.markPartReady(*ticket, 0));
  ASSERT_TRUE(cut.commit(*ticket));
  EXPECT_EQ(cut.visible(), (NativeSourceFrontier::Cut{"a0", "a1", "b"}));
  EXPECT_EQ(cut.residentBytes(), 348); // Shared parent pages remain charged.
}
TEST(NativeSourceFrontier, MissingSiblingAndConcurrentBudgetKeepCompleteParent) {
  NativeSourceFrontier cut(nodes(), "root", {"a", "b"}, 480, 512);
  EXPECT_FALSE(cut.prepare("a", {"a0"}, 10, 1));
  EXPECT_FALSE(cut.prepare("a", {"a0", "a1", "a1"}, 10, 1));
  EXPECT_FALSE(cut.prepare("a", {"a0", "a1"}, 33, 1));
  EXPECT_FALSE(cut.prepare("b", {}, 10, 1));
  EXPECT_EQ(cut.visible(), (NativeSourceFrontier::Cut{"a", "b"}));
}
TEST(NativeSourceFrontier, CameraCancellationRejectsStaleCompletion) {
  NativeSourceFrontier cut(nodes(), "root", {"a", "b"}, 300, 512);
  auto old = cut.prepare("a", {"a0", "a1"}, 48, 2);
  ASSERT_TRUE(old);
  ASSERT_TRUE(cut.cancel(*old));
  auto next = cut.prepare("a", {"a0", "a1"}, 48, 2);
  ASSERT_TRUE(next);
  EXPECT_FALSE(cut.markPartReady(*old, 0));
  EXPECT_FALSE(cut.commit(*old));
  EXPECT_FALSE(cut.markPartReady(*next, 2));
  cut.reset({"root"}, 100);
  EXPECT_FALSE(cut.commit(*next));
  EXPECT_EQ(cut.concurrentBytes(), 100);
}
TEST(NativeSourceFrontier, RejectsHolesOverlapAndCorruptTopology) {
  EXPECT_THROW((NativeSourceFrontier(nodes(), "root", {"a0", "b"}, 1, 512)), std::invalid_argument);
  EXPECT_THROW((NativeSourceFrontier(nodes(), "root", {"root", "a"}, 1, 512)), std::invalid_argument);
  EXPECT_THROW((NativeSourceFrontier({{"root", {"root"}}}, "root", {"root"}, 1, 512)), std::invalid_argument);
  EXPECT_THROW((NativeSourceFrontier({{"root", {"a"}}}, "root", {"root"}, 1, 512)), std::invalid_argument);
}

TEST(NativeSourceRefinement, TerminalCenterDoesNotBlockVisibleNeighbors) {
  EXPECT_EQ(nativeSourceRefinementScore(100, 300, 720, 0, false), 0);
  EXPECT_GT(nativeSourceRefinementScore(12, 300, 720, .4f, true), 0);
  EXPECT_EQ(nativeSourceRefinementScore(6, 300, 720, 0, true), 0);
}
TEST(NativeSourceRefinement, CoverageAndRemainingQualityDeterminePriority) {
  EXPECT_GT(nativeSourceRefinementScore(12, 300, 720, .2f, true), nativeSourceRefinementScore(12, 30, 720, .2f, true));
  EXPECT_GT(nativeSourceRefinementScore(24, 300, 720, .2f, true), nativeSourceRefinementScore(12, 300, 720, .2f, true));
  EXPECT_EQ(nativeSourceRefinementScore(12, 300, 0, .2f, true), 0);
}
