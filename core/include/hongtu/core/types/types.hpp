#pragma once

#include <hongtu/core/common.hpp>

namespace hongtu::core {

// ============================================================================
// Basic type definitions and utilities
// ============================================================================

// Result type for error handling
template<typename T, typename E = String>
class Result {
 public:
  static Result<T, E> ok(T value) {
    Result r;
    r.value_ = value;
    r.is_ok_ = true;
    return r;
  }

  static Result<T, E> error(E error) {
    Result r;
    r.error_ = error;
    r.is_ok_ = false;
    return r;
  }

  bool is_ok() const { return is_ok_; }
  bool is_error() const { return !is_ok_; }

  T unwrap() const {
    HT_ASSERT(is_ok_, "Attempted to unwrap an error result");
    return value_;
  }

  E unwrap_error() const {
    HT_ASSERT(is_error(), "Attempted to unwrap error from OK result");
    return error_;
  }

  T unwrap_or(T default_value) const {
    return is_ok_ ? value_ : default_value;
  }

 private:
  bool is_ok_{false};
  T value_{};
  E error_{};
};

// Specialization for void return type
template<typename E>
class Result<void, E> {
 public:
  static Result<void, E> ok() {
    Result r;
    r.is_ok_ = true;
    return r;
  }

  static Result<void, E> error(E error) {
    Result r;
    r.error_ = error;
    r.is_ok_ = false;
    return r;
  }

  bool is_ok() const { return is_ok_; }
  bool is_error() const { return !is_ok_; }

  void unwrap() const {
    HT_ASSERT(is_ok_, "Attempted to unwrap an error result");
  }

  E unwrap_error() const {
    HT_ASSERT(is_error(), "Attempted to unwrap error from OK result");
    return error_;
  }

 private:
  bool is_ok_{false};
  E error_{};
};

// ============================================================================
// Byte span and buffer utilities
// ============================================================================

using ByteSpan = Span<const u8>;
using MutableByteSpan = Span<u8>;

// ============================================================================
// Handle types for opaque references
// ============================================================================

template<typename Tag>
class Handle {
 public:
  Handle() : value_(INVALID_HANDLE) {}

  explicit Handle(u64 value) : value_(value) {}

  bool is_valid() const { return value_ != INVALID_HANDLE; }

  u64 get() const { return value_; }

  bool operator==(const Handle& other) const { return value_ == other.value_; }
  bool operator!=(const Handle& other) const { return value_ != other.value_; }
  bool operator<(const Handle& other) const { return value_ < other.value_; }

  static constexpr u64 INVALID_HANDLE = ~0ULL;

 private:
  u64 value_;
};

// ============================================================================
// Bit operations
// ============================================================================

template<typename T>
constexpr bool has_flag(T value, T flag) {
  return (value & flag) == flag;
}

template<typename T>
constexpr T set_flag(T value, T flag) {
  return value | flag;
}

template<typename T>
constexpr T clear_flag(T value, T flag) {
  return value & ~flag;
}

template<typename T>
constexpr T toggle_flag(T value, T flag) {
  return value ^ flag;
}

} // namespace hongtu::core
