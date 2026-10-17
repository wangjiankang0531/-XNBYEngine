#pragma once

#include <hongtu/core/common.hpp>
#include <chrono>
#include <unordered_map>

namespace hongtu::core {

struct ProfileResult {
  String name;
  std::chrono::microseconds duration;
  std::chrono::high_resolution_clock::time_point start_time;
  std::chrono::high_resolution_clock::time_point end_time;
};

class Profiler {
 public:
  static Profiler& instance();

  void start_session(StringView session_name);
  void end_session();

  void begin_frame();
  void end_frame();

  void profile_scope(StringView scope_name);

  const Vector<ProfileResult>& get_results() const { return results_; }
  void clear_results();

 private:
  Profiler() = default;
  ~Profiler() = default;

  Profiler(const Profiler&) = delete;
  Profiler& operator=(const Profiler&) = delete;

  Vector<ProfileResult> results_;
  String current_session_name_;
  bool session_active_{false};
};

class ScopeTimer {
 public:
  ScopeTimer(StringView scope_name);
  ~ScopeTimer();

 private:
  String scope_name_;
  std::chrono::high_resolution_clock::time_point start_time_;
};

} // namespace hongtu::core

#ifdef HT_PROFILING_ENABLED
  #define HT_PROFILE_SCOPE(name)                                                           \
    ::hongtu::core::ScopeTimer HT_CONCAT(timer_, __LINE__)(name)
#else
  #define HT_PROFILE_SCOPE(name)
#endif
