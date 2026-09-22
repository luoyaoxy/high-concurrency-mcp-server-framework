/**
 * @file mcp_client.cpp
 * @brief MCP 客户端实现
 */

#include "mcp_client.h"

#include "../json_rpc/jsonrpc.h"

#include <httplib.h>
#include <stdexcept>

namespace mcp {

// ===== HttpTransport 实现 =====

class HttpTransport::Impl {
public:
    Impl(const std::string& host, int port)
        : host_(host), port_(port), client_(host, port) {
        client_.set_connection_timeout(5, 0);  // 5 秒连接超时
        client_.set_read_timeout(30, 0);       // 30 秒读取超时
    }

    json send(const json& request) {
        auto res = client_.Post("/jsonrpc", request.dump(), "application/json");

        if (!res) {
            throw std::runtime_error("Failed to connect to MCP server at " +
                                   host_ + ":" + std::to_string(port_));
        }

        if (res->status != 200) {
            throw std::runtime_error("HTTP error: " + std::to_string(res->status));
        }

        try {
            return json::parse(res->body);
        } catch (const json::exception& e) {
            throw std::runtime_error("Failed to parse JSON response: " + std::string(e.what()));
        }
    }

private:
    std::string host_;
    int port_;
    httplib::Client client_;
};

HttpTransport::HttpTransport(const std::string& host, int port)
    : impl_(std::make_unique<Impl>(host, port)) {}

HttpTransport::~HttpTransport() = default;

json HttpTransport::send(const json& request) {
    return impl_->send(request);
}

// McpClient 实现
McpClient::McpClient(const std::string& host, int port) : transport_(std::make_unique<HttpTransport>(host, port)) {}

McpClient::~McpClient() = default;

json McpClient::send_request(const std::string& method, const json& params) {
    last_error_.clear();

    try {
        json request = {
            {"jsonrpc", kJsonRpcVersion},
            {"method", method},
            {"params", params},
            {"id", ++request_id_}
        };

        json response = transport_->send(request);

        // 检查是否有错误
        if (response.contains("error")) {
            last_error_ = response["error"]["message"].get<std::string>();
            throw std::runtime_error("MCP error: " + last_error_);
        }

        return response["result"];

    } catch (const std::exception& e) {
        last_error_ = e.what();
        throw;
    }
}

InitializeResult McpClient::initialize() {
    json result = send_request("initialize", {
        {"protocolVersion", kLatestLegacyProtocolVersion},
        {"capabilities", json::object()},
        {"clientInfo", {{"name", "mcp-client-sdk"}, {"version", "1.0.0"}}}
    });
    return InitializeResult::from_json(result);
}

std::vector<Tool> McpClient::list_tools() {
    json result = send_request("tools/list", json::object());

    std::vector<Tool> tools;
    for (const auto& tool_json : result["tools"]) {
        tools.push_back(Tool::from_json(tool_json));
    }
    return tools;
}

ToolResult McpClient::call_tool(const std::string& name, const json& arguments) {
    json params = {
        {"name", name},
        {"arguments", arguments}
    };

    json result = send_request("tools/call", params);
    return ToolResult::from_json(result);
}

std::vector<Resource> McpClient::list_resources() {
    json result = send_request("resources/list", json::object());

    std::vector<Resource> resources;
    for (const auto& res_json : result["resources"]) {
        resources.push_back(Resource::from_json(res_json));
    }
    return resources;
}

ResourceContent McpClient::read_resource(const std::string& uri) {
    json params = {{"uri", uri}};
    json result = send_request("resources/read", params);

    // 返回第一个内容项
    return ResourceContent::from_json(result["contents"][0]);
}

std::vector<Prompt> McpClient::list_prompts() {
    json result = send_request("prompts/list", json::object());

    std::vector<Prompt> prompts;
    for (const auto& prompt_json : result["prompts"]) {
        prompts.push_back(Prompt::from_json(prompt_json));
    }
    return prompts;
}

std::vector<PromptMessage> McpClient::get_prompt(const std::string& name, const json& arguments) {
    json params = {
        {"name", name},
        {"arguments", arguments}
    };

    json result = send_request("prompts/get", params);

    std::vector<PromptMessage> messages;
    for (const auto& msg_json : result["messages"]) {
        messages.push_back(PromptMessage::from_json(msg_json));
    }
    return messages;
}

std::string McpClient::get_last_error() const {
    return last_error_;
}

} // namespace mcp
