/**
 * MCP 天气服务器 - 主入口点
 * 
 * 该文件实现了 MCP (模型上下文协议) 天气服务器的主入口点，
 * 通过各种端点提供天气信息。
 * 
 * 服务器支持：
 * - 用于 AI 助手集成的 MCP 协议
 * - 模拟和真实的 OpenWeatherMap API 后端
 * - 用于健康检查和服务器信息的 RESTful 端点
 * - 优雅关闭处理
 */

#include "mcp/weather_server.h"
#include "mcp/weather_api.h"
#include "mcp/http_transport.h"
#include "mcp/config.h"
#include "logger/logger.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>

using namespace mcp;

// 用于优雅关闭的全局标志
std::atomic<bool> running{true};

/**
 * 初始化日志系统
 * 使用新的 MCP Logger 系统
 */
void setup_logging() {
    // 使用配置文件中的日志设置
    auto& config = MCP_CONFIG;
    MCP_LOG_INIT(config.GetLoggingLoggerName(), 
                 config.GetLoggingLogFilePath(),
                 config.GetLoggingMaxFileSize(), 
                 config.GetLoggingMaxFiles(), 
                 config.GetLoggingConsoleOutput());
    MCP_LOG_SET_LEVEL(spdlog::level::debug);
    MCP_LOG_INFO("MCP 日志系统初始化完成");
}

/**
 * 优雅关闭的信号处理器
 * 处理 SIGINT (Ctrl+C) 和 SIGTERM 信号
 */
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        MCP_LOG_WARN("收到关闭信号，正在关闭服务器...");
        running = false;
    }
}

/**
 * 打印命令行参数的使用信息
 */
void print_usage(const char* program_name) {
    MCP_LOG_INFO("用法: {} [选项]", program_name);
    MCP_LOG_INFO("选项:");
    MCP_LOG_INFO("  --port PORT        HTTP 服务器端口 (默认: 8080)");
    MCP_LOG_INFO("  --api-key KEY      OpenWeatherMap API 密钥 (使用 'mock' 来获取模拟数据)");
    MCP_LOG_INFO("  --help, -h         显示此帮助信息");
}

/**
 * 解析命令行参数并验证输入
 * 如果解析成功返回 true，如果应该退出则返回 false
 */
bool parse_arguments(int argc, char* argv[], int& port, std::string& api_key) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
                if (port < 1 || port > 65535) {
                    MCP_LOG_ERROR("错误: 端口必须在 1 到 65535 之间");
                    return false;
                }
            } catch (const std::exception& e) {
                MCP_LOG_ERROR("错误: 无效的端口号");
                return false;
            }
        } else if (arg == "--api-key" && i + 1 < argc) {
            api_key = argv[++i];
            if (api_key.empty()) {
                MCP_LOG_ERROR("错误: API 密钥不能为空");
                return false;
            }
        } else {
            MCP_LOG_ERROR("未知参数: {}", arg);
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    // 加载配置文件
    if (!MCP_CONFIG.LoadFromFile("config/server.json")) {
        std::cerr << "警告: 无法加载配置文件，使用默认值" << std::endl;
    }
    
    // 初始化日志系统
    setup_logging();
    
    // 从配置文件获取默认值
    int port = MCP_CONFIG.GetServerPort();
    std::string api_key = "mock";
    
    MCP_LOG_INFO("启动 MCP 天气服务器...");
    
    // 解析并验证命令行参数
    if (!parse_arguments(argc, argv, port, api_key)) {
        return argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") ? 0 : 1;
    }
    
    MCP_LOG_DEBUG("配置参数: port={}, api_key={}", port, api_key == "mock" ? "mock" : "<hidden>");
    
    // 设置信号处理器以实现优雅关闭
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // 初始化天气 API 后端
        std::unique_ptr<WeatherApi> weather_api;
        if (api_key == "mock") {
            MCP_LOG_INFO("初始化模拟天气 API 后端");
            weather_api = std::make_unique<MockWeatherApi>();
        } else {
            MCP_LOG_INFO("初始化 OpenWeatherMap API 后端");
            weather_api = std::make_unique<OpenWeatherMapApi>(api_key);
        }
        
        // 使用天气 API 初始化 MCP 服务器
        MCP_LOG_DEBUG("初始化 MCP 服务器");
        WeatherMcpServer mcp_server(std::move(weather_api));
        
        // 初始化支持 CORS 的 HTTP 服务器
        MCP_LOG_DEBUG("初始化 HTTP 服务器在端口 {}", port);
        HttpServer http_server(port);
        http_server.EnableCors(MCP_CONFIG.GetServerCorsEnabled());
        
        // 配置带有路由的 HTTP 请求处理器
        http_server.SetRequestHandler([&mcp_server](const std::string& path, 
                                                      const std::string& method,
                                                      const std::string& body,
                                                      const std::string& content_type) -> std::string {
            
            std::cout << "收到请求 " << method << " " << path << std::endl;
            
            // 处理 CORS 预检请求
            if (method == "OPTIONS") {
                return "";
            }
            
            // 路由: MCP 协议端点
            if (method == "POST" && (path == "/" || path == "/mcp")) {
                try {
                    std::string response = mcp_server.ProcessMessage(body);
                    std::cout << "MCP 请求: " << body << std::endl;
                    std::cout << "MCP 响应: " << response << std::endl;
                    return response;
                } catch (const std::exception& e) {
                    std::cerr << "处理 MCP 请求时出错: " << e.what() << std::endl;
                    // 返回 JSON-RPC 2.0 错误响应
                    int error_code = MCP_CONFIG.GetMcpErrorCode("internal_error");
                    return R"({"jsonrpc":"2.0","error":{"code":)" + std::to_string(error_code) + R"(,"message":"Internal error"},"id":null})";
                }
            }
                        
            // 路由: 健康检查端点
            if (method == "GET" && path == "/health") {
                std::string service_name = MCP_CONFIG.GetMcpName();
                std::string version = MCP_CONFIG.GetMcpVersion();
                return R"({"status":"ok","service":")" + service_name + R"(","version":")" + version + R"(","timestamp":")"
                       + std::to_string(std::time(nullptr)) + R"("})";
            }
            
            // 路由: 服务器能力和信息
            if (method == "GET" && path == "/info") {
                std::string server_name = MCP_CONFIG.GetMcpName();
                std::string version = MCP_CONFIG.GetMcpVersion();
                std::string protocol_version = MCP_CONFIG.GetMcpProtocolVersion();
                return R"({
                    "name": ")" + server_name + R"(",
                    "version": ")" + version + R"(",
                    "protocol": "MCP )" + protocol_version + R"(",
                    "capabilities": {
                        "tools": ["get_current_weather", "get_forecast", "get_weather_by_coordinates"],
                        "resources": ["weather://help"]
                    },
                    "endpoints": {
                        "mcp": "POST /mcp",
                        "health": "GET /health", 
                        "info": "GET /info"
                    }
                })";
            }
            
            // 默认: 404 未找到
            return R"({"error":"Not found","path":")” + path + R"(","method":")” + method + R"("})";
        });
        
        // 启动 HTTP 服务器
        if (!http_server.Start()) {
            std::cerr << "在端口 " << port << " 上启动 HTTP 服务器失败" << std::endl;
            return 1;
        }
        
        // 显示服务器启动信息
        std::cout << "\n=== " << MCP_CONFIG.GetMcpName() << " 已启动 ===" << std::endl;
        std::cout << "端口: " << port << std::endl;
        std::cout << "API 后端: " << (api_key == "mock" ? "模拟 (测试)" : "OpenWeatherMap") << std::endl;
        std::cout << "\n可用端点:" << std::endl;
        std::cout << "  POST /mcp     - MCP 协议端点" << std::endl;
        std::cout << "  GET  /health  - 健康检查" << std::endl;
        std::cout << "  GET  /info    - 服务器能力" << std::endl;
        std::cout << "\n服务器就绪！按 Ctrl+C 停止。" << std::endl;
        
        // 主服务器事件循环，支持优雅关闭
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(MCP_CONFIG.GetServerShutdownCheckIntervalMs()));
        }
        
        // 优雅关闭
        std::cout << "正在停止 HTTP 服务器..." << std::endl;
        http_server.Stop();
        std::cout << "服务器成功停止。" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "服务器错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}