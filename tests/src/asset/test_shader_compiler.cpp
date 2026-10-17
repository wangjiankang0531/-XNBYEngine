#include <gtest/gtest.h>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/asset/temporary_file_cleanup.hpp>

#include <fstream>

#ifdef _WIN32
#  include <windows.h>
#endif

TEST(ShaderTemporaryCleanup, MissingOwnedFileIsHarmless) {
  const auto path = std::filesystem::temp_directory_path() / "hongtu_nonexistent_cleanup_test_file.glsl";
  ASSERT_FALSE(std::filesystem::exists(path));
  EXPECT_TRUE(HongTu::Asset::detail::removeOwnedTemporaryFile(path));
}

#ifdef _WIN32
TEST(ShaderTemporaryCleanup, ActualWindowsSharingLockDoesNotThrow) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("hongtu_cleanup_lock_" + std::to_string(GetCurrentProcessId()) + ".glsl");
  {
    std::ofstream file(path);
    file << "owned test shader";
  }
  HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(reader, INVALID_HANDLE_VALUE);
  bool removed = true;
  EXPECT_NO_THROW(removed = HongTu::Asset::detail::removeOwnedTemporaryFile(path));
  EXPECT_FALSE(removed);
  EXPECT_TRUE(std::filesystem::exists(path));
  CloseHandle(reader);
  EXPECT_TRUE(HongTu::Asset::detail::removeOwnedTemporaryFile(path));
  EXPECT_FALSE(std::filesystem::exists(path));
}
#endif

TEST(ShaderCompilerRuntime, ValidSPIRVAndCompilerErrorRemainDistinct) {
  HongTu::Asset::ShaderCompiler compiler;
  const auto valid = compiler.compileGLSLToSPIRV("void main(){gl_Position=vec4(0,0,0,1);}", "main", "vert");
  ASSERT_TRUE(valid.success) << valid.errorMessage;
  ASSERT_GE(valid.spirvCode.size(), 5u);
  EXPECT_EQ(valid.spirvCode[0], 0x07230203u);
  const auto invalid = compiler.compileGLSLToSPIRV("not a valid shader", "main", "vert");
  EXPECT_FALSE(invalid.success);
  EXPECT_TRUE(invalid.spirvCode.empty());
  EXPECT_FALSE(invalid.errorMessage.empty());
}
