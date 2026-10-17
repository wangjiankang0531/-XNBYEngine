# Platform detection
if(WIN32)
  set(HT_PLATFORM "Windows")
  set(HT_PLATFORM_WINDOWS TRUE)
elseif(APPLE)
  set(HT_PLATFORM "macOS")
  set(HT_PLATFORM_MACOS TRUE)
elseif(UNIX AND NOT APPLE)
  set(HT_PLATFORM "Linux")
  set(HT_PLATFORM_LINUX TRUE)
else()
  message(FATAL_ERROR "Unsupported platform")
endif()

# Architecture detection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(HT_ARCHITECTURE "x86_64")
  set(HT_ARCH_X86_64 TRUE)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(HT_ARCHITECTURE "x86")
  set(HT_ARCH_X86 TRUE)
endif()

# Additional architecture checks
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(HT_ARCHITECTURE "aarch64")
  set(HT_ARCH_AARCH64 TRUE)
endif()

message(STATUS "Detected Platform: ${HT_PLATFORM}")
message(STATUS "Detected Architecture: ${HT_ARCHITECTURE}")

# Platform-specific settings
if(HT_PLATFORM_WINDOWS)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-DWIN32_LEAN_AND_MEAN)
elseif(HT_PLATFORM_LINUX)
  add_definitions(-D_GNU_SOURCE)
endif()
