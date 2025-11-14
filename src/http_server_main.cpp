/**
 * @file http_server_main.cpp
 * @brief HTTP JSON-RPC MCP 服务器主程序
 *
 * 使用方法:
 *   ./mcp_http_server [--host HOST] [--port PORT] [--config CONFIG_FILE]
 */

#include "config.h"
#include "logger.h"
#include "http_jsonrpc.h"

#include <iostream>
#include <csignal>
#include <atomic>

using namespace mcp;

// 全局服务器实例指针（用于信号处理）
static std::atomic<mcp::HttpJsonRpcServer*> g_server{nullptr};

/**
 * @brief 信号处理函数
 */
void signal_handler(int signal) {
    std::cerr << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    auto* server = g_server.load();
    if (server) {
        server->stop();
    }
}

/**
 * @brief 字符串转日志级别
 */
spdlog::level::level_enum StringToLogLevel(const std::string& level_str) {
    if (level_str == "trace") {
        return spdlog::level::trace;
    } else if (level_str == "debug") {
        return spdlog::level::debug;
    } else if (level_str == "info") {
        return spdlog::level::info;
    } else if (level_str == "warn") {
        return spdlog::level::warn;
    } else if (level_str == "error") {
        return spdlog::level::err;
    } else if (level_str == "critical") {
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file = "../../config/server.json";
    std::string host;
    int port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --config FILE    Configuration file path (default: ../../config/server.json)\n"
                      << "  --host HOST      Server host (default: from config or 0.0.0.0)\n"
                      << "  --port PORT      Server port (default: from config or 8080)\n"
                      << "  --help, -h       Show this help message\n";
            return 0;
        }
    }

    // 加载配置文件
    if (!MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
        std::cerr << "Using default configuration..." << std::endl;
    } else {
        std::cout << "Config loaded successfully: " << config_file << std::endl;
    }

    // 从配置文件获取设置（如果命令行没有指定）
    if (host.empty()) {
        host = "0.0.0.0";  // 默认监听所有接口
    }
    if (port == 0) {
        port = MCP_CONFIG.GetServerPort();
    }

    std::string log_file = MCP_CONFIG.GetLogFilePath();
    std::string log_level_str = MCP_CONFIG.GetLogLevel();
    size_t log_file_size = MCP_CONFIG.GetLogFileSize();
    int log_file_count = MCP_CONFIG.GetLogFileCount();
    bool log_console = MCP_CONFIG.GetLogConsoleOutput();

    spdlog::level::level_enum log_level = StringToLogLevel(log_level_str);

    // 初始化日志系统
    MCP_LOG_INIT("mcp_http_server", log_file, log_file_size, log_file_count, log_console);
    MCP_LOG_SET_LEVEL(log_level);

    MCP_LOG_INFO("=== MCP HTTP JSON-RPC Server ===");
    MCP_LOG_INFO("Configuration:");
    MCP_LOG_INFO("  Host: {}", host);
    MCP_LOG_INFO("  Port: {}", port);
    MCP_LOG_INFO("  Log file: {}", log_file.empty() ? "disabled" : log_file);
    MCP_LOG_INFO("  Log level: {}", log_level_str);
    MCP_LOG_INFO("  Log console: {}", log_console);

    try {
        // 创建 JSON-RPC 调度器并注册方法
        JsonRpcDispatcher dispatcher;

        // 注册 initialize 方法
        dispatcher.registerHandler("initialize", [](const nlohmann::json& params) -> nlohmann::json {
            MCP_LOG_INFO("Client initialized");
            nlohmann::json capabilities = {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", {
                    {"tools", {{"listChanged", false}}},
                    {"resources", {{"listChanged", false}, {"subscribe", false}}},
                    {"prompts", {{"listChanged", false}}}
                }},
                {"serverInfo", {
                    {"name", "mcp-http-tutorial"},
                    {"version", "1.0.0"}
                }}
            };
            return capabilities;
        });

        // 注册 echo 方法（测试用）
        dispatcher.registerHandler("echo", [](const nlohmann::json& params) -> nlohmann::json {
            MCP_LOG_DEBUG("Echo request: {}", params.dump());
            return params;
        });

        // 注册 tools/list 方法
        dispatcher.registerHandler("tools/list", [](const nlohmann::json& /*params*/) -> nlohmann::json {
            nlohmann::json tools = {
                {"tools", nlohmann::json::array({
                    {
                        {"name", "echo"},
                        {"description", "Echo back the input"},
                        {"inputSchema", {
                            {"type", "object"},
                            {"properties", {
                                {"message", {{"type", "string"}, {"description", "Message to echo"}}}
                            }},
                            {"required", nlohmann::json::array({"message"})}
                        }}
                    }
                })}
            };
            return tools;
        });

        // 注册 tools/call 方法
        dispatcher.registerHandler("tools/call", [](const nlohmann::json& params) -> nlohmann::json {
            std::string name = params.at("name").get<std::string>();
            nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

            MCP_LOG_INFO("Tool call: {} with arguments: {}", name, arguments.dump());

            if (name == "echo") {
                std::string message = arguments.at("message").get<std::string>();
                return {
                    {"content", nlohmann::json::array({
                        {{"type", "text"}, {"text", message}}
                    })}
                };
            }

            throw std::runtime_error("Unknown tool: " + name);
        });

        // 创建 HTTP 服务器
        // 如果命令行指定了 host/port，使用命令行参数；否则使用配置文件
        HttpJsonRpcServer server = (host != "0.0.0.0" || port != MCP_CONFIG.GetServerPort())
            ? HttpJsonRpcServer(std::move(dispatcher), host, port)
            : HttpJsonRpcServer(std::move(dispatcher));
        g_server.store(&server);

        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 启动服务器（阻塞）
        server.run();

        g_server.store(nullptr);
        MCP_LOG_INFO("Server shutdown complete");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}
