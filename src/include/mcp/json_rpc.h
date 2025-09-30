#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <memory>

namespace mcp {

using json = nlohmann::json;

// JSON-RPC 2.0 请求结构体
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";        // JSON-RPC 版本号
    std::string method;                 // 要调用的方法名
    json params;                        // 方法参数
    std::optional<std::string> id;      // 请求 ID（可选）

    json ToJson() const;                            // 转换为 JSON 格式
    static JsonRpcRequest FromJson(const json& j);  // 从 JSON 格式解析
};

// JSON-RPC 2.0 响应结构体
struct JsonRpcResponse {
    std::string jsonrpc = "2.0";        // JSON-RPC 版本号
    std::optional<json> result;         // 成功响应结果（可选）
    std::optional<json> error;          // 错误信息（可选）
    std::optional<std::string> id;      // 响应 ID（可选）

    json ToJson() const;                             // 转换为 JSON 格式
    static JsonRpcResponse FromJson(const json& j);  // 从 JSON 格式解析
    
    bool IsError() const { return error.has_value(); }    // 检查是否为错误响应
    bool IsSuccess() const { return result.has_value(); } // 检查是否为成功响应
};

// JSON-RPC 错误信息结构体
struct JsonRpcError {
    int code;                           // 错误代码
    std::string message;                // 错误消息
    std::optional<json> data;           // 额外错误数据（可选）

    json ToJson() const;                           // 转换为 JSON 格式
    static JsonRpcError FromJson(const json& j);   // 从 JSON 格式解析
};

// JSON-RPC 处理器抽象基类
class JsonRpcHandler {
public:
    virtual ~JsonRpcHandler() = default;
    // 纯虚函数：处理 JSON-RPC 请求（子类必须实现）
    virtual JsonRpcResponse HandleRequest(const JsonRpcRequest& request) = 0;
    
    // 处理原始 JSON 消息并返回响应
    std::string ProcessMessage(const std::string& message);
    
protected:
    // 创建错误响应
    JsonRpcResponse CreateErrorResponse(const std::optional<std::string>& id, 
                                         int code, const std::string& message);
    // 创建成功响应
    JsonRpcResponse CreateSuccessResponse(const std::optional<std::string>& id, 
                                           const json& result);
};

// JSON-RPC 标准错误代码定义
namespace error_codes {
    constexpr int PARSE_ERROR = -32700;      // 解析错误
    constexpr int INVALID_REQUEST = -32600;  // 无效请求
    constexpr int METHOD_NOT_FOUND = -32601; // 方法未找到
    constexpr int INVALID_PARAMS = -32602;   // 无效参数
    constexpr int INTERNAL_ERROR = -32603;   // 内部错误
}

} // namespace mcp