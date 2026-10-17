#pragma once

#include <hongtu/core/common.hpp>
#include <functional>

namespace hongtu::core {

class UUID {
 public:
  UUID();
  explicit UUID(u64 value);

  static UUID generate();
  static UUID nil();

  u64 value() const { return value_; }

  bool is_nil() const { return value_ == 0; }

  bool operator==(const UUID& other) const { return value_ == other.value_; }
  bool operator!=(const UUID& other) const { return value_ != other.value_; }
  bool operator<(const UUID& other) const { return value_ < other.value_; }

  String to_string() const;
  static UUID from_string(StringView str);

 private:
  u64 value_;
};

} // namespace hongtu::core

namespace std {
template<>
struct hash<hongtu::core::UUID> {
  usize operator()(const hongtu::core::UUID& uuid) const {
    return hash<u64>()(uuid.value());
  }
};
} // namespace std
