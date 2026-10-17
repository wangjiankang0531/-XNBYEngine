#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HongTu::Asset {

  // ============================================================================
  // Shader Compilation Result
  // ============================================================================

  struct ShaderCompilationResult {
    bool success = false;
    std::vector<uint32_t> spirvCode;
    std::string errorMessage;
    std::string warningMessage;
    uint64_t compilationTimeMs = 0;
  };

  // ============================================================================
  // Shader Variant
  // ============================================================================

  struct ShaderVariant {
    std::unordered_map<std::string, std::string> defines;
    std::vector<uint32_t> spirvCode;
    uint64_t timestamp = 0;

    bool operator==(const ShaderVariant& other) const {
      return defines == other.defines && spirvCode == other.spirvCode;
    }
  };

  // ============================================================================
  // Shader Compiler
  // ============================================================================

  class ShaderCompiler {
  public:
    ShaderCompiler();
    ~ShaderCompiler() = default;

    // Compilation
    ShaderCompilationResult compileGLSLToSPIRV(const std::string& glslSource,
                                               const std::string& entryPoint,
                                               const std::string& shaderStage,
                                               const std::unordered_map<std::string, std::string>& defines = {});

    ShaderCompilationResult compileGLSLFileToSPIRV(const std::string& filePath,
                                                   const std::string& entryPoint,
                                                   const std::string& shaderStage,
                                                   const std::unordered_map<std::string, std::string>& defines = {});

    // Variant Management
    void addVariant(const std::string& shaderId, const ShaderVariant& variant);

    ShaderVariant* getVariant(const std::string& shaderId, const std::unordered_map<std::string, std::string>& defines);

    bool hasVariant(const std::string& shaderId, const std::unordered_map<std::string, std::string>& defines) const;

    void clearVariants(const std::string& shaderId);
    void clearAllVariants();

    // Hot Reload
    void enableFileWatching(bool enable) {
      watchingEnabled = enable;
    }
    void updateWatchedFiles();
    bool hasUpdatedShaders() const {
      return !updatedShaders.empty();
    }
    std::vector<std::string> getUpdatedShaders();

    // Utilities
    // Prefer the compiler shipped beside this executable. Does not depend on cwd.
    static std::filesystem::path executablePath();
    static std::string shaderStageToString(const std::string& stage);
    static std::string getShaderExtension(const std::string& stage);

  private:
    struct ShaderVariantKey {
      std::vector<std::pair<std::string, std::string>> sortedDefines;

      bool operator==(const ShaderVariantKey& other) const {
        return sortedDefines == other.sortedDefines;
      }
    };

    struct ShaderVariantKeyHash {
      size_t operator()(const ShaderVariantKey& key) const;
    };

    using VariantCache = std::unordered_map<ShaderVariantKey, ShaderVariant, ShaderVariantKeyHash>;

    // Variant caching
    std::unordered_map<std::string, VariantCache> variantCaches;

    // File watching
    bool watchingEnabled = false;
    std::unordered_map<std::string, uint64_t> watchedFileTimestamps;
    std::vector<std::string> updatedShaders;

    // Helper methods
    ShaderVariantKey makeVariantKey(const std::unordered_map<std::string, std::string>& defines) const;
    std::string buildDefineFlags(const std::unordered_map<std::string, std::string>& defines) const;
    uint64_t getFileTimestamp(const std::string& filePath) const;
    std::string readFileContents(const std::string& filePath) const;
  };

} // namespace HongTu::Asset
