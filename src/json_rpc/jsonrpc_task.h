#pragma once

#include "jsonrpc.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mcp {

// 请求优先级：现在先定义出来，后面做优先级队列时会用到
enum class RpcTaskPriority {
    Low = 0,
    Normal = 1,
    High = 2
};

// 任务所属的执行通道，后续用于路由到不同 worker pool。
enum class JsonRpcTaskLane {
    Default,
    Tool,
    Resource,
    Prompt
};

// 用于日志和监控输出。
const char* jsonrpc_task_lane_name(
    JsonRpcTaskLane lane
);

// 取消标记：现在先作为任务字段存在，后面做取消请求时会用到
class CancellationToken {
public:
    CancellationToken()
        : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

    bool is_cancelled() const {
        return cancelled_ && cancelled_->load(std::memory_order_relaxed);
    }

    void cancel() const {
        if (cancelled_) {
            cancelled_->store(true, std::memory_order_relaxed);
        }
    }

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

// JSON-RPC 的运行期任务单元
//
// JsonRpcRequest 是协议层对象，只表示客户端发了什么。
// JsonRpcTask 是服务端执行层对象，表示这个请求进入服务端后应该如何被调度、追踪、超时和取消。
struct JsonRpcTask {
    using Clock = std::chrono::steady_clock;

    // 服务端内部任务 ID，和 JSON-RPC request id 不同
    std::uint64_t task_id = 0;

    // JSON-RPC 协议字段
    std::string jsonrpc = kJsonRpcVersion;
    std::optional<json> request_id;
    std::string method;
    json params = json::object();

    // 任务运行时字段
    Clock::time_point received_at = Clock::now();
    std::optional<Clock::time_point> deadline;

    std::string trace_id;
    std::string client_id;

    RpcTaskPriority priority = RpcTaskPriority::Normal;
    JsonRpcTaskLane lane = JsonRpcTaskLane::Default;
    CancellationToken cancellation;

    bool is_notification() const {
        return !request_id.has_value();
    }
};

// 把协议层请求转换成服务端内部任务
JsonRpcTask make_jsonrpc_task(
    const JsonRpcRequest& request,
    std::string client_id = "",
    std::chrono::milliseconds timeout = std::chrono::milliseconds{30000}
);

// 执行一个 JSON-RPC 任务
//
// 返回 std::nullopt 表示这是 notification，不需要响应。
std::optional<JsonRpcResponse> execute_jsonrpc_task(
    const JsonRpcTask& task,
    const JsonRpcDispatcher& dispatcher
);

} // namespace mcp
