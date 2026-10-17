#include <hongtu/core/logging/logger.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace hongtu::core {

Logger& Logger::instance() {
  static Logger instance;
  return instance;
}

void Logger::initialize(const String& name) {
  try {
    // Create console sink with color output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Create rotating file sink (10MB max, 3 rotations)
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/hongtu_engine.log", 10 * 1024 * 1024, 3);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    // Combine sinks
    spdlog::sinks_init_list sink_list{console_sink, file_sink};
    logger_ = std::make_shared<spdlog::logger>(name, sink_list);

    // Set default level
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::critical);

    // Register as default logger
    spdlog::register_logger(logger_);

  } catch (const spdlog::spdlog_ex&) {
    // Fallback to console output if initialization fails
    logger_ = spdlog::stdout_color_mt(name);
  }
}

void Logger::shutdown() {
  if (logger_) {
    logger_->flush();
    spdlog::drop_all();
    logger_.reset();
  }
}

void Logger::set_level(LogLevel level) {
  current_level_ = level;
  if (logger_) {
    logger_->set_level(static_cast<spdlog::level::level_enum>(static_cast<int>(level)));
  }
}

LogLevel Logger::get_level() const {
  return current_level_;
}

void Logger::add_sink(std::shared_ptr<spdlog::sinks::sink> sink) {
  if (logger_) {
    logger_->sinks().push_back(sink);
  }
}

void Logger::flush() {
  if (logger_) {
    logger_->flush();
  }
}

Logger::~Logger() {
  shutdown();
}

void assert_handler(const char* file, int line, const char* condition,
                    const char* message) {
  Logger::instance().critical("Assertion failed at {}:{}", file, line);
  Logger::instance().critical("Condition: {}", condition);
  Logger::instance().critical("Message: {}", message);
  std::abort();
}

} // namespace hongtu::core
