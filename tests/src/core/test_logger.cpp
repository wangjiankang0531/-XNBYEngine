#include <gtest/gtest.h>
#include <hongtu/core/logging/logger.hpp>

using namespace hongtu::core;

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Logger::instance().initialize("TestLogger");
  }

  void TearDown() override {
    Logger::instance().shutdown();
  }
};

TEST_F(LoggerTest, InitializeLogger) {
  Logger& logger = Logger::instance();
  EXPECT_NE(logger.get_level(), LogLevel::Off);
}

TEST_F(LoggerTest, SetLogLevel) {
  Logger& logger = Logger::instance();
  logger.set_level(LogLevel::Debug);
  EXPECT_EQ(logger.get_level(), LogLevel::Debug);
}

TEST_F(LoggerTest, LogMessages) {
  Logger& logger = Logger::instance();
  EXPECT_NO_THROW({
    logger.info("Test info message");
    logger.debug("Test debug message");
    logger.warn("Test warning message");
    logger.error("Test error message");
  });
}
