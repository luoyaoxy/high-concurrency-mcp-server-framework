#include "mcp/client.h"
#include <iostream>
#include <sstream>
#include <random>

namespace mcp {

McpClient::McpClient(const ClientConfig& config) 
    : config_(config), state_(ClientState::Disconnected), request_id_counter_(1) {
    
    http_client_ = std::make_unique<HttpClient>();
    http_client_->SetTimeout(config_.timeout_seconds);
    http_client_->SetConnectTimeout(config_.connect_timeout_seconds);
    
    for (const auto& [key, value] : config_.headers) {
        http_client_->AddHeader(key + ": " + value);
    }
}

McpClient::~McpClient() {
    Disconnect();
}

std::future<bool> McpClient::Connect() {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    
    state_ = ClientState::Connecting;
    
    std::thread([this, promise]() {
        try {
            auto init_request = CreateRequest(mcp_methods::INITIALIZE, {
                {"protocolVersion", "2024-11-05"},
                {"clientInfo", {
                    {"name", "MCP C++ Client"},
                    {"version", "1.0.0"}
                }},
                {"capabilities", {
                    {"tools", json::object()},
                    {"resources", json::object()}
                }}
            });
            
            auto response = SendHttpRequest(init_request).get();
            
            if (response.IsSuccess()) {
                if (response.result->contains("serverInfo")) {
                    ServerInfo info;
                    const auto& server_info = (*response.result)["serverInfo"];
                    info.name = server_info.value("name", "Unknown");
                    info.version = server_info.value("version", "Unknown");
                    info.protocol_version = response.result->value("protocolVersion", "Unknown");
                    info.capabilities = response.result->value("capabilities", json::object());
                    server_info_ = info;
                }
                
                state_ = ClientState::Connected;
                promise->set_value(true);
            } else {
                state_ = ClientState::Error;
                promise->set_value(false);
            }
        } catch (const std::exception& e) {
            std::cerr << "Connection error: " << e.what() << std::endl;
            state_ = ClientState::Error;
            promise->set_value(false);
        }
    }).detach();
    
    return future;
}

void McpClient::Disconnect() {
    state_ = ClientState::Disconnected;
    server_info_.reset();
    
    if (sse_client_) {
        sse_client_->Stop();
    }
    
    std::lock_guard<std::mutex> lock(pending_requests_mutex_);
    for (auto& [id, promise] : pending_requests_) {
        promise.set_exception(std::make_exception_ptr(
            std::runtime_error("Client disconnected")));
    }
    pending_requests_.clear();
}

ClientState McpClient::GetState() const {
    return state_.load();
}

std::optional<ServerInfo> McpClient::GetServerInfo() const {
    return server_info_;
}

std::future<std::vector<Tool>> McpClient::ListTools() {
    auto promise = std::make_shared<std::promise<std::vector<Tool>>>();
    auto future = promise->get_future();
    
    std::thread([this, promise]() {
        try {
            auto request = CreateRequest(mcp_methods::TOOLS_LIST);
            auto response = SendHttpRequest(request).get();
            
            std::vector<Tool> tools;
            if (response.IsSuccess() && response.result->contains("tools")) {
                for (const auto& tool_json : (*response.result)["tools"]) {
                    tools.push_back(Tool::FromJson(tool_json));
                }
            }
            
            promise->set_value(tools);
        } catch (const std::exception& e) {
            promise->set_exception(std::current_exception());
        }
    }).detach();
    
    return future;
}

std::future<ToolResult> McpClient::CallTool(const std::string& name, const json& arguments) {
    auto promise = std::make_shared<std::promise<ToolResult>>();
    auto future = promise->get_future();
    
    std::thread([this, promise, name, arguments]() {
        try {
            auto request = CreateRequest(mcp_methods::TOOLS_CALL, {
                {"name", name},
                {"arguments", arguments}
            });
            
            auto response = SendHttpRequest(request).get();
            
            ToolResult result;
            if (response.IsSuccess() && response.result->contains("content") && 
                (*response.result)["content"].is_array() && !(*response.result)["content"].empty()) {
                
                result = ToolResult::FromJson((*response.result)["content"][0]);
            } else {
                result.type = "error";
                result.content = "Tool call failed";
                result.is_error = true;
            }
            
            promise->set_value(result);
        } catch (const std::exception& e) {
            promise->set_exception(std::current_exception());
        }
    }).detach();
    
    return future;
}

std::future<std::vector<Resource>> McpClient::ListResources() {
    auto promise = std::make_shared<std::promise<std::vector<Resource>>>();
    auto future = promise->get_future();
    
    std::thread([this, promise]() {
        try {
            auto request = CreateRequest(mcp_methods::RESOURCES_LIST);
            auto response = SendHttpRequest(request).get();
            
            std::vector<Resource> resources;
            if (response.IsSuccess() && response.result->contains("resources")) {
                for (const auto& resource_json : (*response.result)["resources"]) {
                    resources.push_back(Resource::FromJson(resource_json));
                }
            }
            
            promise->set_value(resources);
        } catch (const std::exception& e) {
            promise->set_exception(std::current_exception());
        }
    }).detach();
    
    return future;
}

std::future<json> McpClient::ReadResource(const std::string& uri) {
    auto promise = std::make_shared<std::promise<json>>();
    auto future = promise->get_future();
    
    std::thread([this, promise, uri]() {
        try {
            auto request = CreateRequest(mcp_methods::RESOURCES_READ, {{"uri", uri}});
            auto response = SendHttpRequest(request).get();
            
            json content;
            if (response.IsSuccess() && response.result->contains("contents") && 
                (*response.result)["contents"].is_array() && !(*response.result)["contents"].empty()) {
                
                const auto& first_content = (*response.result)["contents"][0];
                if (first_content.contains("text")) {
                    content = json::parse(first_content["text"].get<std::string>());
                }
            }
            
            promise->set_value(content);
        } catch (const std::exception& e) {
            promise->set_exception(std::current_exception());
        }
    }).detach();
    
    return future;
}

std::future<JsonRpcResponse> McpClient::SendRequest(const JsonRpcRequest& request) {
    return SendHttpRequest(request);
}

std::future<JsonRpcResponse> McpClient::SendRequestAsync(const JsonRpcRequest& request) {
    return SendHttpRequest(request);
}

void McpClient::SetNotificationHandler(std::function<void(const std::string& method, const json& params)> handler) {
    notification_handler_ = handler;
}

std::string McpClient::GenerateRequestId() {
    return std::to_string(request_id_counter_.fetch_add(1));
}

void McpClient::HandleSseMessage(const std::string& data, const std::string& event_type) {
    try {
        auto j = json::parse(data);
        auto response = JsonRpcResponse::FromJson(j);
        
        if (response.id.has_value()) {
            std::lock_guard<std::mutex> lock(pending_requests_mutex_);
            auto it = pending_requests_.find(response.id.value());
            if (it != pending_requests_.end()) {
                it->second.set_value(response);
                pending_requests_.erase(it);
            }
        } else {
            if (notification_handler_ && j.contains("method")) {
                std::string method = j["method"];
                json params = j.value("params", json::object());
                notification_handler_(method, params);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing SSE message: " << e.what() << std::endl;
    }
}

void McpClient::HandleSseError(const std::string& error) {
    std::cerr << "SSE Error: " << error << std::endl;
    state_ = ClientState::Error;
}

JsonRpcRequest McpClient::CreateRequest(const std::string& method, const json& params) {
    JsonRpcRequest request;
    request.method = method;
    request.params = params;
    request.id = GenerateRequestId();
    return request;
}

std::future<JsonRpcResponse> McpClient::SendHttpRequest(const JsonRpcRequest& request) {
    auto promise = std::make_shared<std::promise<JsonRpcResponse>>();
    auto future = promise->get_future();
    
    std::thread([this, promise, request]() {
        try {
            std::string request_data = request.ToJson().dump();
            auto http_response = http_client_->post(config_.server_url, request_data);
            
            if (!http_response.IsSuccess()) {
                JsonRpcResponse error_response;
                error_response.error = json{
                    {"code", -1},
                    {"message", "HTTP Error: " + std::to_string(http_response.status_code) + " " + http_response.error_message}
                };
                error_response.id = request.id;
                promise->set_value(error_response);
                return;
            }
            
            auto response_json = json::parse(http_response.body);
            auto response = JsonRpcResponse::FromJson(response_json);
            promise->set_value(response);
            
        } catch (const std::exception& e) {
            JsonRpcResponse error_response;
            error_response.error = json{
                {"code", -32603},
                {"message", e.what()}
            };
            error_response.id = request.id;
            promise->set_value(error_response);
        }
    }).detach();
    
    return future;
}

// AsyncMcpClient implementation
AsyncMcpClient::AsyncMcpClient(const ClientConfig& config) : running_(false) {
    client_ = std::make_unique<McpClient>(config);
}

AsyncMcpClient::~AsyncMcpClient() {
    Disconnect();
}

void AsyncMcpClient::Connect(std::function<void(bool success, const std::string& error)> callback) {
    std::thread([this, callback]() {
        try {
            bool success = client_->Connect().get();
            callback(success, success ? "" : "Connection failed");
        } catch (const std::exception& e) {
            callback(false, e.what());
        }
    }).detach();
}

void AsyncMcpClient::Disconnect() {
    running_ = false;
    if (client_) {
        client_->Disconnect();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void AsyncMcpClient::ListTools(std::function<void(const std::vector<Tool>& tools, const std::string& error)> callback) {
    std::thread([this, callback]() {
        try {
            auto tools = client_->ListTools().get();
            callback(tools, "");
        } catch (const std::exception& e) {
            callback({}, e.what());
        }
    }).detach();
}

void AsyncMcpClient::CallTool(const std::string& name, const json& arguments,
                              std::function<void(const ToolResult& result, const std::string& error)> callback) {
    std::thread([this, name, arguments, callback]() {
        try {
            auto result = client_->CallTool(name, arguments).get();
            callback(result, "");
        } catch (const std::exception& e) {
            ToolResult error_result;
            error_result.type = "error";
            error_result.content = e.what();
            error_result.is_error = true;
            callback(error_result, e.what());
        }
    }).detach();
}

void AsyncMcpClient::ListResources(std::function<void(const std::vector<Resource>& resources, const std::string& error)> callback) {
    std::thread([this, callback]() {
        try {
            auto resources = client_->ListResources().get();
            callback(resources, "");
        } catch (const std::exception& e) {
            callback({}, e.what());
        }
    }).detach();
}

void AsyncMcpClient::ReadResource(const std::string& uri,
                                  std::function<void(const json& content, const std::string& error)> callback) {
    std::thread([this, uri, callback]() {
        try {
            auto content = client_->ReadResource(uri).get();
            callback(content, "");
        } catch (const std::exception& e) {
            callback(json{}, e.what());
        }
    }).detach();
}

} // namespace mcp