#include "jsonrpc_task.h"
#include "logger.h"

#include <atomic>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mcp {
namespace {

JsonRpcTaskLane classify_task_lane(
    const std::string& method
) {
    if (method == "tools/call") {
        return JsonRpcTaskLane::Tool;
    }

    if (method == "resources/read") {
        return JsonRpcTaskLane::Resource;
    }

    if (method == "prompts/get") {
        return JsonRpcTaskLane::Prompt;
    }

    // initialize、list 等轻量请求先走默认通道。
    return JsonRpcTaskLane::Default;
}

// 全局任务编号生成器。
// atomic 保证未来多个线程同时创建任务时，每个 task_id 仍然唯一。
std::atomic<std::uint64_t> g_next_task_id{1};

// 生成简单的链路追踪 ID。
// 当前格式：rpc-时间戳-task_id
//
// 目前它只在当前进程内用于日志追踪。
// 以后如果需要跨进程追踪，可以优先接收客户端传入的 trace_id，
// 或替换为 UUID / OpenTelemetry Trace ID。
std::string make_trace_id(std::uint64_t task_id) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream oss;
    oss << "rpc-" << millis << "-" << task_id;
    return oss.str();
}

// 统一构造 JSON-RPC 响应。
// 将响应构造集中在这里，可以避免正常结果和各种错误分支重复填写字段。
JsonRpcResponse make_response(
    const JsonRpcTask& task,
    std::optional<json> result,
    std::optional<JsonRpcError> error
) {
    JsonRpcResponse response;
    response.jsonrpc = "2.0";

    // 普通请求一定有 request_id。
    // notification 没有 request_id，并且正常情况下不会调用到这里返回响应。
    response.id = task.request_id.value_or(nullptr);

    response.result = std::move(result);
    response.error = std::move(error);
    return response;
}

// 统一构造 JSON-RPC 错误对象。
JsonRpcError make_error(int code, std::string message) {
    return JsonRpcError{
        code,
        std::move(message),
        std::nullopt
    };
}

} // namespace

const char* jsonrpc_task_lane_name(
    JsonRpcTaskLane lane
) {
    switch (lane) {
        case JsonRpcTaskLane::Tool:
            return "tool";
        case JsonRpcTaskLane::Resource:
            return "resource";
        case JsonRpcTaskLane::Prompt:
            return "prompt";
        case JsonRpcTaskLane::Default:
        default:
            return "default";
    }
}

// 将协议层的 JsonRpcRequest 转换为运行时 JsonRpcTask。
//
// JsonRpcRequest 只描述客户端发送了什么；
// JsonRpcTask 还携带服务器执行任务所需的 task_id、trace_id、
// deadline、client_id、priority 和 cancellation token。
JsonRpcTask make_jsonrpc_task(
    const JsonRpcRequest& request,
    std::string client_id,
    std::chrono::milliseconds timeout
) {
    JsonRpcTask task;

    // 原子递增，确保并发创建任务时不会出现重复编号。
    task.task_id =
        g_next_task_id.fetch_add(1, std::memory_order_relaxed);

    task.trace_id = make_trace_id(task.task_id);

    // 复制 JSON-RPC 协议字段。
    task.jsonrpc = request.jsonrpc;
    task.request_id = request.id;

    task.method = request.method;
    task.lane = classify_task_lane(task.method);
    task.params = request.params.value_or(json::object());

    // client_id 用于区分任务来自哪个客户端或连接。
    task.client_id = std::move(client_id);

    // steady_clock 不受系统时间修改影响，适合计算超时。
    task.received_at = JsonRpcTask::Clock::now();

    // timeout <= 0 表示暂时不设置 deadline。
    if (timeout.count() > 0) {
        task.deadline = task.received_at + timeout;
    }


    const std::string request_id = task.request_id.has_value()
        ? task.request_id->dump()
        : "notification";

    // 任务创建即代表请求已进入统一任务系统。
    MCP_LOG_DEBUG(
        "[trace_id={}] Received JSON-RPC request: "
        "task_id={}, method={}, lane={}, request_id={}, client_id={}",
        task.trace_id,
        task.task_id,
        task.method,
        jsonrpc_task_lane_name(task.lane),
        request_id,
        task.client_id
    );

    return task;
}

// 执行一个 JSON-RPC 任务。
//
// 由 worker 调用以执行统一的 JSON-RPC 任务；函数本身保持同步，
// 并发由外层的任务队列和 worker pool 提供。
std::optional<JsonRpcResponse> execute_jsonrpc_task(
    const JsonRpcTask& task,
    const JsonRpcDispatcher& dispatcher
) {
    MCP_LOG_DEBUG(
        "[trace_id={}] Executing JSON-RPC task: "
        "task_id={}, method={}, client_id={}",
        task.trace_id,
        task.task_id,
        task.method,
        task.client_id
    );

    try {
        // 第一步：验证 JSON-RPC 协议版本。
        if (task.jsonrpc != "2.0") {
            // notification 不需要向客户端返回任何响应。
            if (task.is_notification()) {
                return std::nullopt;
            }

            return make_response(
                task,
                std::nullopt,
                make_error(
                    jsonrpc_errc::InvalidRequest,
                    "Invalid Request: jsonrpc must be 2.0"
                )
            );
        }

        // 第二步：验证 method 是否存在。
        if (task.method.empty()) {
            if (task.is_notification()) {
                return std::nullopt;
            }

            return make_response(
                task,
                std::nullopt,
                make_error(
                    jsonrpc_errc::InvalidRequest,
                    "Invalid Request: method missing"
                )
            );
        }

        // 第三步：执行前检查任务是否已经被取消。
        //
        // 排队任务会在此处被跳过；正在执行的慢 handler 则可通过
        // JsonRpcRequestContext 协作式检查取消状态。
        if (task.cancellation.is_cancelled()) {
            if (task.is_notification()) {
                return std::nullopt;
            }

            return make_response(
                task,
                std::nullopt,
                make_error(-32000, "Request cancelled")
            );
        }

        // 第四步：检查任务是否已经超过截止时间。
        //
        // 这可以阻止一个在队列中等待太久的任务继续占用 worker。
        // 目前它还不能强制终止已经开始执行的 handler。
        if (
            task.deadline.has_value() &&
            JsonRpcTask::Clock::now() > *task.deadline
        ) {
            if (task.is_notification()) {
                return std::nullopt;
            }

            return make_response(
                task,
                std::nullopt,
                make_error(
                    -32001,
                    "Request timeout before execution"
                )
            );
        }

        // 第五步：确认 dispatcher 中注册了对应的方法。
        if (!dispatcher.hasHandler(task.method)) {
            if (task.is_notification()) {
                return std::nullopt;
            }

            return make_response(
                task,
                std::nullopt,
                make_error(
                    jsonrpc_errc::MethodNotFound,
                    "Method not found: " + task.method
                )
            );
        }

        // 第六步：真正执行 MCP tool/resource/prompt 对应的处理函数。
        //
        // 该同步调用运行在当前 worker 线程中；handler 不应长期持有
        // registry 锁，并应在需要时检查请求上下文的取消与 deadline。
        json result = dispatcher.call(task.method, task.params);

        // JSON-RPC notification 即使执行成功，也不返回响应。
        if (task.is_notification()) {
            return std::nullopt;
        }

        // 普通请求返回执行结果。
        return make_response(
            task,
            std::move(result),
            std::nullopt
        );

    } catch (const std::invalid_argument& e) {
        // 参数检查失败，转换为 JSON-RPC Invalid Params 错误。
        if (task.is_notification()) {
            return std::nullopt;
        }

        return make_response(
            task,
            std::nullopt,
            make_error(jsonrpc_errc::InvalidParams, e.what())
        );

    } catch (const std::exception& e) {
        // 捕获 handler 抛出的其他标准异常，避免异常逃出 worker，
        // 并转换成统一的 JSON-RPC Internal Error。
        if (task.is_notification()) {
            return std::nullopt;
        }

        return make_response(
            task,
            std::nullopt,
            make_error(jsonrpc_errc::InternalError, e.what())
        );
    }
}

} // namespace mcp
