#include "../src/logger/logger.h"
#include <thread>
#include <chrono>

/**
 * @brief Logger 使用示例
 * 
 * 本示例展示了如何在项目中使用 MCP Logger 系统
 */

void basic_logging_example() {
    MCP_LOG_INFO("=== 基础日志记录示例 ===");
    
    // 不同级别的日志
    MCP_LOG_TRACE("这是一条 TRACE 级别的日志");
    MCP_LOG_DEBUG("这是一条 DEBUG 级别的日志");
    MCP_LOG_INFO("这是一条 INFO 级别的日志");
    MCP_LOG_WARN("这是一条 WARN 级别的日志");
    MCP_LOG_ERROR("这是一条 ERROR 级别的日志");
    MCP_LOG_CRITICAL("这是一条 CRITICAL 级别的日志");
}

void formatted_logging_example() {
    MCP_LOG_INFO("=== 格式化日志记录示例 ===");
    
    std::string username = "张三";
    int user_id = 12345;
    double balance = 1234.56;
    
    // 使用格式化参数
    MCP_LOG_INFO("用户登录: 用户名={}, ID={}, 余额={:.2f}", username, user_id, balance);
    
    // 复杂的格式化
    std::vector<std::string> items = {"苹果", "香蕉", "橘子"};
    MCP_LOG_DEBUG("购物车内容:");
    for (size_t i = 0; i < items.size(); ++i) {
        MCP_LOG_DEBUG("  [{}] {}", i + 1, items[i]);
    }
}

void conditional_logging_example() {
    MCP_LOG_INFO("=== 条件日志记录示例 ===");
    
    int error_code = 404;
    bool debug_mode = true;
    
    // 条件日志
    MCP_LOG_ERROR_IF(error_code != 0, "操作失败，错误码: {}", error_code);
    MCP_LOG_DEBUG_IF(debug_mode, "调试模式已启用");
    
    // 不会记录，因为条件为假
    MCP_LOG_ERROR_IF(error_code == 0, "这条日志不会被记录");
}

void location_logging_example() {
    MCP_LOG_INFO("=== 位置信息日志示例 ===");
    
    // 带文件名和行号的日志
    MCP_LOG_DEBUG_LOC("这是带位置信息的调试日志");
    MCP_LOG_ERROR_LOC("这是带位置信息的错误日志");
}

void performance_example() {
    MCP_LOG_INFO("=== 性能测试示例 ===");
    
    // 函数执行时间测量
    MCP_LOG_FUNC_TIMER();
    
    // 模拟一些工作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    MCP_LOG_INFO("函数执行完成");
}

void different_levels_example() {
    MCP_LOG_INFO("=== 不同日志级别示例 ===");
    
    // 设置为 DEBUG 级别
    MCP_LOG_SET_LEVEL(spdlog::level::debug);
    MCP_LOG_INFO("当前日志级别: DEBUG");
    MCP_LOG_DEBUG("现在可以看到 DEBUG 日志了");
    
    // 设置为 WARN 级别
    MCP_LOG_SET_LEVEL(spdlog::level::warn);
    MCP_LOG_INFO("日志级别已设置为 WARN");
    MCP_LOG_DEBUG("这条 DEBUG 日志不会显示");
    MCP_LOG_WARN("但这条 WARN 日志会显示");
    
    // 恢复为 INFO 级别
    MCP_LOG_SET_LEVEL(spdlog::level::info);
}

int main() {
    // 初始化日志系统
    // 参数：logger名称, 日志文件路径, 最大文件大小, 最大文件数, 是否输出到控制台
    MCP_LOG_INIT("mcp_logger_example", "logs/example.log", 10*1024*1024, 5, true);
    
    MCP_LOG_INFO("MCP Logger 示例程序启动");
    MCP_LOG_INFO("日志系统初始化完成");
    
    try {
        // 运行各种示例
        basic_logging_example();
        formatted_logging_example();
        conditional_logging_example();
        location_logging_example();
        performance_example();
        different_levels_example();
        
        MCP_LOG_INFO("所有示例执行完成");
        
    } catch (const std::exception& e) {
        MCP_LOG_CRITICAL("程序发生异常: {}", e.what());
        return 1;
    }
    
    // 刷新日志缓冲区
    MCP_LOG_FLUSH();
    
    MCP_LOG_INFO("程序即将退出");
    
    // 关闭日志系统
    MCP_LOG_SHUTDOWN();
    
    return 0;
}
