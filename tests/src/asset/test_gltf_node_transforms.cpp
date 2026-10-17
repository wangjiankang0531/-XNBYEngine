// GltfLoader 节点变换烘焙 —— 城市级多节点 glb 的摆放正确性。
//
// 构造最小 .gltf:一个三角形 mesh,被两个节点引用(平移 (5,0,0) 与
// 缩放 2 + 平移 (0,0,7)),断言展平后两份网格的顶点各自落位。
#include <hongtu/asset/GltfLoader.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

// 3 顶点 (0,0,0)(1,0,0)(0,1,0) float32 + 3 个 uint16 索引(补齐 4 字节)。
// base64(36B pos || 8B idx) — 预先手算固定串,测试免依赖编码库。
const char* kMinimalGltf = R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"mesh": 0, "translation": [5.0, 0.0, 0.0]},
    {"mesh": 0, "translation": [0.0, 0.0, 7.0], "scale": [2.0, 2.0, 2.0]}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 44,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA="
  }]
})JSON";

TEST(GltfNodeTransforms, MultiNodeReferencesBakeAndClone) {
  const std::string path = "/tmp/ht_test_node_xform.gltf";
  {
    std::ofstream f(path);
    f << kMinimalGltf;
  }
  hongtu::asset::GltfLoader loader;
  auto res = loader.load(path);
  ASSERT_TRUE(res.has_value()) << loader.lastError();

  // 1 个 mesh × 2 个引用节点 → 展平 1 份 + 克隆 1 份。
  ASSERT_EQ(res->meshes.size(), 2u);
  const auto& a = res->meshes[0];  // 就地烘焙:第一个引用 (T 5,0,0)
  const auto& b = res->meshes[1];  // 克隆:第二个引用 (T 0,0,7 · S 2)
  ASSERT_EQ(a.positions.size(), 3u);
  ASSERT_EQ(b.positions.size(), 3u);

  EXPECT_NEAR(a.positions[0].x, 5.0f, 1e-5f);
  EXPECT_NEAR(a.positions[1].x, 6.0f, 1e-5f);   // (1,0,0)+T
  EXPECT_NEAR(a.positions[2].y, 1.0f, 1e-5f);

  EXPECT_NEAR(b.positions[0].z, 7.0f, 1e-5f);   // (0,0,0)·S+T
  EXPECT_NEAR(b.positions[1].x, 2.0f, 1e-5f);   // (1,0,0)×2
  EXPECT_NEAR(b.positions[2].y, 2.0f, 1e-5f);   // (0,1,0)×2

  std::remove(path.c_str());
}

TEST(GltfNodeTransforms, SynthesizesNormalsWhenAttributeMissing) {
  // 程序化/导出网格常缺 NORMAL(trimesh process=False),空法线 → PBR 全黑面。
  // 加载器应从几何合成法线。三角 (0,0,0)(1,0,0)(0,1,0) 绕序 0,1,2 →
  // cross((1,0,0),(0,1,0)) = (0,0,1)。单位化后每个顶点法线 = +Z。
  const std::string path = "/tmp/ht_test_synth_normal.gltf";
  {
    std::ofstream f(path);
    f << kMinimalGltf;  // 该 glTF 只有 POSITION,无 NORMAL
  }
  hongtu::asset::GltfLoader loader;
  auto res = loader.load(path);
  ASSERT_TRUE(res.has_value()) << loader.lastError();
  ASSERT_FALSE(res->meshes.empty());
  const auto& m = res->meshes[0];  // 节点仅平移,法线方向不变
  ASSERT_EQ(m.normals.size(), m.positions.size());
  for (const auto& n : m.normals) {
    EXPECT_NEAR(n.x, 0.0f, 1e-5f);
    EXPECT_NEAR(n.y, 0.0f, 1e-5f);
    EXPECT_NEAR(n.z, 1.0f, 1e-5f);
  }
  std::remove(path.c_str());
}

TEST(GltfNodeTransforms, IdentityNodesUntouched) {
  // 单节点恒等变换:行为与烘焙前完全一致(树/灌木等单物体资产回归)。
  const std::string path = "/tmp/ht_test_node_ident.gltf";
  {
    std::string j = kMinimalGltf;
    const std::string from = R"("nodes": [
    {"mesh": 0, "translation": [5.0, 0.0, 0.0]},
    {"mesh": 0, "translation": [0.0, 0.0, 7.0], "scale": [2.0, 2.0, 2.0]}
  ])";
    const std::string to = R"("nodes": [{"mesh": 0}])";
    auto k = j.find(from);
    ASSERT_NE(k, std::string::npos);
    j.replace(k, from.size(), to);
    k = j.find(R"("nodes": [0, 1])");
    ASSERT_NE(k, std::string::npos);
    j.replace(k, std::string(R"("nodes": [0, 1])").size(), R"("nodes": [0])");
    std::ofstream f(path);
    f << j;
  }
  hongtu::asset::GltfLoader loader;
  auto res = loader.load(path);
  ASSERT_TRUE(res.has_value()) << loader.lastError();
  ASSERT_EQ(res->meshes.size(), 1u);
  EXPECT_NEAR(res->meshes[0].positions[1].x, 1.0f, 1e-6f);
  EXPECT_NEAR(res->meshes[0].positions[2].y, 1.0f, 1e-6f);
  std::remove(path.c_str());
}

const char* kEmissiveGltf = R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1,
                               "material": 0}]}],
  "materials": [{
    "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                              "metallicFactor": 0.0},
    "emissiveFactor": [1.0, 0.5, 0.25],
    "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2.0}}
  }],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "buffers": [{
    "byteLength": 44,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA="
  }]
})JSON";

}  // namespace

TEST(GltfEmissive, ParsesFactorAndStrength) {
  // 与本文件其余测试一致:写 /tmp 临时文件 → load → remove。
  const std::string path = "/tmp/ht_test_emissive.gltf";
  {
    std::ofstream f(path);
    f << kEmissiveGltf;
  }
  hongtu::asset::GltfLoader loader;
  auto md = loader.load(path);
  ASSERT_TRUE(md.has_value()) << loader.lastError();
  ASSERT_EQ(md->materials.size(), 1u);
  const auto& m = md->materials[0];
  // emissiveFactor × KHR_materials_emissive_strength(2.0)
  EXPECT_FLOAT_EQ(m.emissive_factor.r, 2.0f);
  EXPECT_FLOAT_EQ(m.emissive_factor.g, 1.0f);
  EXPECT_FLOAT_EQ(m.emissive_factor.b, 0.5f);
  EXPECT_EQ(m.emissive_texture, -1);
  std::remove(path.c_str());
}

// findNodeWorldTransform — 读取 glb 里命名挂点(如喷水炮口 `nozzle`)的世界变换。
// 纯遍历,只依赖 nodes + root_nodes,故手搓 ModelData 直接验证组合与未命中。
TEST(GltfNodeLookup, FindsNamedNodeWorldTransformComposed) {
  using hongtu::asset::ModelData;
  using hongtu::asset::NodeData;
  ModelData md;
  // 根 base:平移 (10,0,0);子 nozzle:再平移 (2,0,3) + 绕 Z 旋 90°。
  NodeData base;
  base.name = "base";
  base.translation = {10.0, 0.0, 0.0};
  base.children = {1};
  NodeData nozzle;
  nozzle.name = "nozzle";
  nozzle.translation = {2.0, 0.0, 3.0};
  nozzle.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
  md.nodes = {base, nozzle};
  md.root_nodes = {0};

  auto w = md.findNodeWorldTransform("nozzle");
  ASSERT_TRUE(w.has_value());
  // 世界平移 = base.t + nozzle.t = (12, 0, 3)
  const glm::vec3 origin = glm::vec3((*w)[3]);
  EXPECT_NEAR(origin.x, 12.0f, 1e-4f);
  EXPECT_NEAR(origin.y, 0.0f, 1e-4f);
  EXPECT_NEAR(origin.z, 3.0f, 1e-4f);
  // 节点 +X 轴经绕 Z 90° → 世界 +Y(喷射方向取 +X 轴)。
  const glm::vec3 fwd = glm::normalize(glm::vec3((*w) * glm::vec4(1, 0, 0, 0)));
  EXPECT_NEAR(fwd.x, 0.0f, 1e-4f);
  EXPECT_NEAR(fwd.y, 1.0f, 1e-4f);
  EXPECT_NEAR(fwd.z, 0.0f, 1e-4f);

  EXPECT_FALSE(md.findNodeWorldTransform("missing").has_value());
}
