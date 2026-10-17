#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/core/logging/log_level.hpp>
#include <memory>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

namespace hongtu::core {

// ============================================================================
// Logger class - centralized logging system
// ============================================================================

class Logger {
 public:
  static Logger& instance();

  // Initialize logger with custom configuration
  void initialize(const String& name = "XNBYEngine");

  // Shutdown logger
  void shutdown();

  // Logging functions with printf-style formatting
  template<typename... Args>
  void trace(StringView format, Args&&... args) {
    if (logger_) logger_->trace(fmt::runtime(format), std::forward<Args>(args)...);
  }
  template<typename... Args>
  void debug(StringView format, Args&&... args) {
    if (logger_) logger_->debug(fmt::runtime(format), std::forward<Args>(args)...);
  }
  template<typename... Args>
  void info(StringView format, Args&&... args) {
    if (logger_) logger_->info(fmt::runtime(format), std::forward<Args>(args)...);
  }
  template<typename... Args>
  void warn(StringView format, Args&&... args) {
    if (logger_) logger_->warn(fmt::runtime(format), std::forward<Args>(args)...);
  }
  template<typename... Args>
  void error(StringView format, Args&&... args) {
    if (logger_) logger_->error(fmt::runtime(format), std::forward<Args>(args)...);
  }
  template<typename... Args>
  void critical(StringView format, Args&&... args) {
    if (logger_) logger_->critical(fmt::runtime(format), std::forward<Args>(args)...);
  }

  // Set log level
  void set_level(LogLevel level);

  // Get log level
  LogLevel get_level() const;

  // Add custom sink
  void add_sink(std::shared_ptr<spdlog::sinks::sink> sink);

  // Flush all loggers
  void flush();

 private:
  Logger() = default;
  ~Logger();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  std::shared_ptr<spdlog::logger> logger_;
  LogLevel current_level_{LogLevel::Info};
};

// ============================================================================
// Convenience macros for logging
// ============================================================================

#define HT_LOG_TRACE(...) ::hongtu::core::Logger::instance().trace(__VA_ARGS__)
#define HT_LOG_DEBUG(...) ::hongtu::core::Logger::instance().debug(__VA_ARGS__)
#define HT_LOG_INFO(...) ::hongtu::core::Logger::instance().info(__VA_ARGS__)
#define HT_LOG_WARN(...) ::hongtu::core::Logger::instance().warn(__VA_ARGS__)
#define HT_LOG_ERROR(...) ::hongtu::core::Logger::instance().error(__VA_ARGS__)
#define HT_LOG_CRITICAL(...) ::hongtu::core::Logger::instance().critical(__VA_ARGS__)

} // namespace hongtu::core
