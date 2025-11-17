/**
 * @file client_demo.cpp
 * @brief MCP 客户端 SDK 使用示例
 *
 * 演示如何使用 McpClient 调用 MCP 服务器
 */

#include "mcp_client.h"
#include <iostream>
#include <iomanip>

using namespace mcp;

void print_separator(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

int main() {
    try {
        // 创建客户端（连接到 localhost:8089）
        McpClient client("localhost", 8089);

        print_separator("1. 初始化连接");
        auto init_result = client.initialize();
        std::cout << "服务器名称: " << init_result.server_info.name << "\n";
        std::cout << "服务器版本: " << init_result.server_info.version << "\n";

        // ===== 测试 Tools =====
        print_separator("2. 列出所有工具");
        auto tools = client.list_tools();
        std::cout << "可用工具数量: " << tools.size() << "\n\n";

        for (const auto& tool : tools) {
            std::cout << "  - " << tool.name << ": " << tool.description << "\n";
        }

        // 调用 echo 工具
        print_separator("3. 调用 echo 工具");
        auto echo_result = client.call_tool("echo", {{"message", "Hello from MCP Client SDK!"}});
        if (!echo_result.is_error && !echo_result.content.empty()) {
            std::cout << "Echo 返回: " << echo_result.content[0].text.value() << "\n";
        }

        // 调用 calculate 工具
        print_separator("4. 调用 calculate 工具");
        auto calc_result = client.call_tool("calculate", {
            {"operation", "add"},
            {"a", 123},
            {"b", 456}
        });
        if (!calc_result.is_error && !calc_result.content.empty()) {
            std::cout << "123 + 456 = " << calc_result.content[0].text.value() << "\n";
        }

        // 调用 get_time 工具
        print_separator("5. 调用 get_time 工具");
        auto time_result = client.call_tool("get_time", json::object());
        if (!time_result.is_error && !time_result.content.empty()) {
            std::cout << "当前时间: " << time_result.content[0].text.value() << "\n";
        }

        // 调用 get_weather 工具
        print_separator("6. 调用 get_weather 工具");
        auto weather_result = client.call_tool("get_weather", {{"city", "Beijing"}});
        if (!weather_result.is_error && !weather_result.content.empty()) {
            std::cout << weather_result.content[0].text.value() << "\n";
        }

        // 调用 write_file 工具
        print_separator("7. 调用 write_file 工具");
        auto write_result = client.call_tool("write_file", {
            {"path", "/tmp/mcp_client_test.txt"},
            {"content", "This file was created by MCP Client SDK!\nTimestamp: " + time_result.content[0].text.value()}
        });
        if (!write_result.is_error && !write_result.content.empty()) {
            std::cout << write_result.content[0].text.value() << "\n";
        }

        // ===== 测试 Resources =====
        print_separator("8. 列出所有资源");
        auto resources = client.list_resources();
        std::cout << "可用资源数量: " << resources.size() << "\n\n";

        for (const auto& res : resources) {
            std::cout << "  - " << res.uri << ": " << res.name << "\n";
        }

        // 读取系统信息资源
        print_separator("9. 读取 system://info 资源");
        auto sys_info = client.read_resource("system://info");
        std::cout << sys_info.text << "\n";

        // 读取配置资源
        print_separator("10. 读取 config://server 资源");
        auto config_info = client.read_resource("config://server");
        std::cout << config_info.text << "\n";

        // ===== 测试 Prompts =====
        print_separator("11. 列出所有提示词");
        auto prompts = client.list_prompts();
        std::cout << "可用提示词数量: " << prompts.size() << "\n\n";

        for (const auto& prompt : prompts) {
            std::cout << "  - " << prompt.name << ": " << prompt.description.value_or("(无描述)") << "\n";
        }

        // 获取 code_review 提示词
        print_separator("12. 获取 code_review 提示词");
        auto messages = client.get_prompt("code_review", {
            {"language", "C++"},
            {"code", "int add(int a, int b) { return a + b; }"}
        });

        std::cout << "生成的提示消息数量: " << messages.size() << "\n";
        for (const auto& msg : messages) {
            std::cout << "\n角色: " << (msg.role == Role::User ? "User" : "Assistant") << "\n";
            std::cout << "内容: " << msg.content["text"].get<std::string>() << "\n";
        }

        // ===== 完整流程演示：查天气并写文件 =====
        print_separator("13. 完整流程：查北京天气并写入文件");

        std::cout << "步骤 1: 查询北京天气...\n";
        auto beijing_weather = client.call_tool("get_weather", {{"city", "Beijing"}});

        std::cout << "步骤 2: 将天气信息写入文件...\n";
        auto write_weather = client.call_tool("write_file", {
            {"path", "/tmp/beijing_weather.txt"},
            {"content", beijing_weather.content[0].text.value()}
        });

        std::cout << "\n结果:\n";
        std::cout << "  " << write_weather.content[0].text.value() << "\n";
        std::cout << "  文件位置: /tmp/beijing_weather.txt\n";

        print_separator("测试完成");
        std::cout << "\n所有测试成功完成！\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n错误: " << e.what() << std::endl;
        return 1;
    }
}
