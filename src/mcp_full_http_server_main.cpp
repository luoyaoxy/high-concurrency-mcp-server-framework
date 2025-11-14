/**
 * @file mcp_http_server_main.cpp
 * @brief 完整的 MCP HTTP 服务器 - 支持 Tools/Resources/Prompts
 *
 * 使用方法:
 *   ./mcp_full_http_server [--host HOST] [--port PORT] [--config CONFIG_FILE]
 */

#include "config.h"
#include "logger.h"
#include "http_jsonrpc.h"
#include "mcp_server.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <fstream>
#include <sstream>
#include <ctime>

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
    if (level_str == "trace") return spdlog::level::trace;
    if (level_str == "debug") return spdlog::level::debug;
    if (level_str == "info") return spdlog::level::info;
    if (level_str == "warn") return spdlog::level::warn;
    if (level_str == "error") return spdlog::level::err;
    if (level_str == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

/**
 * @brief 注册 MCP 工具
 */
void register_mcp_tools(McpServer& mcp) {
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
}

/**
 * @brief 注册 MCP 资源
 */
void register_mcp_resources(McpServer& mcp) {
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
}

/**
 * @brief 注册 MCP 提示词
 */
void register_mcp_prompts(McpServer& mcp) {
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
                      << "  --config FILE    Configuration file path\n"
                      << "  --host HOST      Server host\n"
                      << "  --port PORT      Server port\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

    // 加载配置
    if (!MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
    }

    if (host.empty()) host = "0.0.0.0";
    if (port == 0) port = MCP_CONFIG.GetServerPort();

    // 初始化日志
    MCP_LOG_INIT("mcp_full_http_server", MCP_CONFIG.GetLogFilePath(),
                 MCP_CONFIG.GetLogFileSize(), MCP_CONFIG.GetLogFileCount(),
                 MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(MCP_CONFIG.GetLogLevel()));

    MCP_LOG_INFO("=== MCP Full HTTP Server ===");
    MCP_LOG_INFO("Host: {}, Port: {}", host, port);

    try {
        // 创建 MCP 服务器实例
        McpServer mcp_server("mcp-full-http-server", "1.0.0");

        // 设置能力
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false};
        capabilities.resources = ServerCapabilities::ResourcesCapability{false, false};
        capabilities.prompts = ServerCapabilities::PromptsCapability{false};
        mcp_server.set_capabilities(capabilities);

        // 注册 Tools, Resources, Prompts
        register_mcp_tools(mcp_server);
        register_mcp_resources(mcp_server);
        register_mcp_prompts(mcp_server);

        MCP_LOG_INFO("Registered {} tools", mcp_server.list_tools().size());
        MCP_LOG_INFO("Registered {} resources", mcp_server.list_resources().size());
        MCP_LOG_INFO("Registered {} prompts", mcp_server.list_prompts().size());

        // 创建 JSON-RPC 调度器
        JsonRpcDispatcher dispatcher;

        // 注册 initialize 方法
        dispatcher.registerHandler("initialize", [&mcp_server](const json& /*params*/) -> json {
            MCP_LOG_INFO("Client initialized");
            return mcp_server.get_initialize_result().to_json();
        });

        // 注册 tools/list 方法
        dispatcher.registerHandler("tools/list", [&mcp_server](const json& /*params*/) -> json {
            json tools_arr = json::array();
            for (const auto& tool : mcp_server.list_tools()) {
                tools_arr.push_back(tool.to_json());
            }
            return {{"tools", tools_arr}};
        });

        // 注册 tools/call 方法
        dispatcher.registerHandler("tools/call", [&mcp_server](const json& params) -> json {
            std::string name = params.at("name").get<std::string>();
            json arguments = params.value("arguments", json::object());

            MCP_LOG_INFO("Calling tool: {}", name);
            auto result = mcp_server.call_tool(name, arguments);
            return result.to_json();
        });

        // 注册 resources/list 方法
        dispatcher.registerHandler("resources/list", [&mcp_server](const json& /*params*/) -> json {
            json resources_arr = json::array();
            for (const auto& resource : mcp_server.list_resources()) {
                resources_arr.push_back(resource.to_json());
            }
            return {{"resources", resources_arr}};
        });

        // 注册 resources/read 方法
        dispatcher.registerHandler("resources/read", [&mcp_server](const json& params) -> json {
            std::string uri = params.at("uri").get<std::string>();

            MCP_LOG_INFO("Reading resource: {}", uri);
            auto content = mcp_server.read_resource(uri);

            json contents_arr = json::array();
            contents_arr.push_back(content.to_json());
            return {{"contents", contents_arr}};
        });

        // 注册 prompts/list 方法
        dispatcher.registerHandler("prompts/list", [&mcp_server](const json& /*params*/) -> json {
            json prompts_arr = json::array();
            for (const auto& prompt : mcp_server.list_prompts()) {
                prompts_arr.push_back(prompt.to_json());
            }
            return {{"prompts", prompts_arr}};
        });

        // 注册 prompts/get 方法
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

        // 创建 HTTP 服务器
        HttpJsonRpcServer server = (host != "0.0.0.0" || port != MCP_CONFIG.GetServerPort())
            ? HttpJsonRpcServer(std::move(dispatcher), host, port)
            : HttpJsonRpcServer(std::move(dispatcher));
        g_server.store(&server);

        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 启动服务器
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
