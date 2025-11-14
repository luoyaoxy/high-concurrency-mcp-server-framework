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

// JSON-RPC 2.0 请求类型
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::optional<json> id;        // 缺省表示 Notification（无响应）
    std::string method;
    std::optional<json> params;    // 对象或数组，缺省表示无参数
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    json id; // 允许字符串/数字/null
    std::optional<json> result;
    std::optional<JsonRpcError> error;
};


class JsonRpcDispatcher {
public:
    /// 方法处理器类型，接受参数并返回结果，均为 json
    using Handler = std::function<json(const json& params)>;

    /// 注册一个方法处理器
    void registerHandler(const std::string& method, Handler handler);
    /// 检查方法是否已注册
    bool hasHandler(const std::string& method) const;
    /// 调用已注册的方法
    json call(const std::string& method, const json& params) const; // 可能抛异常

private:
    /// 方法处理器映射，用来查询已经注册的方法
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
    // 方法路由与调用分发器（保存 method -> handler 的映射）
    JsonRpcDispatcher dispatcher_;
    // 输入流，默认绑定到标准输入
    std::istream& in_ = std::cin;
    // 输出流，默认绑定到标准输出
    std::ostream& out_ = std::cout;
    // 从输入流读取一条完整的 JSON-RPC 消息体（依据 Content-Length）
    bool readMessage(std::string& out_body); // 从 stdin 读取一条完整 JSON 文本
    // 将 JSON 消息写到输出流，并附带 Content-Length 头
    void writeMessage(const json& msg);

    // 处理单个请求
    JsonRpcResponse handleRequest(const JsonRpcRequest& req);
};

// 常用错误码
namespace jsonrpc_errc {
    constexpr int ParseError = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams = -32602;
    constexpr int InternalError = -32603;
    // 应用自定义错误建议使用 -32000 ~ -32099
}

} // namespace mcp

