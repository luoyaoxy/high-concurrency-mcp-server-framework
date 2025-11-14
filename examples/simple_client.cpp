/**
 * @file simple_client.cpp
 * @brief 简单的 MCP JSON-RPC 客户端示例
 * 向 stdio 服务端发送 JSON-RPC 请求
 */

#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief 发送一个带 Content-Length 头的 JSON-RPC 消息
 */
void sendJsonRpcMessage(const json& message) {
    std::string payload = message.dump();

    // 发送 Content-Length 头
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n";

    // 发送消息体
    std::cout << payload << std::flush;
}

/**
 * @brief 从 stdin 读取带 Content-Length 头的响应
 */
json readJsonRpcResponse() {
    std::string line;
    size_t content_length = 0;
    bool found_length = false;

    // 读取头部
    while (std::getline(std::cin, line)) {
        // 去除 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // 空行表示头部结束
        if (line.empty()) {
            break;
        }

        // 解析 Content-Length
        if (line.find("Content-Length:") == 0) {
            std::string len_str = line.substr(15);
            // 去除前导空格
            size_t pos = len_str.find_first_not_of(' ');
            if (pos != std::string::npos) {
                len_str = len_str.substr(pos);
            }
            content_length = std::stoull(len_str);
            found_length = true;
        }
    }

    if (!found_length || content_length == 0) {
        return json();
    }

    // 读取消息体
    std::string body(content_length, '\0');
    std::cin.read(&body[0], content_length);

    return json::parse(body);
}

int main(int argc, char* argv[]) {

    // 示例 1: 发送 initialize 请求
    {
        std::cerr << "[客户端] 发送 initialize 请求..." << std::endl;
        json init_request = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2024-11-05"},
                {"clientInfo", {
                    {"name", "simple-client"},
                    {"version", "1.0.0"}
                }}
            }}
        };

        sendJsonRpcMessage(init_request);

        // 读取响应
        json response = readJsonRpcResponse();
        std::cerr << "[客户端] 收到响应: " << response.dump(2) << std::endl;
        std::cerr << std::endl;
    }

    // 示例 2: 发送 echo 请求
    {
        std::cerr << "[客户端] 发送 echo 请求..." << std::endl;
        json echo_request = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "echo"},
            {"params", {
                {"message", "Hello from client!"},
                {"timestamp", "2024-11-13"}
            }}
        };

        sendJsonRpcMessage(echo_request);

        // 读取响应
        json response = readJsonRpcResponse();
        std::cerr << "[客户端] 收到响应: " << response.dump(2) << std::endl;
        std::cerr << std::endl;
    }

    // 示例 3: 发送一个不存在的方法（测试错误处理）
    {
        std::cerr << "[客户端] 发送不存在的方法请求..." << std::endl;
        json invalid_request = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "nonexistent_method"},
            {"params", {}}
        };

        sendJsonRpcMessage(invalid_request);

        // 读取响应
        json response = readJsonRpcResponse();
        std::cerr << "[客户端] 收到响应: " << response.dump(2) << std::endl;
        std::cerr << std::endl;
    }

    std::cerr << "[客户端] 所有请求已发送完毕" << std::endl;

    return 0;
}
