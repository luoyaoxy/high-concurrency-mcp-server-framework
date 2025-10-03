#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <iostream>

namespace mcp {

using json = nlohmann::json;

// ==============================
// JSON-RPC 2.0 基础类型
// ==============================

struct JsonRpcError {
    int code;
    std::string message;
    std::optional<json> data;
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    json id; // 允许字符串/数字/null
    std::optional<json> result;
    std::optional<JsonRpcError> error;
};

// 序列化/反序列化
// 移至 jsonrpc_serialization.h

// ==============================
// 处理器与服务器
// ==============================

class JsonRpcDispatcher {
public:
    using Handler = std::function<json(const json& params)>;

    void registerHandler(const std::string& method, Handler handler);
    bool hasHandler(const std::string& method) const;
    json call(const std::string& method, const json& params) const; // 可能抛异常

private:
    std::unordered_map<std::string, Handler> handlers_;
};

// stdio + Content-Length 封包的 JSON-RPC 服务器
class StdioJsonRpcServer {
public:
    explicit StdioJsonRpcServer(JsonRpcDispatcher dispatcher);
    StdioJsonRpcServer(JsonRpcDispatcher dispatcher, std::istream& in, std::ostream& out);
    // 阻塞运行：循环读取请求并输出响应
    void run();

private:
    JsonRpcDispatcher dispatcher_;
    std::istream& in_ = std::cin;
    std::ostream& out_ = std::cout;
    bool readMessage(std::string& out_body); // 从 stdin 读取一条完整 JSON 文本
    void writeMessage(const json& msg);      // 写出一条 JSON，使用 Content-Length 头

    JsonRpcResponse handleRequest(const json& req);
};

// 常用错误码（JSON-RPC 2.0 标准）
namespace jsonrpc_errc {
    constexpr int ParseError = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams = -32602;
    constexpr int InternalError = -32603;
    // 应用自定义错误建议使用 -32000 ~ -32099
}

} // namespace mcp


