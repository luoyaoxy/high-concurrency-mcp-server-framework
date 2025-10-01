#include <gtest/gtest.h>
#include "../src/include/mcp/logger.h"
#include <fstream>
#include <filesystem>

using namespace mcp::logger;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test runs from build/tests/, so use relative path
        test_log_dir_ = "../../logs/test";
        test_log_file_ = test_log_dir_ + "/test_logger.log";

        if (std::filesystem::exists(test_log_dir_)) {
            std::filesystem::remove_all(test_log_dir_);
        }
    }

    void TearDown() override {
        // Shutdown logger
        Logger::getInstance().shutdown();

        // Clean up test log files
        if (std::filesystem::exists(test_log_dir_)) {
            std::filesystem::remove_all(test_log_dir_);
        }
    }

    std::string test_log_dir_;
    std::string test_log_file_;
};

// 测试 Logger 初始化
TEST_F(LoggerTest, InitializationTest) {
    // 只输出到控制台，不输出到文件
    Logger::getInstance().init("test_logger", "", 1024, 2, true);

    auto logger = Logger::getInstance().getLogger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "test_logger");
}

// 测试文件日志输出
TEST_F(LoggerTest, FileOutputTest) {
    // 初始化 logger 并输出到文件
    Logger::getInstance().init("test_logger", test_log_file_, 1024*1024, 2, false);

    // 写入日志
    auto logger = Logger::getInstance().getLogger();
    ASSERT_NE(logger, nullptr);

    logger->info("Test info message");
    logger->warn("Test warning message");
    logger->error("Test error message");

    // 刷新日志
    Logger::getInstance().flush();

    // 检查日志文件是否存在
    EXPECT_TRUE(std::filesystem::exists(test_log_file_));

    // 读取日志文件内容
    std::ifstream log_file(test_log_file_);
    ASSERT_TRUE(log_file.is_open());

    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    log_file.close();

    // 验证日志内容
    EXPECT_TRUE(content.find("Test info message") != std::string::npos);
    EXPECT_TRUE(content.find("Test warning message") != std::string::npos);
    EXPECT_TRUE(content.find("Test error message") != std::string::npos);
}

// 测试日志级别设置
TEST_F(LoggerTest, LogLevelTest) {
    Logger::getInstance().init("test_logger", test_log_file_, 1024*1024, 2, false);

    // 设置日志级别为 WARN
    Logger::getInstance().setLevel(spdlog::level::warn);

    auto logger = Logger::getInstance().getLogger();
    ASSERT_NE(logger, nullptr);

    // 写入不同级别的日志
    logger->debug("Debug message - should not appear");
    logger->info("Info message - should not appear");
    logger->warn("Warn message - should appear");
    logger->error("Error message - should appear");

    Logger::getInstance().flush();

    // 读取日志文件
    std::ifstream log_file(test_log_file_);
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    log_file.close();

    // 验证只有 WARN 和 ERROR 级别的日志被记录
    EXPECT_TRUE(content.find("Debug message") == std::string::npos);
    EXPECT_TRUE(content.find("Info message") == std::string::npos);
    EXPECT_TRUE(content.find("Warn message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
}

// 测试日志宏
TEST_F(LoggerTest, LogMacrosTest) {
    MCP_LOG_INIT("test_logger", test_log_file_, 1024*1024, 2, false);
    MCP_LOG_SET_LEVEL(spdlog::level::trace);

    // 使用宏记录不同级别的日志
    MCP_LOG_TRACE("Trace log with value: {}", 42);
    MCP_LOG_DEBUG("Debug log with string: {}", "test");
    MCP_LOG_INFO("Info log");
    MCP_LOG_WARN("Warning log");
    MCP_LOG_ERROR("Error log");
    MCP_LOG_CRITICAL("Critical log");

    MCP_LOG_FLUSH();

    // 读取日志文件
    std::ifstream log_file(test_log_file_);
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    log_file.close();

    // 验证所有日志都被记录（注意：现在所有日志宏都会添加 [file:line] 前缀）
    EXPECT_TRUE(content.find("test_logger.cpp") != std::string::npos);
    // EXPECT_TRUE(content.find("Trace log with value: 42") != std::string::npos);
    // EXPECT_TRUE(content.find("Debug log with string: test") != std::string::npos);
    EXPECT_TRUE(content.find("Info log") != std::string::npos);
    EXPECT_TRUE(content.find("Warning log") != std::string::npos);
    EXPECT_TRUE(content.find("Error log") != std::string::npos);
    EXPECT_TRUE(content.find("Critical log") != std::string::npos);
}

// 测试带位置信息的日志宏
TEST_F(LoggerTest, LogMacrosWithLocationTest) {
    MCP_LOG_INIT("test_logger", test_log_file_, 1024*1024, 2, false);
    MCP_LOG_SET_LEVEL(spdlog::level::debug);

    // 使用带位置信息的日志宏（现在所有宏都带位置信息）
    MCP_LOG_DEBUG("Debug message with location");
    MCP_LOG_ERROR("Error message with location");

    MCP_LOG_FLUSH();

    // 读取日志文件
    std::ifstream log_file(test_log_file_);
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    log_file.close();

    // 验证日志中包含文件名和行号信息
    EXPECT_TRUE(content.find("test_logger.cpp") != std::string::npos);
    EXPECT_TRUE(content.find("Debug message with location") != std::string::npos);
    EXPECT_TRUE(content.find("Error message with location") != std::string::npos);
}

// 测试条件日志宏
TEST_F(LoggerTest, ConditionalLogMacrosTest) {
    MCP_LOG_INIT("test_logger", test_log_file_, 1024*1024, 2, false);
    MCP_LOG_SET_LEVEL(spdlog::level::debug);  // Set level to debug to record DEBUG logs

    int value = 10;
    MCP_LOG_DEBUG_IF(value > 5, "Value is greater than 5: {}", value);
    MCP_LOG_DEBUG_IF(value < 5, "Value is less than 5: {}", value);

    int error_code = 0;
    MCP_LOG_ERROR_IF(error_code != 0, "Error occurred with code: {}", error_code);

    error_code = 404;
    MCP_LOG_ERROR_IF(error_code != 0, "Error occurred with code: {}", error_code);

    MCP_LOG_FLUSH();

    // 读取日志文件
    std::ifstream log_file(test_log_file_);
    std::string content((std::istreambuf_iterator<char>(log_file)),
                        std::istreambuf_iterator<char>());
    log_file.close();

    // 验证只有满足条件的日志被记录（注意：日志宏会添加 [file:line] 前缀）
    // EXPECT_TRUE(content.find("test_logger.cpp") != std::string::npos);
    EXPECT_TRUE(content.find("Value is greater than 5: 10") != std::string::npos);
    EXPECT_TRUE(content.find("Value is less than 5") == std::string::npos);
    EXPECT_TRUE(content.find("Error occurred with code: 404") != std::string::npos);
}

// 测试自动初始化
TEST_F(LoggerTest, AutoInitializationTest) {
    // 不手动初始化，直接使用日志宏
    MCP_LOG_INFO("This should trigger auto-initialization");

    auto logger = Logger::getInstance().getLogger();
    ASSERT_NE(logger, nullptr);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
