// Tests for the glTF / glb loader. Verifies:
//   1. Both container formats (.gltf ASCII + .glb binary) load and yield
//      equivalent ModelData.
//   2. Vertex / index counts match the source.
//   3. Container auto-detection works regardless of extension.
//
// Test fixtures are generated at runtime via tinygltf's writer so the tests
// are self-contained — no checked-in asset files. The writer symbols are
// already linked into ht_asset (which has TINYGLTF_IMPLEMENTATION defined in
// gltf_loader.cpp), so this TU only needs the declarations.

#include <gtest/gtest.h>
#include <hongtu/asset/GltfLoader.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

// No TINYGLTF_IMPLEMENTATION here — symbols come from ht_asset's gltf_loader.cpp.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <tiny_gltf.h>

using namespace hongtu::asset;

namespace {

/// Construct an in-memory tinygltf::Model representing a unit-cube
/// (8 vertices, 36 indices, no normals/UVs) so the loader has something
/// concrete to chew on. Returns by value — caller owns the Model.
tinygltf::Model buildCubeModel() {
  // 8 cube corners — packed as float32 little-endian.
  static const float positions[8 * 3] = {
    -0.5f, -0.5f, -0.5f,    0.5f, -0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,   -0.5f,  0.5f, -0.5f,
    -0.5f, -0.5f,  0.5f,    0.5f, -0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,   -0.5f,  0.5f,  0.5f,
  };
  // 12 triangles, two per face.
  static const uint16_t indices[36] = {
    0,1,2, 0,2,3,  4,6,5, 4,7,6,
    0,4,5, 0,5,1,  2,6,7, 2,7,3,
    1,5,6, 1,6,2,  0,3,7, 0,7,4,
  };

  tinygltf::Model m;
  m.asset.version = "2.0";
  m.asset.generator = "hongtu test fixture";

  // Single buffer holds positions + indices back-to-back.
  tinygltf::Buffer buf;
  buf.data.resize(sizeof(positions) + sizeof(indices));
  std::memcpy(buf.data.data(),                       positions, sizeof(positions));
  std::memcpy(buf.data.data() + sizeof(positions),   indices,   sizeof(indices));
  m.buffers.push_back(buf);

  tinygltf::BufferView vbv;
  vbv.buffer = 0;
  vbv.byteOffset = 0;
  vbv.byteLength = sizeof(positions);
  vbv.target = TINYGLTF_TARGET_ARRAY_BUFFER;
  m.bufferViews.push_back(vbv);

  tinygltf::BufferView ibv;
  ibv.buffer = 0;
  ibv.byteOffset = sizeof(positions);
  ibv.byteLength = sizeof(indices);
  ibv.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
  m.bufferViews.push_back(ibv);

  tinygltf::Accessor pa;
  pa.bufferView = 0;
  pa.byteOffset = 0;
  pa.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
  pa.count = 8;
  pa.type  = TINYGLTF_TYPE_VEC3;
  pa.minValues = {-0.5, -0.5, -0.5};
  pa.maxValues = { 0.5,  0.5,  0.5};
  m.accessors.push_back(pa);

  tinygltf::Accessor ia;
  ia.bufferView = 1;
  ia.byteOffset = 0;
  ia.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
  ia.count = 36;
  ia.type  = TINYGLTF_TYPE_SCALAR;
  m.accessors.push_back(ia);

  tinygltf::Primitive prim;
  prim.attributes["POSITION"] = 0;
  prim.indices = 1;
  prim.material = -1;
  prim.mode = TINYGLTF_MODE_TRIANGLES;
  tinygltf::Mesh mesh;
  mesh.primitives.push_back(prim);
  m.meshes.push_back(mesh);

  tinygltf::Node node;
  node.mesh = 0;
  node.translation = {1.0, 2.0, 3.0};
  m.nodes.push_back(node);

  tinygltf::Scene scene;
  scene.nodes = {0};
  m.scenes.push_back(scene);
  m.defaultScene = 0;
  return m;
}

/// Write the cube model to disk in the requested container.
std::filesystem::path writeCubeFixture(bool binary, const std::string& tag) {
  tinygltf::Model m = buildCubeModel();
  auto dir = std::filesystem::temp_directory_path() / "hongtu_gltf_tests";
  std::filesystem::create_directories(dir);
  std::filesystem::path out = dir / (tag + (binary ? ".glb" : ".gltf"));

  tinygltf::TinyGLTF ctx;
  bool ok = ctx.WriteGltfSceneToFile(
      &m, out.string(),
      /*embedImages=*/   true,
      /*embedBuffers=*/  true,
      /*prettyPrint=*/   true,
      /*writeBinary=*/   binary);
  EXPECT_TRUE(ok) << "failed to write " << out;
  return out;
}

void expectMatchesCube(const ModelData& md) {
  ASSERT_EQ(md.meshes.size(), 1u);
  const auto& mesh = md.meshes[0];
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.indices.size(),  36u);
  // Bounds: ±0.5 + 节点平移 (1,2,3) —— 加载器现在烘焙节点变换
  // (城市级 glb 修复;此前这条测试固化的是"丢变换"的旧 bug 行为)。
  for (const auto& p : mesh.positions) {
    EXPECT_GE(p.x, 0.5f); EXPECT_LE(p.x, 1.5f);
    EXPECT_GE(p.y, 1.5f); EXPECT_LE(p.y, 2.5f);
    EXPECT_GE(p.z, 2.5f); EXPECT_LE(p.z, 3.5f);
  }
  // Index 0 must be vertex 0 (first triangle), index 1 → vertex 1, etc.
  EXPECT_EQ(mesh.indices[0], 0u);
  EXPECT_EQ(mesh.indices[1], 1u);
  EXPECT_EQ(mesh.indices[2], 2u);

  // Node has translation (1, 2, 3).
  ASSERT_EQ(md.nodes.size(), 1u);
  EXPECT_DOUBLE_EQ(md.nodes[0].translation.x, 1.0);
  EXPECT_DOUBLE_EQ(md.nodes[0].translation.y, 2.0);
  EXPECT_DOUBLE_EQ(md.nodes[0].translation.z, 3.0);
  EXPECT_EQ(md.nodes[0].mesh, 0);

  // Default scene root → node 0.
  ASSERT_EQ(md.root_nodes.size(), 1u);
  EXPECT_EQ(md.root_nodes[0], 0);
}

}  // namespace

TEST(GltfLoaderTest, LoadsAsciiGltf) {
  auto path = writeCubeFixture(/*binary=*/false, "ascii_cube");
  GltfLoader loader;
  auto md = loader.load(path.string());
  ASSERT_TRUE(md.has_value()) << loader.lastError();
  expectMatchesCube(*md);
}

TEST(GltfLoaderTest, LoadsBinaryGlb) {
  auto path = writeCubeFixture(/*binary=*/true, "binary_cube");
  GltfLoader loader;
  auto md = loader.load(path.string());
  ASSERT_TRUE(md.has_value()) << loader.lastError();
  expectMatchesCube(*md);
}

TEST(GltfLoaderTest, BothContainersYieldIdenticalGeometry) {
  // Same source Model → write as both .gltf and .glb → both load to the
  // same MeshData. This is the contract that lets the renderer treat the
  // two container formats interchangeably.
  auto ascii_path  = writeCubeFixture(false, "ident_ascii");
  auto binary_path = writeCubeFixture(true,  "ident_binary");

  GltfLoader loader;
  auto a = loader.load(ascii_path.string());
  auto b = loader.load(binary_path.string());
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_EQ(a->meshes.size(), b->meshes.size());

  const auto& ma = a->meshes[0];
  const auto& mb = b->meshes[0];
  ASSERT_EQ(ma.positions.size(), mb.positions.size());
  ASSERT_EQ(ma.indices.size(),   mb.indices.size());
  for (size_t i = 0; i < ma.positions.size(); ++i) {
    EXPECT_FLOAT_EQ(ma.positions[i].x, mb.positions[i].x);
    EXPECT_FLOAT_EQ(ma.positions[i].y, mb.positions[i].y);
    EXPECT_FLOAT_EQ(ma.positions[i].z, mb.positions[i].z);
  }
  for (size_t i = 0; i < ma.indices.size(); ++i) {
    EXPECT_EQ(ma.indices[i], mb.indices[i]);
  }
}

TEST(GltfLoaderTest, AutoDetectsBinaryWhenExtensionIsAmbiguous) {
  // Write a glb file but with .bin extension — loader should sniff the magic
  // and still load it correctly.
  auto path = writeCubeFixture(true, "weird_ext");
  auto renamed = path;
  renamed.replace_extension(".bin");
  std::filesystem::rename(path, renamed);

  GltfLoader loader;
  auto md = loader.load(renamed.string());
  ASSERT_TRUE(md.has_value()) << loader.lastError();
  expectMatchesCube(*md);
}

TEST(GltfLoaderTest, MissingFileReturnsNullopt) {
  GltfLoader loader;
  auto md = loader.load("/nonexistent/path/missing.gltf");
  EXPECT_FALSE(md.has_value());
  EXPECT_FALSE(loader.lastError().empty());
}
