#pragma once

#include <chrono>
#include <filesystem>
#include <thread>

namespace HongTu::Asset::detail {
  // Only call for a file created/owned by this operation. A transient reader or
  // scanner must not turn valid compiled shader bytes into a renderer failure.
  inline bool removeOwnedTemporaryFile(const std::filesystem::path& path) noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
      std::error_code error;
      std::filesystem::remove(path, error);
      if (!error)
        return true;
#ifdef _WIN32
      const bool sharing = error.value() == 32 || error.value() == 33 || error == std::errc::permission_denied;
      if (sharing && attempt < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
#endif
      return false;
    }
    return false;
  }
} // namespace HongTu::Asset::detail
