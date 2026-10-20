#include <gtest/gtest.h>
#include <hongtu/renderer/native_level_index.hpp>
#include <hongtu/renderer/native_source_frontier.hpp>

#include <limits>
#include <random>

using namespace hongtu::renderer;

TEST(NativeLevelIndex, ViewportCornersAndNearPlaneHaveCoverage) {
  NativeLevelIndex index;
  index.build({{0, 16, {8, 9, 8}, {12, 11, 12}},
               {1, 17, {-12, 9, 8}, {-8, 11, 12}},
               {2, 18, {-1, -1, -1}, {1, 2, 1}},
               {3, 0, {-200, -200, -200}, {200, 200, 200}},
               {4, 0, {30, 9, 30}, {32, 11, 32}},
               {5, 0, {-1, -20, -1}, {1, -10, 1}}});
  const auto found = index.query({{0, 0, 0}, {0, 1, 0}, 90, 1, 1, 100});
  EXPECT_EQ(found.ids, (std::vector<uint32_t>{0, 1, 2, 3}));
}

TEST(NativeLevelIndex, TangentFarPlaneVerticalCameraAndEmptyIndex) {
  NativeLevelIndex index;
  EXPECT_TRUE(index.query({{0, 0, 10}, {0, 0, 0}}).ids.empty());
  index.build(
      {{0, 0, {-1, 99, -1}, {1, 100, 1}}, {1, 0, {-1, 101, -1}, {1, 102, 1}}, {2, 0, {10, 10, 0}, {10, 10, 0}}});
  EXPECT_EQ(index.query({{0, 0, 0}, {0, 1, 0}, 90, 1, 1, 100}).ids, (std::vector<uint32_t>{0, 2}));
  index.build({{7, 23, {-1, -1, 0}, {1, 1, 1}}});
  EXPECT_EQ(index.query({{0, 0, 10}, {0, 0, 0}, 50, 3}).ids, (std::vector<uint32_t>{7}));
}

TEST(NativeLevelIndex, RejectsInvalidInputWithoutReplacingPreviousIndex) {
  NativeLevelIndex index;
  index.build({{0, 0, {-1, 5, -1}, {1, 6, 1}}});
  EXPECT_THROW(index.build({{0, 0, {0, 0, 0}, {-1, 1, 1}}}), std::invalid_argument);
  EXPECT_THROW(index.build({{1, 0, {0, 0, 0}, {1, 1, 1}}, {1, 2, {1, 1, 1}, {2, 2, 2}}}), std::invalid_argument);
  EXPECT_THROW(index.build({{0, 0, {0, 0, 0}, {std::numeric_limits<double>::infinity(), 1, 1}}}),
               std::invalid_argument);
  EXPECT_EQ(index.size(), 1u);
  EXPECT_THROW(index.query({{0, 0, 0}, {0, 0, 0}}), std::invalid_argument);
  EXPECT_THROW(index.query({{0, 0, 0}, {0, 1, 0}, 0}), std::invalid_argument);
}

TEST(NativeLevelIndex, DenseForestMatchesIndependentCornerOracleAndRejectsMostEntries) {
  std::mt19937_64 random(13579);
  std::uniform_real_distribution<double> location(-5000, 5000), extent(0, 20), origin(-500, 500);
  std::vector<NativeLevelBounds> entries;
  for (uint32_t i = 0; i < 100000; ++i) {
    const glm::dvec3 low(location(random), location(random), location(random));
    entries.push_back({i, int(i % 24), low, low + glm::dvec3(extent(random), extent(random), extent(random))});
  }
  NativeLevelIndex index;
  index.build(entries);
  for (int run = 0; run < 20; ++run) {
    const glm::dvec3 camera(origin(random), origin(random), origin(random));
    const NativeLevelView view{camera, camera + glm::dvec3(.2, 1, -.1), 40, 1.8, .1, 150};
    const auto planes = view.planes();
    std::vector<uint32_t> oracle;
    for (const auto& entry : entries) {
      bool outside = false;
      for (const auto& plane : planes) {
        bool inside = false;
        for (int mask = 0; mask < 8; ++mask) {
          const glm::dvec3 p(mask & 1 ? entry.high.x : entry.low.x,
                             mask & 2 ? entry.high.y : entry.low.y,
                             mask & 4 ? entry.high.z : entry.low.z);
          inside |= glm::dot(glm::dvec3(plane), p) + plane.w >= -1e-7;
        }
        outside |= !inside;
      }
      if (!outside)
        oracle.push_back(entry.id);
    }
    const auto actual = index.query(view);
    EXPECT_EQ(actual.ids, oracle);
    EXPECT_LT(actual.entryBoundsTested, entries.size() / 100);
  }
}

TEST(NativeLevelIndex, MixedDescendantCutCommitsOnlyAfterEveryUpload) {
  NativeSourceFrontier cut(
      {{"r", {"a", "b"}}, {"a", {"a1", "a2"}}, {"a1", {"fine"}}, {"a2", {}}, {"fine", {}}, {"b", {}}},
      "r",
      {"r"},
      40,
      100);
  const auto ticket = cut.prepareDescendants("r", {"fine", "a2", "b"}, 50, 3);
  ASSERT_TRUE(ticket);
  EXPECT_EQ(cut.visible(), NativeSourceFrontier::Cut{"r"});
  EXPECT_EQ(cut.concurrentBytes(), 90u);
  cut.markPartReady(*ticket, 0);
  cut.markPartReady(*ticket, 1);
  EXPECT_FALSE(cut.commit(*ticket));
  cut.markPartReady(*ticket, 2);
  EXPECT_TRUE(cut.commit(*ticket));
  EXPECT_EQ(cut.visible(), (NativeSourceFrontier::Cut{"fine", "a2", "b"}));
}
TEST(NativeLevelIndex, MixedDescendantCutRefusesHolesOverlapsAndExcessMemory) {
  NativeSourceFrontier cut({{"r", {"a", "b"}}, {"a", {"fine"}}, {"fine", {}}, {"b", {}}}, "r", {"a", "b"}, 40, 100);
  EXPECT_FALSE(cut.prepareDescendants("a", {"b"}, 20, 1));
  EXPECT_FALSE(cut.prepareDescendants("a", {"a", "fine"}, 20, 1));
  EXPECT_FALSE(cut.prepareDescendants("a", {"fine", "fine"}, 20, 1));
  EXPECT_FALSE(cut.prepareDescendants("a", {"fine"}, 61, 1));
  EXPECT_FALSE(cut.prepareDescendants("a", {"fine"}, 20, 65));
  EXPECT_EQ(cut.visible(), (NativeSourceFrontier::Cut{"a", "b"}));
}
