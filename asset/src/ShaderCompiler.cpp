#include "hongtu/asset/ShaderCompiler.h"

#include "hongtu/asset/temporary_file_cleanup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <process.h>
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using hires_clock = std::chrono::high_resolution_clock;

namespace HongTu::Asset {
  namespace {

    fs::path resolveGlslcPath(const std::string& configuredPath) {
      std::vector<fs::path> candidates;
#ifdef _WIN32
      std::wstring module(32768, L'\0');
      const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
      if (length && length < module.size()) {
        module.resize(length);
        candidates.push_back(fs::path(module).parent_path() / "runtime" / "shaderc" / "glslc.exe");
      }
#endif
      if (!configuredPath.empty()) {
        fs::path configured = fs::u8path(configuredPath);
        candidates.push_back(configured);
        fs::path shadercDir = configured.parent_path();
        fs::path toolsDir = shadercDir.parent_path();
        fs::path tripletDir = toolsDir.parent_path();
        fs::path installedDir = tripletDir.parent_path();
        const std::string triplet = tripletDir.filename().string();

        if (!installedDir.empty() && !triplet.empty()) {
          candidates.push_back(installedDir / "vcpkg" / "pkgs" / ("shaderc_" + triplet) / "tools" / "shaderc" /
                               "glslc.exe");
          candidates.push_back(installedDir / "vcpkg" / "blds" / "shaderc" / (triplet + "-rel") / "glslc" /
                               "glslc.exe");
        }
      }

      for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
          return candidate;
        }
      }

      return "glslc";
    }

  } // namespace

  ShaderCompiler::ShaderCompiler() = default;

  fs::path ShaderCompiler::executablePath() {
#ifdef HT_GLSLC_PATH
    return resolveGlslcPath(HT_GLSLC_PATH);
#else
    return resolveGlslcPath("");
#endif
  }

  ShaderCompilationResult ShaderCompiler::compileGLSLToSPIRV(
      const std::string& glslSource,
      const std::string& entryPoint,
      const std::string& shaderStage,
      const std::unordered_map<std::string, std::string>& defines) {
    auto startTime = hires_clock::now();
    ShaderCompilationResult result;

    // Create temporary shader file
    const auto uniqueName =
        "shader_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(std::random_device{}()) + ".glsl";
    fs::path tempShaderPath = fs::temp_directory_path() / uniqueName;
    std::ofstream shaderFile(tempShaderPath);
    if (!shaderFile.is_open()) {
      result.errorMessage = "Failed to create temporary shader file";
      return result;
    }

    // Add version and precision
    shaderFile << "#version 450\n";
    shaderFile << "#extension GL_ARB_separate_shader_objects : enable\n";

    // Add defines
    std::string definesStr = buildDefineFlags(defines);
    if (!definesStr.empty()) {
      shaderFile << definesStr << "\n";
    }

    // Add shader source
    shaderFile << glslSource;
    shaderFile.close();

    // Create temporary SPIR-V output path
    fs::path tempSpirvPath = tempShaderPath;
    tempSpirvPath += ".spv";
    fs::path tempLogPath = tempShaderPath;
    tempLogPath += ".log";
    const auto cleanup = [&] {
      bool removed = true;
      for (const auto& file : {tempShaderPath, tempSpirvPath, tempLogPath})
        if (!detail::removeOwnedTemporaryFile(file))
          removed = false;
      if (!removed)
        result.warningMessage = "Temporary shader files remain in use; compiled result is preserved";
    };

    // Build glslc command
    const fs::path glslcPath = executablePath();
    const auto pathText = [](const fs::path& path) {
      const auto utf8 = path.u8string();
      return std::string(utf8.begin(), utf8.end());
    };
    auto quoteArg = [&](const fs::path& path) {
      return "\"" + pathText(path) + "\"";
    };

    std::vector<std::string> args = {
        pathText(glslcPath),
        "-fshader-stage=" + shaderStageToString(shaderStage),
        "-fentry-point=" + entryPoint,
        "-o",
        pathText(tempSpirvPath),
        pathText(tempShaderPath),
    };

    std::string command = "\"" + pathText(glslcPath) + "\"";
    for (size_t i = 1; i < args.size(); ++i) {
      command += " " + quoteArg(fs::u8path(args[i]));
    }

    // Execute glslc
#ifdef _WIN32
    std::vector<std::wstring> wideArgs;
    for (const auto& arg : args)
      wideArgs.push_back(fs::u8path(arg).wstring());
    // The CRT joins argv into a Windows command line; quote paths with spaces.
    for (size_t i : {size_t(0), size_t(4), size_t(5)})
      wideArgs[i] = L"\"" + wideArgs[i] + L"\"";
    std::vector<const wchar_t*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : wideArgs) {
      argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    intptr_t retCode = glslcPath.has_parent_path() ? _wspawnv(_P_WAIT, glslcPath.c_str(), argv.data())
                                                   : _wspawnvp(_P_WAIT, glslcPath.c_str(), argv.data());
#else
    command += " 2> " + quoteArg(tempLogPath);
    int retCode = std::system(command.c_str());
#endif
    if (retCode != 0) {
      result.errorMessage = "glslc compilation failed with code " + std::to_string(retCode);
      result.errorMessage += "; command: " + command;
#ifndef _WIN32
      const std::string compilerLog = readFileContents(tempLogPath.string());
      if (!compilerLog.empty()) {
        result.errorMessage += ": " + compilerLog;
      }
#endif
      cleanup();
      return result;
    }

    // Read compiled SPIR-V
    std::ifstream spirvFile(tempSpirvPath, std::ios::binary | std::ios::ate);
    if (!spirvFile.is_open()) {
      result.errorMessage = "Failed to read compiled SPIR-V file";
      cleanup();
      return result;
    }

    std::streamsize fileSize = spirvFile.tellg();
    if (fileSize < 20 || fileSize % sizeof(uint32_t) != 0) {
      result.errorMessage = "Invalid compiled SPIR-V file size";
      spirvFile.close();
      cleanup();
      return result;
    }
    spirvFile.seekg(0, std::ios::beg);

    result.spirvCode.resize(fileSize / sizeof(uint32_t));
    if (!spirvFile.read(reinterpret_cast<char*>(result.spirvCode.data()), fileSize)) {
      result.errorMessage = "Failed to read SPIR-V code";
      spirvFile.close();
      cleanup();
      return result;
    }

    spirvFile.close();
    result.success = true;

    // Cleanup temporary files
    cleanup();

    auto endTime = hires_clock::now();
    result.compilationTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return result;
  }

  ShaderCompilationResult ShaderCompiler::compileGLSLFileToSPIRV(
      const std::string& filePath,
      const std::string& entryPoint,
      const std::string& shaderStage,
      const std::unordered_map<std::string, std::string>& defines) {
    ShaderCompilationResult result;

    // Read file
    std::string source = readFileContents(filePath);
    if (source.empty()) {
      result.errorMessage = "Failed to read shader file: " + filePath;
      return result;
    }

    // Watch file if watching is enabled
    if (watchingEnabled) {
      watchedFileTimestamps[filePath] = getFileTimestamp(filePath);
    }

    return compileGLSLToSPIRV(source, entryPoint, shaderStage, defines);
  }

  void ShaderCompiler::addVariant(const std::string& shaderId, const ShaderVariant& variant) {
    if (variantCaches.find(shaderId) == variantCaches.end()) {
      variantCaches[shaderId] = VariantCache();
    }

    ShaderVariantKey key = makeVariantKey(variant.defines);
    variantCaches[shaderId][key] = variant;
  }

  ShaderVariant* ShaderCompiler::getVariant(const std::string& shaderId,
                                            const std::unordered_map<std::string, std::string>& defines) {
    if (variantCaches.find(shaderId) == variantCaches.end()) {
      return nullptr;
    }

    ShaderVariantKey key = makeVariantKey(defines);
    auto& cache = variantCaches[shaderId];

    if (cache.find(key) == cache.end()) {
      return nullptr;
    }

    return &cache[key];
  }

  bool ShaderCompiler::hasVariant(const std::string& shaderId,
                                  const std::unordered_map<std::string, std::string>& defines) const {
    if (variantCaches.find(shaderId) == variantCaches.end()) {
      return false;
    }

    ShaderVariantKey key = makeVariantKey(defines);
    return variantCaches.at(shaderId).find(key) != variantCaches.at(shaderId).end();
  }

  void ShaderCompiler::clearVariants(const std::string& shaderId) {
    variantCaches.erase(shaderId);
  }

  void ShaderCompiler::clearAllVariants() {
    variantCaches.clear();
  }

  void ShaderCompiler::updateWatchedFiles() {
    if (!watchingEnabled)
      return;

    updatedShaders.clear();

    for (auto& [filePath, lastTimestamp] : watchedFileTimestamps) {
      uint64_t currentTimestamp = getFileTimestamp(filePath);
      if (currentTimestamp > lastTimestamp) {
        watchedFileTimestamps[filePath] = currentTimestamp;
        updatedShaders.push_back(filePath);
      }
    }
  }

  std::vector<std::string> ShaderCompiler::getUpdatedShaders() {
    auto result = updatedShaders;
    updatedShaders.clear();
    return result;
  }

  std::string ShaderCompiler::shaderStageToString(const std::string& stage) {
    if (stage == "vertex" || stage == "vert" || stage == "vs")
      return "vertex";
    if (stage == "fragment" || stage == "frag" || stage == "fs")
      return "fragment";
    if (stage == "compute" || stage == "comp" || stage == "cs")
      return "compute";
    if (stage == "geometry" || stage == "geom" || stage == "gs")
      return "geometry";
    if (stage == "tessellation_control" || stage == "tess_ctrl")
      return "tessellation_control";
    if (stage == "tessellation_evaluation" || stage == "tess_eval")
      return "tessellation_evaluation";
    return stage;
  }

  std::string ShaderCompiler::getShaderExtension(const std::string& stage) {
    std::string normalized = shaderStageToString(stage);
    if (normalized == "vertex")
      return ".vert";
    if (normalized == "fragment")
      return ".frag";
    if (normalized == "compute")
      return ".comp";
    if (normalized == "geometry")
      return ".geom";
    if (normalized == "tessellation_control")
      return ".tesc";
    if (normalized == "tessellation_evaluation")
      return ".tese";
    return ".glsl";
  }

  ShaderCompiler::ShaderVariantKey ShaderCompiler::makeVariantKey(
      const std::unordered_map<std::string, std::string>& defines) const {
    ShaderVariantKey key;
    for (const auto& [name, value] : defines) {
      key.sortedDefines.emplace_back(name, value);
    }
    std::sort(key.sortedDefines.begin(), key.sortedDefines.end());
    return key;
  }

  std::string ShaderCompiler::buildDefineFlags(const std::unordered_map<std::string, std::string>& defines) const {
    std::string result;
    for (const auto& [name, value] : defines) {
      result += "#define " + name;
      if (!value.empty()) {
        result += " " + value;
      }
      result += "\n";
    }
    return result;
  }

  uint64_t ShaderCompiler::getFileTimestamp(const std::string& filePath) const {
    try {
      auto lastWriteTime = fs::last_write_time(filePath);
      return lastWriteTime.time_since_epoch().count();
    } catch (...) {
      return 0;
    }
  }

  std::string ShaderCompiler::readFileContents(const std::string& filePath) const {
    std::ifstream file(filePath);
    if (!file.is_open()) {
      return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  // Hash function for ShaderVariantKey
  size_t ShaderCompiler::ShaderVariantKeyHash::operator()(const ShaderVariantKey& key) const {
    size_t hash = 0;
    for (const auto& [name, value] : key.sortedDefines) {
      hash ^= std::hash<std::string>()(name) ^ (std::hash<std::string>()(value) << 1);
    }
    return hash;
  }

} // namespace HongTu::Asset
