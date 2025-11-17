/**
 * @file mcp_client.h
 * @brief MCP 客户端 SDK - 方便其他程序调用 MCP 服务器
 */

#pragma once

#include "types.h"
#include <string>
#include <memory>
#include <vector>

namespace mcp {

// 抽象传输层接口
class Transport {
public:
    virtual ~Transport() = default;
    virtual json send(const json& request) = 0;
};

// HTTP 传输实现
class HttpTransport : public Transport {
public:
    HttpTransport(const std::string& host, int port);
    ~HttpTransport();

    json send(const json& request) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// MCP 客户端，用来调用 MCP 服务器的功能
class McpClient {
public:
    McpClient(const std::string& host, int port);

    ~McpClient();
    InitializeResult initialize();

    std::vector<Tool> list_tools();

    ToolResult call_tool(const std::string& name, const json& arguments);

    std::vector<Resource> list_resources();

    ResourceContent read_resource(const std::string& uri);

    std::vector<Prompt> list_prompts();

    std::vector<PromptMessage> get_prompt(const std::string& name, const json& arguments);

    std::string get_last_error() const;
    // 禁止拷贝，这是
    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;

private:
    json send_request(const std::string& method, const json& params);

    std::unique_ptr<Transport> transport_;
    int request_id_ = 0;
    std::string last_error_;
};

} // namespace mcp
