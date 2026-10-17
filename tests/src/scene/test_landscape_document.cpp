#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
#include <gtest/gtest.h>

#include "../../../apps/showcase_demo/landscape_document.hpp"

namespace {
  using namespace hongtu::landscape;
  Json feature(const std::string& type, Json positions) {
    return {{"id", "feature-a"}, {"name", "标注"}, {"type", type}, {"positions", positions}, {"style", defaultStyle()}};
  }
  TEST(LandscapeDocument, ConcaveRegionPreservesAreaInBothWindings) {
    std::vector<glm::vec3> ring = {{0, 0, 0}, {10, 0, 0}, {10, 4, 0}, {4, 4, 0}, {4, 10, 0}, {0, 10, 0}};
    for (int pass = 0; pass < 2; ++pass) {
      auto indices = triangulate(ring);
      ASSERT_EQ(indices.size(), 12);
      double area = 0;
      for (size_t i = 0; i < indices.size(); i += 3)
        area += cross2(ring[indices[i]], ring[indices[i + 1]], ring[indices[i + 2]]) / 2;
      EXPECT_NEAR(area, 64, 1e-6);
      std::reverse(ring.begin(), ring.end());
    }
  }
  TEST(LandscapeDocument, CrossedAndTouchingRegionsAreRejected) {
    EXPECT_THROW(triangulate({{0, 0, 0}, {10, 10, 0}, {0, 10, 0}, {10, 0, 0}}), std::runtime_error);
    EXPECT_THROW(triangulate({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {5, 0, 0}, {0, 10, 0}}), std::runtime_error);
  }
  TEST(LandscapeDocument, CoordinateAndDuplicateFeatureFailuresAreAtomic) {
    auto doc = emptyDocument();
    doc["layers"][0]["features"].push_back(feature("point", {{1, 2, 3}}));
    EXPECT_NO_THROW(validate(doc));
    auto bad = doc;
    bad["coordinates"]["origin"][0] = 0;
    EXPECT_THROW(validate(bad), std::runtime_error);
    bad = doc;
    bad["layers"][0]["features"].push_back(bad["layers"][0]["features"][0]);
    EXPECT_THROW(validate(bad), std::runtime_error);
  }
  TEST(LandscapeDocument, TransparentStylesAndLineLimits) {
    auto doc = emptyDocument();
    auto line = feature("line", {{0, 0, 0}, {10, 0, 0}});
    line["style"]["opacity"] = 0;
    line["style"]["fillOpacity"] = 0;
    line["style"]["widthPx"] = 32;
    doc["layers"][0]["features"].push_back(line);
    EXPECT_NO_THROW(validate(doc));
    doc["layers"][0]["features"][0]["style"]["opacity"] = 1.01;
    EXPECT_THROW(validate(doc), std::runtime_error);
  }
  TEST(LandscapeDocument, SaveReloadAndInvalidSavePreservePreviousFile) {
    auto file = std::filesystem::temp_directory_path() / "hongtu_landscape_unit.json";
    auto doc = emptyDocument();
    doc["layers"][0]["name"] = "点线面图层";
    save(file, doc);
    EXPECT_EQ(load(file), doc);
    auto bad = doc;
    bad["schema"] = "wrong";
    EXPECT_THROW(save(file, bad), std::runtime_error);
    EXPECT_EQ(load(file), doc);
    std::filesystem::remove(file);
  }
  TEST(LandscapeDocument, EmbeddedIconSurvivesPortableBase64RoundTrip) {
    std::vector<uint8_t> bytes = {137, 80, 78, 71, 13, 10, 26, 10, 0, 255, 23};
    EXPECT_EQ(decode64(encode64(bytes)), bytes);
  }
  TEST(LandscapeDocument, LineLabelUsesHalfLengthInsteadOfVertexAverage) {
    auto f = feature("line", {{0, 0, 0}, {2, 0, 0}, {20, 0, 0}});
    auto anchor = labelAnchor(f);
    EXPECT_NEAR(anchor.x, 10, 1e-6);
    EXPECT_NEAR(anchor.y, 0, 1e-6);
  }
  TEST(LandscapeDocument, ConcaveRegionLabelIsInsideATriangle) {
    auto f = feature("region", {{0, 0, 0}, {10, 0, 0}, {10, 4, 0}, {4, 4, 0}, {4, 10, 0}, {0, 10, 0}});
    auto anchor = labelAnchor(f);
    EXPECT_GE(anchor.x, 0);
    EXPECT_GE(anchor.y, 0);
    EXPECT_TRUE(anchor.x <= 4 || anchor.y <= 4);
  }
  TEST(LandscapeDocument, LabelsAndEditedVerticesSurviveSaveReload) {
    auto file = std::filesystem::temp_directory_path() / "hongtu_landscape_labels_unit.json";
    auto doc = emptyDocument();
    auto f = feature("line", {{0, 0, 0}, {20, 5, 8}});
    f["label"] = defaultLabel();
    f["label"]["visible"] = true;
    f["label"]["text"] = "消防道路";
    f["label"]["offset"] = {-80, 30};
    f["positions"][1] = {25.125, 9.75, 10.5};
    doc["layers"][0]["features"].push_back(f);
    save(file, doc);
    EXPECT_EQ(load(file), doc);
    auto invalid = doc;
    invalid["layers"][0]["features"][0]["label"]["fontPx"] = 80;
    EXPECT_THROW(save(file, invalid), std::runtime_error);
    EXPECT_EQ(load(file), doc);
    invalid = doc;
    invalid["layers"][0]["features"][0]["positions"][1][0] = 200000;
    EXPECT_THROW(save(file, invalid), std::runtime_error);
    EXPECT_EQ(load(file), doc);
    std::filesystem::remove(file);
  }
} // namespace
