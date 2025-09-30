#pragma once

#include "mcp_protocol.h"
#include "http_transport.h"
#include <memory>
#include <future>
#include <unordered_map>
#include <mutex>

namespace mcp {

// 客户端配置结构体，用于设置连接参数
struct ClientConfig {
    std::string server_url;                                        // MCP 服务器 URL 地址
    int timeout_seconds = 30;                                      // 请求超时时间（秒）
    int connect_timeout_seconds = 10;                              // 连接超时时间（秒）
    std::unordered_map<std::string, std::string> headers;          // 额外的 HTTP 请求头
};

// 客户端连接状态枚举
enum class ClientState {
    Disconnected,    // 已断开连接
    Connecting,      // 正在连接
    Connected,       // 已连接
    Error           // 连接错误
};

// 服务器信息结构体，包含服务器的基本信息
struct ServerInfo {
    std::string name;                // 服务器名称
    std::string version;             // 服务器版本
    std::string protocol_version;    // MCP 协议版本
    json capabilities;               // 服务器能力描述
};

// MCP 客户端类，提供与 MCP 服务器通信的完整功能
class McpClient {
public:
    McpClient(const ClientConfig& config);
    ~McpClient();

    // 异步连接到 MCP 服务器
    std::future<bool> Connect();
    // 断开与服务器的连接
    void Disconnect();
    
    // 获取当前客户端连接状态
    ClientState GetState() const;
    // 获取服务器信息（如果已连接）
    std::optional<ServerInfo> GetServerInfo() const;
    
    // 异步获取服务器提供的工具列表
    std::future<std::vector<Tool>> ListTools();
    // 异步调用指定的工具
    std::future<ToolResult> CallTool(const std::string& name, const json& arguments);
    
    // 异步获取服务器提供的资源列表
    std::future<std::vector<Resource>> ListResources();
    // 异步读取指定的资源内容
    std::future<json> ReadResource(const std::string& uri);
    
    // 发送自定义的 JSON-RPC 请求
    std::future<JsonRpcResponse> SendRequest(const JsonRpcRequest& request);
    std::future<JsonRpcResponse> SendRequestAsync(const JsonRpcRequest& request);
    
    // 设置通知处理器，用于处理来自服务器的通知消息
    void SetNotificationHandler(std::function<void(const std::string& method, const json& params)> handler);
    
private:
    ClientConfig config_;                                          // 客户端配置
    std::unique_ptr<HttpClient> http_client_;                      // HTTP 客户端实例
    std::unique_ptr<SseClient> sse_client_;                        // SSE (Server-Sent Events) 客户端实例
    
    std::atomic<ClientState> state_;                               // 原子操作的客户端状态
    std::optional<ServerInfo> server_info_;                        // 服务器信息缓存
    
    std::mutex pending_requests_mutex_;                            // 待处理请求的互斥锁
    std::unordered_map<std::string, std::promise<JsonRpcResponse>> pending_requests_; // 待处理请求映射表
    std::atomic<int> request_id_counter_;                          // 原子操作的请求 ID 计数器
    
    std::function<void(const std::string&, const json&)> notification_handler_; // 通知处理器
    
    // 生成唯一的请求 ID
    std::string GenerateRequestId();
    // 处理 SSE 消息
    void HandleSseMessage(const std::string& data, const std::string& event_type);
    // 处理 SSE 错误
    void HandleSseError(const std::string& error);
    
    // 创建标准的 JSON-RPC 请求
    JsonRpcRequest CreateRequest(const std::string& method, const json& params = json::object());
    // 通过 HTTP 发送请求
    std::future<JsonRpcResponse> SendHttpRequest(const JsonRpcRequest& request);
};

// 异步 MCP 客户端类，提供基于回调函数的异步接口
class AsyncMcpClient {
public:
    AsyncMcpClient(const ClientConfig& config);
    ~AsyncMcpClient();
    
    // 异步连接到服务器，完成后调用回调函数
    void Connect(std::function<void(bool success, const std::string& error)> callback);
    // 断开连接
    void Disconnect();
    
    // 异步获取工具列表，完成后调用回调函数
    void ListTools(std::function<void(const std::vector<Tool>& tools, const std::string& error)> callback);
    // 异步调用工具，完成后调用回调函数
    void CallTool(const std::string& name, const json& arguments,
                   std::function<void(const ToolResult& result, const std::string& error)> callback);
                   
    // 异步获取资源列表，完成后调用回调函数
    void ListResources(std::function<void(const std::vector<Resource>& resources, const std::string& error)> callback);
    // 异步读取资源，完成后调用回调函数
    void ReadResource(const std::string& uri,
                      std::function<void(const json& content, const std::string& error)> callback);

private:
    std::unique_ptr<McpClient> client_;    // 内部的 MCP 客户端实例
    std::thread worker_thread_;            // 工作线程
    std::atomic<bool> running_;            // 运行状态标志
};

} // namespace mcp