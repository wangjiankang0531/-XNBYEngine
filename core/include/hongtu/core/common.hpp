#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Compiler-specific attributes and macros
// ============================================================================

#ifdef _WIN32
  #define HT_PLATFORM_WINDOWS 1
  #define HT_EXPORT __declspec(dllexport)
  #define HT_IMPORT __declspec(dllimport)
#else
  #define HT_PLATFORM_WINDOWS 0
  #define HT_EXPORT __attribute__((visibility("default")))
  #define HT_IMPORT
#endif

#ifdef __clang__
  #define HT_COMPILER_CLANG 1
  #define HT_LIKELY(x) __builtin_expect(!!(x), 1)
  #define HT_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define HT_NO_INLINE __attribute__((noinline))
  #define HT_ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
  #define HT_COMPILER_CLANG 0
  #define HT_LIKELY(x) (x)
  #define HT_UNLIKELY(x) (x)
  #define HT_NO_INLINE __declspec(noinline)
  #define HT_ALWAYS_INLINE __forceinline
#else
  #define HT_COMPILER_CLANG 0
  #define HT_LIKELY(x) (x)
  #define HT_UNLIKELY(x) (x)
  #define HT_NO_INLINE __attribute__((noinline))
  #define HT_ALWAYS_INLINE inline
#endif

// Debug and assertion macros
#ifndef HT_DEBUG
  #ifdef NDEBUG
    #define HT_DEBUG 0
  #else
    #define HT_DEBUG 1
  #endif
#endif

#ifndef HT_ENABLE_ASSERTS
  #define HT_ENABLE_ASSERTS HT_DEBUG
#endif

#if HT_ENABLE_ASSERTS
  #define HT_ASSERT(condition, message)                                                    \
    do {                                                                                     \
      if (!(condition)) {                                                                   \
        ::hongtu::core::assert_handler(__FILE__, __LINE__, #condition, message);          \
      }                                                                                      \
    } while (0)
  #define HT_VERIFY(condition) HT_ASSERT(condition, "Assertion failed")
#else
  #define HT_ASSERT(condition, message) ((void)0)
  #define HT_VERIFY(condition) ((void)0)
#endif

// Unused variable macro
#define HT_UNUSED(x) (void)(x)

// ============================================================================
// Type aliases and standard types
// ============================================================================

namespace hongtu {

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using f32 = float;
using f64 = double;

using usize = size_t;
using isize = std::ptrdiff_t;

// Smart pointers
template<typename T>
using Unique = std::unique_ptr<T>;

template<typename T>
using Shared = std::shared_ptr<T>;

template<typename T>
using Weak = std::weak_ptr<T>;

// Standard containers
template<typename T>
using Vector = std::vector<T>;

template<typename T>
using Optional = std::optional<T>;

template<typename T>
using Span = std::span<T>;

// String types
using String = std::string;
using StringView = std::string_view;

// ============================================================================
// Version information
// ============================================================================

constexpr u32 HONGTU_VERSION_MAJOR = 0;
constexpr u32 HONGTU_VERSION_MINOR = 1;
constexpr u32 HONGTU_VERSION_PATCH = 0;

constexpr u32 HONGTU_VERSION =
    (HONGTU_VERSION_MAJOR << 16) | (HONGTU_VERSION_MINOR << 8) | HONGTU_VERSION_PATCH;

// ============================================================================
// Forward declarations of core utilities
// ============================================================================

namespace core {

class Logger;

void assert_handler(const char* file, int line, const char* condition, const char* message);

} // namespace core

} // namespace hongtu
