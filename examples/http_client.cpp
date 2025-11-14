/**
 * @file http_client.cpp
 * @brief 简单的 HTTP JSON-RPC 客户端示例
 *
 * 使用方法:
 *   ./http_client [--host HOST] [--port PORT]
 */

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;

/**
 * @brief 发送 JSON-RPC 请求并接收响应
 */
json sendJsonRpcRequest(httplib::Client& client, const json& request) {
    std::string request_str = request.dump();

    std::cout << "\n[客户端] 发送请求:" << std::endl;
    std::cout << request.dump(2) << std::endl;

    auto res = client.Post("/jsonrpc", request_str, "application/json");

    if (!res) {
        throw std::runtime_error("HTTP request failed: " + httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        throw std::runtime_error("HTTP error: " + std::to_string(res->status) + " " + res->reason);
    }

    std::cout << "[客户端] 收到响应:" << std::endl;
    json response = json::parse(res->body);
    std::cout << response.dump(2) << std::endl;

    return response;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string host = "localhost";
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --host HOST      Server host (default: localhost)\n"
                      << "  --port PORT      Server port (default: 8080)\n"
                      << "  --help, -h       Show this help message\n";
            return 0;
        }
    }

    std::cout << "=== MCP HTTP JSON-RPC Client ===" << std::endl;
    std::cout << "Connecting to http://" << host << ":" << port << std::endl;

    try {
        // 创建 HTTP 客户端
        httplib::Client client(host, port);
        client.set_read_timeout(5, 0);   // 5 秒超时
        client.set_write_timeout(5, 0);  // 5 秒超时

        // 1. 检查服务器信息
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "1. 获取服务器信息" << std::endl;
            std::cout << "========================================" << std::endl;

            auto res = client.Get("/");
            if (res && res->status == 200) {
                json info = json::parse(res->body);
                std::cout << info.dump(2) << std::endl;
            }
        }

        // 2. 健康检查
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "2. 健康检查" << std::endl;
            std::cout << "========================================" << std::endl;

            auto res = client.Get("/health");
            if (res && res->status == 200) {
                json health = json::parse(res->body);
                std::cout << health.dump(2) << std::endl;
            }
        }

        // 3. Initialize 请求
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "3. Initialize 请求" << std::endl;
            std::cout << "========================================" << std::endl;

            json init_request = {
                {"jsonrpc", "2.0"},
                {"id", 1},
                {"method", "initialize"},
                {"params", {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", json::object()},
                    {"clientInfo", {
                        {"name", "http-test-client"},
                        {"version", "1.0.0"}
                    }}
                }}
            };

            sendJsonRpcRequest(client, init_request);
        }

        // 4. Echo 请求
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "4. Echo 请求" << std::endl;
            std::cout << "========================================" << std::endl;

            json echo_request = {
                {"jsonrpc", "2.0"},
                {"id", 2},
                {"method", "echo"},
                {"params", {
                    {"message", "Hello, MCP!"},
                    {"timestamp", "2024-11-13"}
                }}
            };

            sendJsonRpcRequest(client, echo_request);
        }

        // 5. 列出工具
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "5. 列出可用工具" << std::endl;
            std::cout << "========================================" << std::endl;

            json tools_list_request = {
                {"jsonrpc", "2.0"},
                {"id", 3},
                {"method", "tools/list"},
                {"params", json::object()}
            };

            sendJsonRpcRequest(client, tools_list_request);
        }

        // 6. 调用工具
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "6. 调用 echo 工具" << std::endl;
            std::cout << "========================================" << std::endl;

            json tools_call_request = {
                {"jsonrpc", "2.0"},
                {"id", 4},
                {"method", "tools/call"},
                {"params", {
                    {"name", "echo"},
                    {"arguments", {
                        {"message", "工具调用成功！"}
                    }}
                }}
            };

            sendJsonRpcRequest(client, tools_call_request);
        }

        // 7. 测试批量请求
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "7. 批量请求" << std::endl;
            std::cout << "========================================" << std::endl;

            json batch_request = json::array({
                {
                    {"jsonrpc", "2.0"},
                    {"id", 5},
                    {"method", "echo"},
                    {"params", {{"msg", "batch 1"}}}
                },
                {
                    {"jsonrpc", "2.0"},
                    {"id", 6},
                    {"method", "echo"},
                    {"params", {{"msg", "batch 2"}}}
                }
            });

            sendJsonRpcRequest(client, batch_request);
        }

        // 8. 测试错误处理（调用不存在的方法）
        {
            std::cout << "\n========================================" << std::endl;
            std::cout << "8. 错误处理测试（调用不存在的方法）" << std::endl;
            std::cout << "========================================" << std::endl;

            json error_request = {
                {"jsonrpc", "2.0"},
                {"id", 7},
                {"method", "nonexistent_method"},
                {"params", json::object()}
            };

            try {
                sendJsonRpcRequest(client, error_request);
            } catch (const std::exception& e) {
                std::cout << "[客户端] 预期的错误: " << e.what() << std::endl;
            }
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "所有测试完成！" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
