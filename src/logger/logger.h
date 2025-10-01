#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/fmt/ostr.h>
#include <memory>
#include <string>

namespace mcp {
namespace logger {

/**
 * @brief Logger 管理类
 * 负责初始化和管理 spdlog 实例
 */
class Logger {
public:
    /**
     * @brief 获取 Logger 单例实例
     * @return Logger& 单例引用
     */
    static Logger& getInstance();

    /**
     * @brief 初始化日志系统
     * @param logger_name 日志器名称
     * @param log_file_path 日志文件路径 (可选)
     * @param max_file_size 单个日志文件最大大小 (默认 10MB)
     * @param max_files 最大日志文件数量 (默认 5个)
     * @param console_output 是否输出到控制台 (默认 true)
     */
    void init(const std::string& logger_name = "mcp",
              const std::string& log_file_path = "",
              size_t max_file_size = 10 * 1024 * 1024,  // 10MB
              size_t max_files = 5,
              bool console_output = true);

    /**
     * @brief 设置全局日志级别
     * @param level 日志级别
     */
    void setLevel(spdlog::level::level_enum level);

    /**
     * @brief 获取默认 logger
     * @return 共享指针到 spdlog::logger
     */
    std::shared_ptr<spdlog::logger> getLogger();

    /**
     * @brief 刷新所有日志缓冲区
     */
    void flush();

    /**
     * @brief 关闭日志系统
     */
    void shutdown();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::shared_ptr<spdlog::logger> m_logger;
    bool m_initialized = false;
};

} // namespace logger
} // namespace mcp

// ================================
// 便利宏定义
// ================================

/**
 * 初始化日志系统的宏
 * 使用示例: MCP_LOG_INIT("my_app", "logs/app.log");
 */
#define MCP_LOG_INIT(name, ...) \
    mcp::logger::Logger::getInstance().init(name, ##__VA_ARGS__)

/**
 * 设置日志级别的宏
 * 使用示例: MCP_LOG_SET_LEVEL(spdlog::level::debug);
 */
#define MCP_LOG_SET_LEVEL(level) \
    mcp::logger::Logger::getInstance().setLevel(level)

/**
 * 刷新日志缓冲区的宏
 */
#define MCP_LOG_FLUSH() \
    mcp::logger::Logger::getInstance().flush()

/**
 * 关闭日志系统的宏
 */
#define MCP_LOG_SHUTDOWN() \
    mcp::logger::Logger::getInstance().shutdown()

// ================================
// 日志记录宏定义
// ================================

/**
 * TRACE 级别日志宏
 * 使用示例: MCP_LOG_TRACE("这是一条trace日志: {}", value);
 */
#define MCP_LOG_TRACE(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->trace("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * DEBUG 级别日志宏
 * 使用示例: MCP_LOG_DEBUG("调试信息: {}", debug_value);
 */
#define MCP_LOG_DEBUG(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->debug("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * INFO 级别日志宏
 * 使用示例: MCP_LOG_INFO("程序启动成功");
 */
#define MCP_LOG_INFO(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->info("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * WARN 级别日志宏
 * 使用示例: MCP_LOG_WARN("警告: 配置文件不存在，使用默认配置");
 */
#define MCP_LOG_WARN(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->warn("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * ERROR 级别日志宏
 * 使用示例: MCP_LOG_ERROR("错误: 无法连接到服务器 {}", server_addr);
 */
#define MCP_LOG_ERROR(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->error("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * CRITICAL 级别日志宏
 * 使用示例: MCP_LOG_CRITICAL("严重错误: 系统即将崩溃");
 */
#define MCP_LOG_CRITICAL(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->critical("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

// ================================
// 带位置信息的日志宏 (用于调试)
// ================================

/**
 * 带文件名和行号的DEBUG日志宏
 * 使用示例: MCP_LOG_DEBUG_LOC("在这里出现了问题: {}", error_msg);
 */
#define MCP_LOG_DEBUG_LOC(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->debug("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

/**
 * 带文件名和行号的ERROR日志宏
 * 使用示例: MCP_LOG_ERROR_LOC("错误发生在这里: {}", error_details);
 */
#define MCP_LOG_ERROR_LOC(...) \
    do { \
        auto logger = mcp::logger::Logger::getInstance().getLogger(); \
        if (logger) logger->error("[{}:{}] " __VA_ARGS__, __FILE__, __LINE__); \
    } while(0)

// ================================
// 性能测试宏 (可选)
// ================================

/**
 * 函数执行时间测量宏
 * 使用示例: 在函数开始处添加 MCP_LOG_FUNC_TIMER();
 */
#define MCP_LOG_FUNC_TIMER() \
    struct FuncTimer { \
        std::chrono::steady_clock::time_point start; \
        const char* func_name; \
        FuncTimer(const char* name) : start(std::chrono::steady_clock::now()), func_name(name) {} \
        ~FuncTimer() { \
            auto end = std::chrono::steady_clock::now(); \
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start); \
            MCP_LOG_DEBUG("函数 {} 执行时间: {}μs", func_name, duration.count()); \
        } \
    } _func_timer(__FUNCTION__)

// ================================
// 条件日志宏
// ================================

/**
 * 条件DEBUG日志宏
 * 使用示例: MCP_LOG_DEBUG_IF(condition, "只有满足条件才记录: {}", value);
 */
#define MCP_LOG_DEBUG_IF(condition, ...) \
    do { \
        if (condition) { \
            auto logger = mcp::logger::Logger::getInstance().getLogger(); \
            if (logger) logger->debug(__VA_ARGS__); \
        } \
    } while(0)

/**
 * 条件ERROR日志宏
 * 使用示例: MCP_LOG_ERROR_IF(error_code != 0, "操作失败，错误码: {}", error_code);
 */
#define MCP_LOG_ERROR_IF(condition, ...) \
    do { \
        if (condition) { \
            auto logger = mcp::logger::Logger::getInstance().getLogger(); \
            if (logger) logger->error(__VA_ARGS__); \
        } \
    } while(0)
