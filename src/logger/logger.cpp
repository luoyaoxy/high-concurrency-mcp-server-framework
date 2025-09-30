#include "logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <iostream>

namespace mcp {
namespace logger {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& logger_name,
                  const std::string& log_file_path,
                  size_t max_file_size,
                  size_t max_files,
                  bool console_output) {
    if (m_initialized) {
        spdlog::warn("Logger already initialized, skipping...");
        return;
    }

    try {
        std::vector<spdlog::sink_ptr> sinks;

        // 添加控制台输出 sink
        if (console_output) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::trace);
            console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
            sinks.push_back(console_sink);
        }

        // 添加文件输出 sink (如果指定了文件路径)
        if (!log_file_path.empty()) {
            // 创建日志文件目录 (如果不存在)
            std::filesystem::path log_path(log_file_path);
            std::filesystem::path log_dir = log_path.parent_path();
            
            if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
                std::filesystem::create_directories(log_dir);
            }

            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file_path, max_file_size, max_files);
            file_sink->set_level(spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v");
            sinks.push_back(file_sink);
        }

        // 创建 logger
        m_logger = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
        m_logger->set_level(spdlog::level::info);  // 默认级别
        m_logger->flush_on(spdlog::level::warn);   // WARN 及以上级别立即刷新

        // 注册为默认 logger
        spdlog::register_logger(m_logger);
        spdlog::set_default_logger(m_logger);

        m_initialized = true;
        
        m_logger->info("Logger 初始化成功 - name: {}, file: {}, console: {}", 
                      logger_name, 
                      log_file_path.empty() ? "disabled" : log_file_path,
                      console_output ? "enabled" : "disabled");
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger 初始化失败: " << ex.what() << std::endl;
        throw;
    }
}

void Logger::setLevel(spdlog::level::level_enum level) {
    if (m_logger) {
        m_logger->set_level(level);
        m_logger->info("Logger 级别设置为: {}", spdlog::level::to_string_view(level));
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger() {
    if (!m_initialized) {
        // 如果没有初始化，使用默认配置自动初始化
        init();
    }
    return m_logger;
}

void Logger::flush() {
    if (m_logger) {
        m_logger->flush();
    }
}

void Logger::shutdown() {
    if (m_logger) {
        m_logger->info("Logger 正在关闭...");
        m_logger->flush();
        spdlog::shutdown();
        m_logger.reset();
        m_initialized = false;
    }
}

Logger::~Logger() {
    shutdown();
}

} // namespace logger
} // namespace mcp
