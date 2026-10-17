#include <gtest/gtest.h>

#include <cstring>
#include <hongtu/gis/dlg_layer.hpp>
#include <hongtu/gis/geojson_reader.hpp>
#include <hongtu/gis/geometry.hpp>
#include <hongtu/gis/shapefile_reader.hpp>
#include <hongtu/gis/tiles3d_reader.hpp>
#include <nlohmann/json.hpp>

using namespace hongtu::gis;

// ============================================================================
// Geometry
// ============================================================================

TEST(GeometryTest, PointBounds) {
  Geometry geom;
  geom.type = GeometryType::Point;
  geom.point = {116.4, 39.9, 0, false};

  double x0, y0, x1, y1;
  geom.getBounds(x0, y0, x1, y1);
  EXPECT_DOUBLE_EQ(x0, 116.4);
  EXPECT_DOUBLE_EQ(y0, 39.9);
}

TEST(GeometryTest, LineStringVertexCount) {
  Geometry geom;
  geom.type = GeometryType::LineString;
  geom.points = {{0, 0}, {1, 1}, {2, 2}};
  EXPECT_EQ(geom.getVertexCount(), 3u);
}

TEST(GeometryTest, PolygonBounds) {
  Geometry geom;
  geom.type = GeometryType::Polygon;
  geom.rings = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}};

  double x0, y0, x1, y1;
  geom.getBounds(x0, y0, x1, y1);
  EXPECT_DOUBLE_EQ(x0, 0.0);
  EXPECT_DOUBLE_EQ(y0, 0.0);
  EXPECT_DOUBLE_EQ(x1, 10.0);
  EXPECT_DOUBLE_EQ(y1, 10.0);
}

TEST(GeometryTest, TypeNames) {
  EXPECT_STREQ(geometryTypeName(GeometryType::Point), "Point");
  EXPECT_STREQ(geometryTypeName(GeometryType::Polygon), "Polygon");
  EXPECT_STREQ(geometryTypeName(GeometryType::MultiPolygon), "MultiPolygon");
}

// ============================================================================
// GeoJSON Reader
// ============================================================================

static const char* kSimpleGeoJSON = R"({
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "geometry": {
        "type": "Point",
        "coordinates": [116.4074, 39.9042]
      },
      "properties": {
        "name": "Beijing",
        "population": 21540000,
        "capital": true
      }
    },
    {
      "type": "Feature",
      "geometry": {
        "type": "Point",
        "coordinates": [121.4737, 31.2304, 4.0]
      },
      "properties": {
        "name": "Shanghai",
        "population": 24870000,
        "capital": false
      }
    }
  ]
})";

TEST(GeoJSONReaderTest, ParseFeatureCollection) {
  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(kSimpleGeoJSON)));

  auto& fc = reader.getFeatures();
  EXPECT_EQ(fc.features.size(), 2u);

  // First feature: Beijing
  auto& f0 = fc.features[0];
  EXPECT_EQ(f0.geometry.type, GeometryType::Point);
  EXPECT_NEAR(f0.geometry.point.x, 116.4074, 1e-6);
  EXPECT_NEAR(f0.geometry.point.y, 39.9042, 1e-6);
  EXPECT_FALSE(f0.geometry.point.has_z);

  auto name = std::get<std::string>(f0.properties.at("name"));
  EXPECT_EQ(name, "Beijing");
  auto pop = std::get<int64_t>(f0.properties.at("population"));
  EXPECT_EQ(pop, 21540000);
  auto capital = std::get<bool>(f0.properties.at("capital"));
  EXPECT_TRUE(capital);

  // Second feature: Shanghai (with Z)
  auto& f1 = fc.features[1];
  EXPECT_TRUE(f1.geometry.point.has_z);
  EXPECT_NEAR(f1.geometry.point.z, 4.0, 1e-6);
}

TEST(GeoJSONReaderTest, ParseLineString) {
  const char* geojson = R"({
    "type": "Feature",
    "geometry": {
      "type": "LineString",
      "coordinates": [[0,0],[1,1],[2,0]]
    },
    "properties": {"length": 2.83}
  })";

  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  auto& fc = reader.getFeatures();
  EXPECT_EQ(fc.features.size(), 1u);
  EXPECT_EQ(fc.features[0].geometry.type, GeometryType::LineString);
  EXPECT_EQ(fc.features[0].geometry.points.size(), 3u);
}

TEST(GeoJSONReaderTest, ParsePolygon) {
  const char* geojson = R"({
    "type": "Feature",
    "geometry": {
      "type": "Polygon",
      "coordinates": [
        [[0,0],[10,0],[10,10],[0,10],[0,0]],
        [[2,2],[8,2],[8,8],[2,8],[2,2]]
      ]
    },
    "properties": {}
  })";

  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  auto& geom = reader.getFeatures().features[0].geometry;
  EXPECT_EQ(geom.type, GeometryType::Polygon);
  EXPECT_EQ(geom.rings.size(), 2u);  // Outer + hole
  EXPECT_EQ(geom.rings[0].size(), 5u);
  EXPECT_EQ(geom.rings[1].size(), 5u);
}

TEST(GeoJSONReaderTest, ParseBareGeometry) {
  const char* geojson = R"({
    "type": "Point",
    "coordinates": [1.0, 2.0]
  })";

  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  EXPECT_EQ(reader.getFeatures().features.size(), 1u);
  EXPECT_EQ(reader.getFeatures().features[0].geometry.type, GeometryType::Point);
}

TEST(GeoJSONReaderTest, ParseMultiPolygon) {
  const char* geojson = R"({
    "type": "Feature",
    "geometry": {
      "type": "MultiPolygon",
      "coordinates": [
        [[[0,0],[1,0],[1,1],[0,1],[0,0]]],
        [[[2,2],[3,2],[3,3],[2,3],[2,2]]]
      ]
    },
    "properties": {}
  })";

  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  auto& geom = reader.getFeatures().features[0].geometry;
  EXPECT_EQ(geom.type, GeometryType::MultiPolygon);
  EXPECT_EQ(geom.rings.size(), 2u);
}

TEST(GeoJSONReaderTest, InvalidJSON) {
  GeoJSONReader reader;
  EXPECT_FALSE(reader.parse(std::string("{invalid json}")));
  EXPECT_FALSE(reader.getError().empty());
}

// ============================================================================
// GeoJSON Writer Roundtrip
// ============================================================================

TEST(GeoJSONWriterTest, Roundtrip) {
  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(kSimpleGeoJSON)));

  std::string output = writeGeoJSON(reader.getFeatures());
  EXPECT_FALSE(output.empty());

  // Parse the output back
  GeoJSONReader reader2;
  ASSERT_TRUE(reader2.parse(output));

  auto& fc1 = reader.getFeatures();
  auto& fc2 = reader2.getFeatures();
  EXPECT_EQ(fc1.features.size(), fc2.features.size());

  // Check coordinates survived roundtrip
  EXPECT_NEAR(fc2.features[0].geometry.point.x, 116.4074, 1e-6);
  EXPECT_NEAR(fc2.features[0].geometry.point.y, 39.9042, 1e-6);
}

// ============================================================================
// FeatureCollection Bounds
// ============================================================================

TEST(FeatureCollectionTest, BoundsFromGeoJSON) {
  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(kSimpleGeoJSON)));

  double x0, y0, x1, y1;
  reader.getFeatures().getBounds(x0, y0, x1, y1);

  EXPECT_NEAR(x0, 116.4074, 1e-4);
  EXPECT_NEAR(y0, 31.2304, 1e-4);
  EXPECT_NEAR(x1, 121.4737, 1e-4);
  EXPECT_NEAR(y1, 39.9042, 1e-4);
}

// ============================================================================
// Shapefile Reader (synthetic binary data)
// ============================================================================

static std::vector<uint8_t> buildTestSHP_Point() {
  // Minimal SHP file with 2 point records
  std::vector<uint8_t> shp(100 + (8 + 24) * 2, 0);
  auto* d = shp.data();

  // File header
  // File code = 9994 (big-endian)
  d[0] = 0x00;
  d[1] = 0x00;
  d[2] = 0x27;
  d[3] = 0x0A;
  // File length in 16-bit words (big-endian)
  int32_t file_len = static_cast<int32_t>(shp.size() / 2);
  d[24] = (file_len >> 24) & 0xFF;
  d[25] = (file_len >> 16) & 0xFF;
  d[26] = (file_len >> 8) & 0xFF;
  d[27] = file_len & 0xFF;
  // Shape type = 1 (Point, little-endian)
  d[32] = 1;
  // Bounding box (little-endian doubles)
  double bbox[] = {116.0, 31.0, 122.0, 40.0};
  std::memcpy(d + 36, bbox, 32);

  // Record 1
  size_t off = 100;
  // Record number (big-endian)
  d[off] = 0;
  d[off + 1] = 0;
  d[off + 2] = 0;
  d[off + 3] = 1;
  // Content length = 10 words = 20 bytes (big-endian)
  d[off + 4] = 0;
  d[off + 5] = 0;
  d[off + 6] = 0;
  d[off + 7] = 10;
  off += 8;
  // Shape type = 1
  d[off] = 1;
  d[off + 1] = 0;
  d[off + 2] = 0;
  d[off + 3] = 0;
  off += 4;
  double pt1[] = {116.4074, 39.9042};
  std::memcpy(d + off, pt1, 16);
  off += 16;

  // Record 2
  d[off] = 0;
  d[off + 1] = 0;
  d[off + 2] = 0;
  d[off + 3] = 2;
  d[off + 4] = 0;
  d[off + 5] = 0;
  d[off + 6] = 0;
  d[off + 7] = 10;
  off += 8;
  d[off] = 1;
  d[off + 1] = 0;
  d[off + 2] = 0;
  d[off + 3] = 0;
  off += 4;
  double pt2[] = {121.4737, 31.2304};
  std::memcpy(d + off, pt2, 16);

  return shp;
}

TEST(ShapefileReaderTest, ParsePointSHP) {
  auto shp_data = buildTestSHP_Point();

  ShapefileReader reader;
  ASSERT_TRUE(reader.openFromBuffers(shp_data, {}));

  EXPECT_EQ(reader.getShapeType(), ShapeType::Point);
  auto& fc = reader.getFeatures();
  EXPECT_EQ(fc.features.size(), 2u);

  EXPECT_EQ(fc.features[0].geometry.type, GeometryType::Point);
  EXPECT_NEAR(fc.features[0].geometry.point.x, 116.4074, 1e-4);
  EXPECT_NEAR(fc.features[0].geometry.point.y, 39.9042, 1e-4);

  EXPECT_NEAR(fc.features[1].geometry.point.x, 121.4737, 1e-4);
}

TEST(ShapefileReaderTest, FileBounds) {
  auto shp_data = buildTestSHP_Point();
  ShapefileReader reader;
  reader.openFromBuffers(shp_data, {});

  double x0, y0, x1, y1;
  reader.getFileBounds(x0, y0, x1, y1);
  EXPECT_NEAR(x0, 116.0, 1e-1);
  EXPECT_NEAR(y1, 40.0, 1e-1);
}

TEST(ShapefileReaderTest, InvalidSHP) {
  std::vector<uint8_t> bad_data = {0, 1, 2, 3};
  ShapefileReader reader;
  EXPECT_FALSE(reader.openFromBuffers(bad_data, {}));
  EXPECT_FALSE(reader.getError().empty());
}

TEST(ShapefileReaderTest, EmptySHP) {
  ShapefileReader reader;
  std::vector<uint8_t> empty;
  EXPECT_FALSE(reader.openFromBuffers(empty, {}));
}

// ============================================================================
// 3D Tiles Reader
// ============================================================================

static const char* kTestTileset = R"({
  "asset": { "version": "1.0" },
  "geometricError": 500.0,
  "root": {
    "boundingVolume": {
      "region": [-1.3197, 0.6988, -1.3196, 0.6989, 0, 88]
    },
    "geometricError": 100.0,
    "refine": "REPLACE",
    "content": { "uri": "root.b3dm" },
    "children": [
      {
        "boundingVolume": {
          "sphere": [0, 0, 0, 50]
        },
        "geometricError": 10.0,
        "content": { "uri": "child1.b3dm" },
        "children": [
          {
            "boundingVolume": {
              "box": [0,0,0, 10,0,0, 0,10,0, 0,0,10]
            },
            "geometricError": 1.0,
            "content": { "uri": "leaf1.b3dm" }
          }
        ]
      },
      {
        "boundingVolume": {
          "sphere": [100, 0, 0, 30]
        },
        "geometricError": 5.0,
        "content": { "uri": "child2.b3dm" }
      }
    ]
  }
})";

TEST(Tiles3DReaderTest, ParseTileset) {
  Tileset3DReader reader;
  ASSERT_TRUE(reader.parse(std::string(kTestTileset)));

  auto& info = reader.getInfo();
  EXPECT_EQ(info.version, "1.0");
  EXPECT_NEAR(info.geometric_error, 500.0, 1e-3);
  EXPECT_EQ(info.tile_count, 4u);
  EXPECT_EQ(info.max_depth, 2u);
}

TEST(Tiles3DReaderTest, RootTile) {
  Tileset3DReader reader;
  reader.parse(std::string(kTestTileset));

  auto* root = reader.getRoot();
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->bounding_volume.type, BoundingVolume::Type::Region);
  EXPECT_EQ(root->refine, Refinement::Replace);
  EXPECT_EQ(root->content_uri, "root.b3dm");
  EXPECT_EQ(root->children.size(), 2u);
}

TEST(Tiles3DReaderTest, ChildTiles) {
  Tileset3DReader reader;
  reader.parse(std::string(kTestTileset));

  auto* root = reader.getRoot();
  auto& child1 = root->children[0];
  EXPECT_EQ(child1->bounding_volume.type, BoundingVolume::Type::Sphere);
  EXPECT_NEAR(child1->bounding_volume.sphere.radius, 50.0, 1e-3);
  EXPECT_EQ(child1->content_uri, "child1.b3dm");
  EXPECT_EQ(child1->children.size(), 1u);

  // Leaf has box bounding volume
  auto& leaf = child1->children[0];
  EXPECT_EQ(leaf->bounding_volume.type, BoundingVolume::Type::Box);
  EXPECT_EQ(leaf->children.size(), 0u);
}

TEST(Tiles3DReaderTest, ContentURIs) {
  Tileset3DReader reader;
  reader.parse(std::string(kTestTileset));

  auto uris = reader.getAllContentURIs();
  EXPECT_EQ(uris.size(), 4u);
  EXPECT_EQ(uris[0], "root.b3dm");
  // DFS order
  EXPECT_EQ(uris[1], "child1.b3dm");
  EXPECT_EQ(uris[2], "leaf1.b3dm");
  EXPECT_EQ(uris[3], "child2.b3dm");
}

TEST(Tiles3DReaderTest, InvalidJSON) {
  Tileset3DReader reader;
  EXPECT_FALSE(reader.parse(std::string("{bad}")));
  EXPECT_FALSE(reader.getError().empty());
}

TEST(Tiles3DReaderTest, NoRootTile) {
  Tileset3DReader reader;
  EXPECT_FALSE(reader.parse(std::string(R"({"asset":{"version":"1.0"}})")));
}

TEST(Tiles3DReaderTest, BoundingVolumeTypes) {
  Tileset3DReader reader;
  reader.parse(std::string(kTestTileset));

  auto* root = reader.getRoot();
  // Root: region
  EXPECT_EQ(root->bounding_volume.type, BoundingVolume::Type::Region);
  // Child1: sphere
  EXPECT_EQ(root->children[0]->bounding_volume.type, BoundingVolume::Type::Sphere);
  // Leaf: box
  EXPECT_EQ(root->children[0]->children[0]->bounding_volume.type, BoundingVolume::Type::Box);
}

// ============================================================================
// GeometryCollection
// ============================================================================

TEST(GeoJSONReaderTest, GeometryCollection) {
  const char* geojson = R"({
    "type": "Feature",
    "geometry": {
      "type": "GeometryCollection",
      "geometries": [
        {"type": "Point", "coordinates": [0, 0]},
        {"type": "LineString", "coordinates": [[0,0],[1,1]]}
      ]
    },
    "properties": {}
  })";

  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  auto& geom = reader.getFeatures().features[0].geometry;
  EXPECT_EQ(geom.type, GeometryType::GeometryCollection);
  EXPECT_EQ(geom.geometries.size(), 2u);
  EXPECT_EQ(geom.geometries[0].type, GeometryType::Point);
  EXPECT_EQ(geom.geometries[1].type, GeometryType::LineString);
  EXPECT_EQ(geom.getVertexCount(), 3u);  // 1 point + 2 linestring verts
}

// ============================================================================
// DLG semantic layers and WGS84 -> ENU
// ============================================================================

TEST(DlgLayerTest, ClassifiesAndFlattensGeoJsonFeatures) {
  const char* geojson = R"({
    "type":"FeatureCollection",
    "features":[
      {"type":"Feature","id":1,"properties":{"highway":"primary"},
       "geometry":{"type":"LineString","coordinates":[[114.59,22.74],[114.60,22.75]]}},
      {"type":"Feature","id":2,"properties":{"building":"industrial"},
       "geometry":{"type":"Polygon","coordinates":[[[114.60,22.74],[114.601,22.74],
                                                         [114.601,22.741],[114.60,22.74]]]}},
      {"type":"Feature","id":3,"properties":{"waterway":"stream"},
       "geometry":{"type":"LineString","coordinates":[[114.61,22.75],[114.62,22.76]]}},
      {"type":"Feature","id":4,"properties":{"man_made":"pipeline"},
       "geometry":{"type":"LineString","coordinates":[[114.58,22.73],[114.59,22.74]]}}
    ]
  })";
  GeoJSONReader reader;
  ASSERT_TRUE(reader.parse(std::string(geojson)));
  const DlgDataset dataset = buildDlgDataset(reader.getFeatures(), "memory.geojson");

  ASSERT_TRUE(dataset.valid) << dataset.error;
  EXPECT_EQ(dataset.source_feature_count, 4u);
  EXPECT_EQ(dataset.polylines.size(), 4u);
  EXPECT_EQ(dataset.vertex_count, 9u);
  EXPECT_EQ(dataset.class_counts[static_cast<std::size_t>(DlgFeatureClass::Road)], 1u);
  EXPECT_EQ(dataset.class_counts[static_cast<std::size_t>(DlgFeatureClass::Building)], 1u);
  EXPECT_EQ(dataset.class_counts[static_cast<std::size_t>(DlgFeatureClass::Water)], 1u);
  EXPECT_EQ(dataset.class_counts[static_cast<std::size_t>(DlgFeatureClass::Pipeline)], 1u);
  EXPECT_TRUE(dataset.bounds.valid);
  EXPECT_NEAR(dataset.bounds.min_lon, 114.58, 1e-12);
  EXPECT_NEAR(dataset.bounds.max_lat, 22.76, 1e-12);
}

TEST(DlgLayerTest, ProjectsWgs84ToDoublePrecisionEnu) {
  FeatureCollection features;
  Feature feature;
  feature.fid = 7;
  feature.properties["highway"] = std::string("service");
  feature.geometry.type = GeometryType::LineString;
  feature.geometry.points = {{114.6, 22.75}, {114.601, 22.751}};
  features.features.push_back(feature);

  const DlgDataset dataset = buildDlgDataset(features, "memory.geojson");
  ASSERT_TRUE(dataset.valid);
  const auto anchor = hongtu::coordinate::Geodetic::fromDegrees(22.75, 114.6, 0.0);
  const DlgLocalDataset local = projectDlgToEnu(dataset, anchor);

  ASSERT_TRUE(local.valid) << local.error;
  ASSERT_EQ(local.polylines.size(), 1u);
  ASSERT_EQ(local.polylines[0].enu_points.size(), 2u);
  EXPECT_NEAR(local.polylines[0].enu_points[0].x, 0.0, 1e-6);
  EXPECT_NEAR(local.polylines[0].enu_points[0].y, 0.0, 1e-6);
  EXPECT_NEAR(local.polylines[0].enu_points[1].x, 102.7, 1.0);
  EXPECT_NEAR(local.polylines[0].enu_points[1].y, 110.8, 1.0);
  EXPECT_LT(local.maximum_roundtrip_error_m, 1e-5);
}

TEST(DlgLayerTest, RejectsOutOfRangeWgs84Coordinates) {
  FeatureCollection features;
  Feature feature;
  feature.geometry.type = GeometryType::LineString;
  feature.geometry.points = {{114.6, 22.75}, {214.6, 22.75}};
  features.features.push_back(feature);
  const DlgDataset dataset = buildDlgDataset(features, "invalid.geojson");
  EXPECT_FALSE(dataset.valid);
  EXPECT_FALSE(dataset.error.empty());
  EXPECT_TRUE(dataset.polylines.empty());
}
