#pragma once

#include <nlohmann/json.hpp>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <iostream>
#include <mutex>

#include <memory>

namespace mcp {

class JsonRpcTaskRuntime;

using json = nlohmann::json;

// JSON-RPC 协议版本，作为全项目统一的协议常量。
inline constexpr char kJsonRpcVersion[] = "2.0";

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
    std::string jsonrpc = kJsonRpcVersion;
    std::optional<json> id;        // 缺省表示 Notification（无响应）
    std::string method;
    std::optional<json> params;    // 对象或数组，缺省表示无参数
};

struct JsonRpcResponse {
    std::string jsonrpc = kJsonRpcVersion;
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

// 基于 stdio 的 JSON-RPC 服务器。
class StdioJsonRpcServer {
public:
    explicit StdioJsonRpcServer(
        std::shared_ptr<JsonRpcTaskRuntime> runtime
    );

    StdioJsonRpcServer(
        std::shared_ptr<JsonRpcTaskRuntime> runtime,
        std::istream& in,
        std::ostream& out
    );

    // 阻塞运行：循环读取请求并输出响应
    void run();

private:
    enum class Framing {
        kUnknown,
        kNewlineDelimited,
        kContentLength,
    };

    std::shared_ptr<JsonRpcTaskRuntime> runtime_;
    // 输入流，默认绑定到标准输入
    std::istream& in_ = std::cin;
    // 输出流，默认绑定到标准输出
    std::ostream& out_ = std::cout;
    // 串行化 stdio 输出，保证每个响应帧完整写出。
    std::mutex output_mutex_;
    // 跟踪已提交、尚未写完响应的普通请求。
    std::mutex pending_response_mutex_;
    std::condition_variable pending_response_cv_;
    std::size_t pending_response_count_ = 0;
    Framing framing_ = Framing::kUnknown;

    void begin_pending_response();
    void finish_pending_response();
    void wait_for_pending_responses();

    // 标准 MCP 使用逐行 JSON；首帧仍自动识别旧 Content-Length 格式。
    bool readMessage(std::string& out_body);
    void writeMessage(const json& msg);

    // 处理单个请求
    std::optional<JsonRpcResponse> handleRequest(const JsonRpcRequest& req);
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
