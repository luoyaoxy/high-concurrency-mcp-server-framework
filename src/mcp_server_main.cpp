/**
 * @file mcp_server_main.cpp
 * @brief 统一的 MCP 服务器 - 同时支持 HTTP 和 stdio 传输
 *
 * 使用方法:
 *   # HTTP 模式（默认）
 *   ./mcp_server --mode http --port 8080
 *
 *   # stdio 模式
 *   ./mcp_server --mode stdio
 *
 *   # 同时启动两种模式
 *   ./mcp_server --mode both --port 8080
 */

#include "config.h"
#include "logger.h"
#include "http_jsonrpc.h"
#include "jsonrpc.h"
#include "mcp_server.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <memory>
#include <ctime>
#include <fstream>
#include <sstream>

using namespace mcp;

// 全局服务器实例指针（用于信号处理）
static std::atomic<bool> g_running{true};
static std::unique_ptr<HttpJsonRpcServer> g_http_server{nullptr};

// 信号处理函数
void signal_handler(int signal) {
    std::cerr << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    if (g_http_server) {
        g_http_server->stop();
    }
}

// 字符串转日志级别
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
    // Default to info if unknown
    return spdlog::level::info;
}

// 注册 MCP 工具、资源、提示词
void setup_mcp_server(McpServer& mcp) {
    // Echo 工具
    {
        Tool tool;
        tool.name = "echo";
        tool.description = "Echo back the input message";
        tool.input_schema.properties = {
            {"message", {{"type", "string"}, {"description", "Message to echo"}}}
        };
        tool.input_schema.required = {"message"};

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = "Echo: " + args.at("message").get<std::string>()
            });
            return result;
        });
    }

    // Calculate 工具
    {
        Tool tool;
        tool.name = "calculate";
        tool.description = "Perform basic arithmetic operations";
        tool.input_schema.properties = {
            {"operation", {{"type", "string"}, {"enum", json::array({"add", "subtract", "multiply", "divide"})}}},
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        };
        tool.input_schema.required = {"operation", "a", "b"};

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string op = args.at("operation").get<std::string>();
            double a = args.at("a").get<double>();
            double b = args.at("b").get<double>();
            double result_val = 0;

            if (op == "add") result_val = a + b;
            else if (op == "subtract") result_val = a - b;
            else if (op == "multiply") result_val = a * b;
            else if (op == "divide") {
                if (b == 0) {
                    ToolResult error;
                    error.is_error = true;
                    error.content.push_back(ContentItem{
                        .type = "text",
                        .text = "Error: Division by zero"
                    });
                    return error;
                }
                result_val = a / b;
            }

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = std::to_string(result_val)
            });
            return result;
        });
    }

    // GetTime 工具
    {
        Tool tool;
        tool.name = "get_time";
        tool.description = "Get current system time";
        tool.input_schema.properties = json::object();

        mcp.register_tool(tool, [](const json& /*args*/) -> ToolResult {
            std::time_t now = std::time(nullptr);
            char buf[100];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = buf
            });
            return result;
        });
    }

    // GetWeather 工具
    {
        Tool tool;
        tool.name = "get_weather";
        tool.description = "Get weather information for a city";
        tool.input_schema.properties = {
            {"city", {{"type", "string"}, {"description", "City name (e.g., Beijing, Shanghai)"}}}
        };
        tool.input_schema.required = {"city"};

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string city = args.at("city").get<std::string>();

            // 模拟天气数据（实际应用中应该调用真实的天气 API）
            std::time_t now = std::time(nullptr);
            char time_buf[100];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

            std::ostringstream weather_info;
            weather_info << "Weather Report for " << city << "\n";
            weather_info << "========================\n";
            weather_info << "Time: " << time_buf << "\n";
            weather_info << "Temperature: 22°C\n";
            weather_info << "Condition: Sunny\n";
            weather_info << "Humidity: 45%\n";
            weather_info << "Wind: 5 km/h NE\n";
            weather_info << "\n(Note: This is simulated data. Integrate with real weather API for production)";

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = weather_info.str()
            });
            return result;
        });
    }

    // WriteFile 工具
    {
        Tool tool;
        tool.name = "write_file";
        tool.description = "Write content to a file";
        tool.input_schema.properties = {
            {"path", {{"type", "string"}, {"description", "File path to write to"}}},
            {"content", {{"type", "string"}, {"description", "Content to write to the file"}}}
        };
        tool.input_schema.required = {"path", "content"};

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string path = args.at("path").get<std::string>();
            std::string content = args.at("content").get<std::string>();

            ToolResult result;

            try {
                std::ofstream file(path);
                if (!file.is_open()) {
                    result.is_error = true;
                    result.content.push_back(ContentItem{
                        .type = "text",
                        .text = "Error: Failed to open file: " + path
                    });
                    return result;
                }

                file << content;
                file.close();

                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = "Successfully wrote to file: " + path
                });
            } catch (const std::exception& e) {
                result.is_error = true;
                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = std::string("Error writing file: ") + e.what()
                });
            }

            return result;
        });
    }

    // ===== 注册资源 =====

    // 系统信息
    {
        Resource res;
        res.uri = "system://info";
        res.name = "System Information";
        res.description = "Basic system information";
        res.mime_type = "text/plain";

        mcp.register_resource(res, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "text/plain";

            std::ostringstream oss;
            oss << "MCP Server - System Info\n";
            oss << "========================\n";
            std::time_t now = std::time(nullptr);
            char buf[100];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            oss << "Time: " << buf << "\n";

            content.text = oss.str();
            return content;
        });
    }

    // 服务器配置
    {
        Resource res;
        res.uri = "config://server";
        res.name = "Server Configuration";
        res.mime_type = "application/json";

        mcp.register_resource(res, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "application/json";
            content.text = json({
                {"port", MCP_CONFIG.GetServerPort()},
                {"log_level", MCP_CONFIG.GetLogLevel()}
            }).dump(2);
            return content;
        });
    }

    // ===== 注册提示词 =====

    // 代码审查
    {
        Prompt prompt;
        prompt.name = "code_review";
        prompt.description = "Generate code review prompt";
        prompt.arguments.push_back(PromptArgument{.name = "code", .required = true});
        prompt.arguments.push_back(PromptArgument{.name = "language", .required = true});

        mcp.register_prompt(prompt, [](const json& args) -> std::vector<PromptMessage> {
            std::vector<PromptMessage> msgs;
            PromptMessage msg;
            msg.role = Role::User;
            msg.content = {
                {"type", "text"},
                {"text", "Please review this " + args.at("language").get<std::string>() +
                         " code:\n\n" + args.at("code").get<std::string>()}
            };
            msgs.push_back(msg);
            return msgs;
        });
    }

    MCP_LOG_INFO("MCP setup complete: {} tools, {} resources, {} prompts",
                 mcp.list_tools().size(), mcp.list_resources().size(), mcp.list_prompts().size());
}

// 创建 JSON-RPC 调度器（绑定到 MCP 服务器）
JsonRpcDispatcher create_dispatcher(McpServer& mcp_server) {
    JsonRpcDispatcher dispatcher;

    // initialize
    dispatcher.registerHandler("initialize", [&mcp_server](const json& /*params*/) -> json {
        MCP_LOG_INFO("Client initialized");
        return mcp_server.get_initialize_result().to_json();
    });

    // tools/list
    dispatcher.registerHandler("tools/list", [&mcp_server](const json& /*params*/) -> json {
        json tools_arr = json::array();
        for (const auto& tool : mcp_server.list_tools()) {
            tools_arr.push_back(tool.to_json());
        }
        return {{"tools", tools_arr}};
    });

    // tools/call
    dispatcher.registerHandler("tools/call", [&mcp_server](const json& params) -> json {
        std::string name = params.at("name").get<std::string>();
        json arguments = params.value("arguments", json::object());
        MCP_LOG_INFO("Calling tool: {}", name);
        auto result = mcp_server.call_tool(name, arguments);
        return result.to_json();
    });

    // resources/list
    dispatcher.registerHandler("resources/list", [&mcp_server](const json& /*params*/) -> json {
        json resources_arr = json::array();
        for (const auto& resource : mcp_server.list_resources()) {
            resources_arr.push_back(resource.to_json());
        }
        return {{"resources", resources_arr}};
    });

    // resources/read
    dispatcher.registerHandler("resources/read", [&mcp_server](const json& params) -> json {
        std::string uri = params.at("uri").get<std::string>();
        MCP_LOG_INFO("Reading resource: {}", uri);
        auto content = mcp_server.read_resource(uri);
        json contents_arr = json::array();
        contents_arr.push_back(content.to_json());
        return {{"contents", contents_arr}};
    });

    // prompts/list
    dispatcher.registerHandler("prompts/list", [&mcp_server](const json& /*params*/) -> json {
        json prompts_arr = json::array();
        for (const auto& prompt : mcp_server.list_prompts()) {
            prompts_arr.push_back(prompt.to_json());
        }
        return {{"prompts", prompts_arr}};
    });

    // prompts/get
    dispatcher.registerHandler("prompts/get", [&mcp_server](const json& params) -> json {
        std::string name = params.at("name").get<std::string>();
        json arguments = params.value("arguments", json::object());
        MCP_LOG_INFO("Getting prompt: {}", name);
        auto messages = mcp_server.get_prompt(name, arguments);
        json messages_arr = json::array();
        for (const auto& msg : messages) {
            messages_arr.push_back(msg.to_json());
        }
        return {{"messages", messages_arr}};
    });

    return dispatcher;
}

// HTTP 模式
void run_http_mode(McpServer& mcp_server, const std::string& host, int port) {
    MCP_LOG_INFO("Starting HTTP server on {}:{}", host, port);

    auto dispatcher = create_dispatcher(mcp_server);
    g_http_server = std::make_unique<HttpJsonRpcServer>(std::move(dispatcher), host, port);

    // 启动服务器（阻塞）
    g_http_server->run();

    MCP_LOG_INFO("HTTP server stopped");
}

// stdio 模式
void run_stdio_mode(McpServer& mcp_server) {
    MCP_LOG_INFO("Starting stdio server");

    auto dispatcher = create_dispatcher(mcp_server);
    StdioJsonRpcServer stdio_server(std::move(dispatcher));

    // 启动服务器（阻塞）
    stdio_server.run();

    MCP_LOG_INFO("stdio server stopped");
}

// 同时运行两种模式
void run_both_modes(McpServer& mcp_server, const std::string& host, int port) {
    MCP_LOG_INFO("Starting both HTTP and stdio servers");

    // 在独立线程中运行 HTTP 服务器
    std::thread http_thread([&mcp_server, &host, port]() {
        run_http_mode(mcp_server, host, port);
    });

    // 在主线程运行 stdio 服务器
    run_stdio_mode(mcp_server);

    // 等待 HTTP 线程结束
    if (http_thread.joinable()) {
        http_thread.join();
    }
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file = "../../config/server.json";
    std::string mode = "http";  // 默认 HTTP 模式
    std::string host;
    int port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --mode MODE      Server mode: http, stdio, or both (default: http)\n"
                      << "  --config FILE    Configuration file path\n"
                      << "  --host HOST      Server host (for HTTP mode)\n"
                      << "  --port PORT      Server port (for HTTP mode)\n"
                      << "  --help, -h       Show this help\n"
                      << "\nExamples:\n"
                      << "  " << argv[0] << " --mode http --port 8080\n"
                      << "  " << argv[0] << " --mode stdio\n"
                      << "  " << argv[0] << " --mode both --port 8080\n";
            return 0;
        }
    }

    // 验证模式
    if (mode != "http" && mode != "stdio" && mode != "both") {
        std::cerr << "Invalid mode: " << mode << " (must be http, stdio, or both)" << std::endl;
        return 1;
    }

    // 加载配置
    if (!MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
    }

    if (host.empty()) {
        host = "0.0.0.0";
    }
    if (port == 0) {
        port = MCP_CONFIG.GetServerPort();
    }

    // 初始化日志
    MCP_LOG_INIT("mcp_server", MCP_CONFIG.GetLogFilePath(),
                 MCP_CONFIG.GetLogFileSize(), MCP_CONFIG.GetLogFileCount(),
                 MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(MCP_CONFIG.GetLogLevel()));

    MCP_LOG_INFO("=== MCP Server ===");
    MCP_LOG_INFO("Mode: {}", mode);
    if (mode == "http" || mode == "both") {
        MCP_LOG_INFO("HTTP: {}:{}", host, port);
    }

    try {
        // 创建 MCP 服务器实例
        McpServer mcp_server("mcp-server", "1.0.0");

        // 设置能力
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false}; // 工具能力
        capabilities.resources = ServerCapabilities::ResourcesCapability{false, false}; // 资源能力
        capabilities.prompts = ServerCapabilities::PromptsCapability{false}; // 提示能力
        mcp_server.set_capabilities(capabilities);

        // 注册 Tools, Resources, Prompts
        setup_mcp_server(mcp_server);

        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 根据模式启动服务器
        if (mode == "http") {
            run_http_mode(mcp_server, host, port);
        } else if (mode == "stdio") {
            run_stdio_mode(mcp_server);
        } else if (mode == "both") {
            run_both_modes(mcp_server, host, port);
        }

        MCP_LOG_INFO("Server shutdown complete");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}
