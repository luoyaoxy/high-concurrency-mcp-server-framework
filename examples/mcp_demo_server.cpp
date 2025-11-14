/**
 * @file mcp_demo_server.cpp
 * @brief 完整的 MCP 服务器示例
 *
 * 展示如何使用 MCP 服务器注册和使用 Tools、Resources、Prompts
 */

#include "mcp_server.h"
#include "logger.h"
#include "config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>

using namespace mcp;

/**
 * @brief 注册示例工具
 */
void register_tools(McpServer& server) {
    // 1. Echo 工具 - 简单回显输入
    {
        Tool echo_tool;
        echo_tool.name = "echo";
        echo_tool.description = "Echo back the input message";
        echo_tool.input_schema.type = "object";
        echo_tool.input_schema.properties = {
            {"message", {{"type", "string"}, {"description", "The message to echo"}}}
        };
        echo_tool.input_schema.required = {"message"};

        server.register_tool(echo_tool, [](const json& args) -> ToolResult {
            std::string message = args.at("message").get<std::string>();

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = "Echo: " + message
            });
            return result;
        });

        MCP_LOG_INFO("Registered tool: echo");
    }

    // 2. Add 工具 - 计算两个数字的和
    {
        Tool add_tool;
        add_tool.name = "add";
        add_tool.description = "Add two numbers together";
        add_tool.input_schema.type = "object";
        add_tool.input_schema.properties = {
            {"a", {{"type", "number"}, {"description", "First number"}}},
            {"b", {{"type", "number"}, {"description", "Second number"}}}
        };
        add_tool.input_schema.required = {"a", "b"};

        server.register_tool(add_tool, [](const json& args) -> ToolResult {
            double a = args.at("a").get<double>();
            double b = args.at("b").get<double>();
            double sum = a + b;

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = std::to_string(a) + " + " + std::to_string(b) + " = " + std::to_string(sum)
            });
            return result;
        });

        MCP_LOG_INFO("Registered tool: add");
    }

    // 3. GetTime 工具 - 获取当前时间
    {
        Tool time_tool;
        time_tool.name = "get_time";
        time_tool.description = "Get current system time";
        time_tool.input_schema.type = "object";
        time_tool.input_schema.properties = json::object();
        time_tool.input_schema.required = {};

        server.register_tool(time_tool, [](const json& /*args*/) -> ToolResult {
            std::time_t now = std::time(nullptr);
            char buf[100];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = std::string("Current time: ") + buf
            });
            return result;
        });

        MCP_LOG_INFO("Registered tool: get_time");
    }
}

/**
 * @brief 注册示例资源
 */
void register_resources(McpServer& server) {
    // 1. 系统信息资源
    {
        Resource system_info;
        system_info.uri = "system://info";
        system_info.name = "System Information";
        system_info.description = "Basic system information";
        system_info.mime_type = "text/plain";

        server.register_resource(system_info, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "text/plain";

            std::ostringstream oss;
            oss << "System Information\n";
            oss << "==================\n";
            oss << "OS: " <<
#ifdef __APPLE__
                "macOS"
#elif __linux__
                "Linux"
#elif _WIN32
                "Windows"
#else
                "Unknown"
#endif
                << "\n";

            std::time_t now = std::time(nullptr);
            char buf[100];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            oss << "Current Time: " << buf << "\n";

            content.text = oss.str();
            return content;
        });

        MCP_LOG_INFO("Registered resource: {}", system_info.uri);
    }

    // 2. 配置文件资源
    {
        Resource config_resource;
        config_resource.uri = "config://server";
        config_resource.name = "Server Configuration";
        config_resource.description = "Current server configuration";
        config_resource.mime_type = "application/json";

        server.register_resource(config_resource, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "application/json";

            json config = {
                {"server", {
                    {"port", MCP_CONFIG.GetServerPort()},
                    {"log_level", MCP_CONFIG.GetLogLevel()}
                }},
                {"logging", {
                    {"file_path", MCP_CONFIG.GetLogFilePath()},
                    {"console_output", MCP_CONFIG.GetLogConsoleOutput()}
                }}
            };

            content.text = config.dump(2);
            return content;
        });

        MCP_LOG_INFO("Registered resource: {}", config_resource.uri);
    }

    // 3. README 文件资源
    {
        Resource readme;
        readme.uri = "file:///README.md";
        readme.name = "Project README";
        readme.description = "Project documentation";
        readme.mime_type = "text/markdown";

        server.register_resource(readme, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "text/markdown";
            content.text = R"(# MCP Demo Server

This is a demonstration of the MCP (Model Context Protocol) server implementation in C++.

## Features

- **Tools**: Callable functions that can be invoked by clients
- **Resources**: Readable data sources (files, system info, etc.)
- **Prompts**: Template-based prompt generation

## Usage

Connect to this server using an MCP client and explore the available tools and resources.
)";
            return content;
        });

        MCP_LOG_INFO("Registered resource: {}", readme.uri);
    }
}

/**
 * @brief 注册示例提示词
 */
void register_prompts(McpServer& server) {
    // 1. 代码审查提示词
    {
        Prompt code_review;
        code_review.name = "code_review";
        code_review.description = "Generate a code review prompt";

        PromptArgument lang_arg;
        lang_arg.name = "language";
        lang_arg.description = "Programming language";
        lang_arg.required = true;
        code_review.arguments.push_back(lang_arg);

        PromptArgument code_arg;
        code_arg.name = "code";
        code_arg.description = "Code to review";
        code_arg.required = true;
        code_review.arguments.push_back(code_arg);

        server.register_prompt(code_review, [](const json& args) -> std::vector<PromptMessage> {
            std::string language = args.at("language").get<std::string>();
            std::string code = args.at("code").get<std::string>();

            std::vector<PromptMessage> messages;

            // System message
            PromptMessage system_msg;
            system_msg.role = Role::User;
            system_msg.content = {
                {"type", "text"},
                {"text", "You are an expert code reviewer. Please review the following code and provide feedback on:\n"
                         "1. Code quality and best practices\n"
                         "2. Potential bugs or issues\n"
                         "3. Performance considerations\n"
                         "4. Suggestions for improvement"}
            };
            messages.push_back(system_msg);

            // Code to review
            PromptMessage code_msg;
            code_msg.role = Role::User;
            code_msg.content = {
                {"type", "text"},
                {"text", "Language: " + language + "\n\nCode:\n```" + language + "\n" + code + "\n```"}
            };
            messages.push_back(code_msg);

            return messages;
        });

        MCP_LOG_INFO("Registered prompt: {}", code_review.name);
    }

    // 2. 翻译提示词
    {
        Prompt translate;
        translate.name = "translate";
        translate.description = "Generate a translation prompt";

        PromptArgument text_arg;
        text_arg.name = "text";
        text_arg.description = "Text to translate";
        text_arg.required = true;
        translate.arguments.push_back(text_arg);

        PromptArgument target_lang_arg;
        target_lang_arg.name = "target_language";
        target_lang_arg.description = "Target language";
        target_lang_arg.required = true;
        translate.arguments.push_back(target_lang_arg);

        server.register_prompt(translate, [](const json& args) -> std::vector<PromptMessage> {
            std::string text = args.at("text").get<std::string>();
            std::string target_lang = args.at("target_language").get<std::string>();

            std::vector<PromptMessage> messages;

            PromptMessage msg;
            msg.role = Role::User;
            msg.content = {
                {"type", "text"},
                {"text", "Please translate the following text to " + target_lang + ":\n\n" + text}
            };
            messages.push_back(msg);

            return messages;
        });

        MCP_LOG_INFO("Registered prompt: {}", translate.name);
    }

    // 3. 总结提示词
    {
        Prompt summarize;
        summarize.name = "summarize";
        summarize.description = "Generate a summarization prompt";

        PromptArgument content_arg;
        content_arg.name = "content";
        content_arg.description = "Content to summarize";
        content_arg.required = true;
        summarize.arguments.push_back(content_arg);

        PromptArgument max_length_arg;
        max_length_arg.name = "max_length";
        max_length_arg.description = "Maximum summary length in words";
        max_length_arg.required = false;
        summarize.arguments.push_back(max_length_arg);

        server.register_prompt(summarize, [](const json& args) -> std::vector<PromptMessage> {
            std::string content = args.at("content").get<std::string>();
            std::string max_length = args.value("max_length", "100");

            std::vector<PromptMessage> messages;

            PromptMessage msg;
            msg.role = Role::User;
            msg.content = {
                {"type", "text"},
                {"text", "Please summarize the following content in no more than " + max_length + " words:\n\n" + content}
            };
            messages.push_back(msg);

            return messages;
        });

        MCP_LOG_INFO("Registered prompt: {}", summarize.name);
    }
}

int main() {
    // 初始化日志系统
    MCP_LOG_INIT("mcp_demo", "../../logs/mcp_demo.log", 10485760, 3, true);
    MCP_LOG_SET_LEVEL(spdlog::level::info);

    MCP_LOG_INFO("=== MCP Demo Server ===");
    MCP_LOG_INFO("Starting MCP demo server...");

    try {
        // 创建 MCP 服务器
        McpServer server("mcp-demo-server", "1.0.0");

        // 设置服务器能力
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false};
        capabilities.resources = ServerCapabilities::ResourcesCapability{false, false};
        capabilities.prompts = ServerCapabilities::PromptsCapability{false};
        server.set_capabilities(capabilities);

        // 注册 Tools、Resources、Prompts
        register_tools(server);
        register_resources(server);
        register_prompts(server);

        MCP_LOG_INFO("Server setup complete!");
        MCP_LOG_INFO("Registered {} tools", server.list_tools().size());
        MCP_LOG_INFO("Registered {} resources", server.list_resources().size());
        MCP_LOG_INFO("Registered {} prompts", server.list_prompts().size());

        // 演示功能
        std::cout << "\n=== MCP Server Demo ===\n\n";

        // 1. 测试 Tools
        std::cout << "1. Testing Tools:\n";
        std::cout << "   - Available tools: ";
        for (const auto& tool : server.list_tools()) {
            std::cout << tool.name << " ";
        }
        std::cout << "\n";

        // 调用 echo 工具
        json echo_args = {{"message", "Hello, MCP!"}};
        auto echo_result = server.call_tool("echo", echo_args);
        std::cout << "   - Echo result: " << echo_result.content[0].text.value() << "\n";

        // 调用 add 工具
        json add_args = {{"a", 10}, {"b", 32}};
        auto add_result = server.call_tool("add", add_args);
        std::cout << "   - Add result: " << add_result.content[0].text.value() << "\n";

        // 2. 测试 Resources
        std::cout << "\n2. Testing Resources:\n";
        std::cout << "   - Available resources: ";
        for (const auto& resource : server.list_resources()) {
            std::cout << resource.uri << " ";
        }
        std::cout << "\n";

        // 读取系统信息资源
        auto sys_info = server.read_resource("system://info");
        std::cout << "   - System info:\n";
        std::cout << sys_info.text << "\n";

        // 3. 测试 Prompts
        std::cout << "\n3. Testing Prompts:\n";
        std::cout << "   - Available prompts: ";
        for (const auto& prompt : server.list_prompts()) {
            std::cout << prompt.name << " ";
        }
        std::cout << "\n";

        // 获取翻译提示词
        json translate_args = {
            {"text", "Hello, World!"},
            {"target_language", "Chinese"}
        };
        auto translate_messages = server.get_prompt("translate", translate_args);
        std::cout << "   - Translate prompt message: "
                  << translate_messages[0].content.dump() << "\n";

        std::cout << "\n=== Demo Complete ===\n";
        MCP_LOG_INFO("Demo completed successfully");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Error: {}", e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}
