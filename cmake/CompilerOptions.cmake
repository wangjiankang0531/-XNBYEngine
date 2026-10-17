# Compiler options based on platform and build type

# Common flags for all compilers and platforms
option(HT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(MSVC)
  # MSVC compiler flags
  add_compile_options(
    /W4              # Warning level 4
    /permissive-     # Strict C++ conformance
    /Zc:inline       # Optimize dead code
    /Zc:preprocessor # Standard-conforming preprocessor
  )

  if(HT_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()

  # Debug configuration
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " /Zi /Od /RTC1")

  # Release configuration
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /Oi /Ot /Oy /Ob2")

  # Profile configuration (Release with debug info)
  if(NOT CMAKE_CXX_FLAGS_PROFILE)
    string(APPEND CMAKE_CXX_FLAGS_PROFILE " /O2 /Zi /Oy-")
  endif()

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC/Clang compiler flags
  add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wundef
    -fdiagnostics-color=auto
  )

  # C++17 specific warnings
  add_compile_options(
    -Wno-c++98-compat
    -Wno-c++11-compat
    -Wno-c++14-compat
  )

  # Debug configuration
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " -g -O0 -fno-omit-frame-pointer")

  # Add sanitizers for Debug
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(HT_USE_ASAN "Enable AddressSanitizer" ON)
    option(HT_USE_UBSAN "Enable UndefinedBehaviorSanitizer" ON)

    if(HT_USE_ASAN)
      string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=address -fno-omit-frame-pointer")
      string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG " -fsanitize=address")
    endif()

    if(HT_USE_UBSAN)
      string(APPEND CMAKE_CXX_FLAGS_DEBUG " -fsanitize=undefined")
      string(APPEND CMAKE_EXE_LINKER_FLAGS_DEBUG " -fsanitize=undefined")
    endif()
  endif()

  # Release configuration — portable by default (no -march=native) so Docker
  # images / distributables run on any x86-64 host, not just the build machine.
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -DNDEBUG")

  # Opt-in native CPU tuning for local dev builds only.
  option(HT_NATIVE_ARCH "Tune Release for the building machine's CPU (-march=native)" OFF)
  if(HT_NATIVE_ARCH)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native")
  endif()

  # Profile configuration (Release with debug symbols)
  if(NOT CMAKE_CXX_FLAGS_PROFILE)
    string(APPEND CMAKE_CXX_FLAGS_PROFILE " -O2 -g -fno-omit-frame-pointer -pg")
  endif()

  # Link-time optimization
  option(HT_ENABLE_LTO "Enable Link Time Optimization" ON)
  if(HT_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HT_IPO_SUPPORTED)
    if(HT_IPO_SUPPORTED)
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      message(STATUS "LTO enabled for Release build")
    endif()
  endif()

endif()

# Common preprocessor definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(HT_DEBUG=1 HT_ENABLE_ASSERTS=1)
else()
  add_compile_definitions(HT_DEBUG=0 NDEBUG)
endif()

if(HT_ENABLE_PROFILING)
  add_compile_definitions(HT_PROFILING_ENABLED=1)
endif()

if(HT_ENABLE_VALIDATION)
  add_compile_definitions(HT_VULKAN_VALIDATION=1)
endif()

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Compiler Options: Applied based on ${CMAKE_BUILD_TYPE} configuration")
