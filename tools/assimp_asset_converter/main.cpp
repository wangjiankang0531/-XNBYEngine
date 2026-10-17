#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
  std::filesystem::path input;
  std::filesystem::path output;
  float unitScale = 1.0F;
  bool stripTextures = false;
  bool centerGround = false;
  bool listMeshes = false;
  std::string includeName;
  std::string includeSourceName;
};

std::string pathForLibrary(const std::filesystem::path& path);

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

bool parseFloat(std::string_view text, float& value) {
  try {
    std::size_t parsed = 0;
    value = std::stof(std::string(text), &parsed);
    return parsed == text.size() && value > 0.0F;
  } catch (const std::exception&) {
    return false;
  }
}

bool parseArgs(const int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--strip-textures") {
      options.stripTextures = true;
    } else if (argument == "--center-ground") {
      options.centerGround = true;
    } else if (argument == "--list-meshes") {
      options.listMeshes = true;
    } else if (argument.starts_with("--input=")) {
      options.input = std::filesystem::path(argument.substr(8));
    } else if (argument.starts_with("--output=")) {
      options.output = std::filesystem::path(argument.substr(9));
    } else if (argument.starts_with("--scale=")) {
      if (!parseFloat(std::string_view(argument).substr(8), options.unitScale)) {
        return false;
      }
    } else if (argument.starts_with("--include-name=")) {
      options.includeName = argument.substr(15);
    } else if (argument.starts_with("--include-source-name=")) {
      options.includeSourceName = argument.substr(22);
    } else {
      return false;
    }
  }
  return !options.input.empty() && (options.listMeshes || !options.output.empty());
}

#ifdef _WIN32
bool parseArgs(const int argc, wchar_t** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if (argument == L"--strip-textures") {
      options.stripTextures = true;
    } else if (argument == L"--center-ground") {
      options.centerGround = true;
    } else if (argument == L"--list-meshes") {
      options.listMeshes = true;
    } else if (argument.starts_with(L"--input=")) {
      options.input = std::filesystem::path(argument.substr(8));
    } else if (argument.starts_with(L"--output=")) {
      options.output = std::filesystem::path(argument.substr(9));
    } else if (argument.starts_with(L"--scale=")) {
      try {
        std::size_t parsed = 0;
        options.unitScale = std::stof(argument.substr(8), &parsed);
        if (parsed != argument.size() - 8 || options.unitScale <= 0.0F) {
          return false;
        }
      } catch (const std::exception&) {
        return false;
      }
    } else if (argument.starts_with(L"--include-name=")) {
      options.includeName = pathForLibrary(std::filesystem::path(argument.substr(15)));
    } else if (argument.starts_with(L"--include-source-name=")) {
      options.includeSourceName = pathForLibrary(std::filesystem::path(argument.substr(22)));
    } else {
      return false;
    }
  }
  return !options.input.empty() && (options.listMeshes || !options.output.empty());
}
#endif

std::string pathForLibrary(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

struct SceneStats {
  std::uint64_t meshes = 0;
  std::uint64_t materials = 0;
  std::uint64_t vertices = 0;
  std::uint64_t triangles = 0;
  aiVector3D minimum{std::numeric_limits<float>::max()};
  aiVector3D maximum{std::numeric_limits<float>::lowest()};
};

SceneStats sceneStats(const aiScene& scene) {
  SceneStats stats;
  stats.meshes = scene.mNumMeshes;
  stats.materials = scene.mNumMaterials;
  for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
    const aiMesh& mesh = *scene.mMeshes[meshIndex];
    stats.vertices += mesh.mNumVertices;
    stats.triangles += mesh.mNumFaces;
    for (unsigned int vertexIndex = 0; vertexIndex < mesh.mNumVertices; ++vertexIndex) {
      const aiVector3D& position = mesh.mVertices[vertexIndex];
      stats.minimum.x = std::min(stats.minimum.x, position.x);
      stats.minimum.y = std::min(stats.minimum.y, position.y);
      stats.minimum.z = std::min(stats.minimum.z, position.z);
      stats.maximum.x = std::max(stats.maximum.x, position.x);
      stats.maximum.y = std::max(stats.maximum.y, position.y);
      stats.maximum.z = std::max(stats.maximum.z, position.z);
    }
  }
  return stats;
}

void centerAndGround(aiScene& scene) {
  const SceneStats source = sceneStats(scene);
  const aiVector3D offset{-(source.minimum.x + source.maximum.x) * 0.5F,
                          -(source.minimum.y + source.maximum.y) * 0.5F,
                          -source.minimum.z};
  for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
    aiMesh& mesh = *scene.mMeshes[meshIndex];
    for (unsigned int vertexIndex = 0; vertexIndex < mesh.mNumVertices; ++vertexIndex) {
      mesh.mVertices[vertexIndex] += offset;
    }
  }
}

void remapNodeMeshes(aiNode& node, const std::vector<int>& mapping) {
  std::vector<unsigned int> remapped;
  remapped.reserve(node.mNumMeshes);
  for (unsigned int index = 0; index < node.mNumMeshes; ++index) {
    const unsigned int oldIndex = node.mMeshes[index];
    if (oldIndex < mapping.size() && mapping[oldIndex] >= 0) {
      remapped.push_back(static_cast<unsigned int>(mapping[oldIndex]));
    }
  }
  if (!remapped.empty()) {
    auto* meshIndices = new unsigned int[remapped.size()];
    std::copy(remapped.begin(), remapped.end(), meshIndices);
    node.mMeshes = meshIndices;
    node.mNumMeshes = static_cast<unsigned int>(remapped.size());
  } else {
    node.mMeshes = nullptr;
    node.mNumMeshes = 0;
  }
  for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex) {
    remapNodeMeshes(*node.mChildren[childIndex], mapping);
  }
}

bool filterMeshesByName(aiScene& scene, const std::string& includeName) {
  std::vector<int> mapping(scene.mNumMeshes, -1);
  std::vector<aiMesh*> selected;
  for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
    aiMesh* mesh = scene.mMeshes[meshIndex];
    if (std::string(mesh->mName.C_Str()).find(includeName) == std::string::npos) {
      continue;
    }
    mapping[meshIndex] = static_cast<int>(selected.size());
    selected.push_back(mesh);
  }
  if (selected.empty()) {
    return false;
  }
  auto** meshes = new aiMesh*[selected.size()];
  std::copy(selected.begin(), selected.end(), meshes);
  scene.mMeshes = meshes;
  scene.mNumMeshes = static_cast<unsigned int>(selected.size());
  remapNodeMeshes(*scene.mRootNode, mapping);
  return true;
}

void stripTextureReferences(aiScene& scene) {
  for (unsigned int materialIndex = 0; materialIndex < scene.mNumMaterials; ++materialIndex) {
    aiMaterial& material = *scene.mMaterials[materialIndex];
    for (int textureType = static_cast<int>(aiTextureType_DIFFUSE);
         textureType <= static_cast<int>(aiTextureType_UNKNOWN); ++textureType) {
      const auto type = static_cast<aiTextureType>(textureType);
      while (material.GetTextureCount(type) > 0) {
        material.RemoveProperty(AI_MATKEY_TEXTURE(type, 0));
      }
    }
  }
}

void printStats(const SceneStats& stats, const float unitScale) {
  std::cout << "meshes=" << stats.meshes << " materials=" << stats.materials << " vertices=" << stats.vertices
            << " triangles=" << stats.triangles << " unit_scale=" << unitScale << '\n';
  std::cout << "bounds_m=[" << stats.minimum.x << ',' << stats.minimum.y << ',' << stats.minimum.z << "]..["
            << stats.maximum.x << ',' << stats.maximum.y << ',' << stats.maximum.z << "]\n";
}

void printMeshList(const aiScene& scene) {
  for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
    const aiMesh& mesh = *scene.mMeshes[meshIndex];
    aiVector3D minimum{std::numeric_limits<float>::max()};
    aiVector3D maximum{std::numeric_limits<float>::lowest()};
    for (unsigned int vertexIndex = 0; vertexIndex < mesh.mNumVertices; ++vertexIndex) {
      const aiVector3D& position = mesh.mVertices[vertexIndex];
      minimum.x = std::min(minimum.x, position.x);
      minimum.y = std::min(minimum.y, position.y);
      minimum.z = std::min(minimum.z, position.z);
      maximum.x = std::max(maximum.x, position.x);
      maximum.y = std::max(maximum.y, position.y);
      maximum.z = std::max(maximum.z, position.z);
    }
    std::cout << "mesh[" << meshIndex << "] name=\"" << mesh.mName.C_Str() << "\" vertices=" << mesh.mNumVertices
              << " triangles=" << mesh.mNumFaces << " material=" << mesh.mMaterialIndex << " bounds=[" << minimum.x
              << ',' << minimum.y << ',' << minimum.z << "]..[" << maximum.x << ',' << maximum.y << ',' << maximum.z
              << "]\n";
  }
}

}  // namespace

template <typename Character>
int run(const int argc, Character** argv) {
  Options options;
  if (!parseArgs(argc, argv, options)) {
    std::cerr << "usage: assimp_asset_converter --input=SOURCE --output=DESTINATION.gltf "
                 "[--scale=0.01] [--strip-textures] [--center-ground] "
                 "[--include-name=TEXT | --include-source-name=TEXT]\n"
                 "       assimp_asset_converter --input=SOURCE --list-meshes [--scale=0.01]\n";
    return 1;
  }
  if (!std::filesystem::is_regular_file(options.input)) {
    std::cerr << "input file does not exist: " << options.input << '\n';
    return 2;
  }
  if (!options.listMeshes && lower(options.output.extension().string()) != ".gltf") {
    std::cerr << "output must use the .gltf extension\n";
    return 2;
  }

  std::error_code error;
  if (!options.listMeshes) {
    std::filesystem::create_directories(options.output.parent_path(), error);
    if (error) {
      std::cerr << "could not create output directory: " << error.message() << '\n';
      return 2;
    }
  }

  Assimp::Importer importer;
  importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, options.unitScale);
  constexpr unsigned int kBaseImportFlags =
      aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
      aiProcess_GenSmoothNormals | aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials |
      aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_SortByPType | aiProcess_GlobalScale |
      aiProcess_ValidateDataStructure;
  const bool inspectOrFilterSourceMeshes = options.listMeshes || !options.includeSourceName.empty();
  const unsigned int importFlags =
      kBaseImportFlags |
      (inspectOrFilterSourceMeshes ? 0u : (aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes));
  const aiScene* imported = importer.ReadFile(pathForLibrary(options.input), importFlags);
  if (imported == nullptr || imported->mRootNode == nullptr || imported->mNumMeshes == 0) {
    std::cerr << "Assimp import failed: " << importer.GetErrorString() << '\n';
    return 3;
  }

  aiScene* scene = const_cast<aiScene*>(imported);
  if (options.listMeshes) {
    printStats(sceneStats(*scene), options.unitScale);
    printMeshList(*scene);
    return 0;
  }
  if (!options.includeSourceName.empty()) {
    if (!filterMeshesByName(*scene, options.includeSourceName)) {
      std::cerr << "no source mesh name contains: " << options.includeSourceName << '\n';
      return 3;
    }
    imported = importer.ApplyPostProcessing(aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes |
                                            aiProcess_RemoveRedundantMaterials | aiProcess_ValidateDataStructure);
    if (imported == nullptr || imported->mNumMeshes == 0) {
      std::cerr << "Assimp source selection post-process failed: " << importer.GetErrorString() << '\n';
      return 3;
    }
    scene = const_cast<aiScene*>(imported);
  }
  if (!options.includeName.empty() && !filterMeshesByName(*scene, options.includeName)) {
    std::cerr << "no mesh name contains: " << options.includeName << '\n';
    return 3;
  }
  if (options.stripTextures) {
    stripTextureReferences(*scene);
  }
  if (options.centerGround) {
    centerAndGround(*scene);
  }
  const SceneStats stats = sceneStats(*scene);
  printStats(stats, options.unitScale);

  Assimp::Exporter exporter;
  const aiReturn result = exporter.Export(scene, "gltf2", pathForLibrary(options.output));
  if (result != aiReturn_SUCCESS) {
    std::cerr << "Assimp glTF export failed: " << exporter.GetErrorString() << '\n';
    return 4;
  }

  std::cout << "wrote=" << pathForLibrary(options.output) << '\n';
  return 0;
}

#ifdef _WIN32
int wmain(const int argc, wchar_t** argv) {
  return run(argc, argv);
}
#else
int main(const int argc, char** argv) {
  return run(argc, argv);
}
#endif
