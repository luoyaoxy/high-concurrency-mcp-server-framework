#include "jsonrpc_task_runtime.h"
#include "logger.h"

#include <memory>
#include <utility>

namespace mcp {
namespace {

JsonRpcTaskResult make_rejected_result(
    const JsonRpcTask& task,
    TaskSubmitStatus status
) {
    if (task.is_notification()) {
        return std::nullopt;
    }

    JsonRpcResponse response;
    response.jsonrpc = kJsonRpcVersion;
    response.id = task.request_id.value_or(nullptr);

    if (status == TaskSubmitStatus::QueueFull) {
        response.error = JsonRpcError{
            -32002,
            "Server busy: task queue is full",
            std::nullopt
        };
    } else {
        response.error = JsonRpcError{
            -32003,
            "Server is shutting down",
            std::nullopt
        };
    }

    return response;
}

JsonRpcTaskResult make_timeout_result(
    const JsonRpcTask& task
) {
    if (task.is_notification()) {
        return std::nullopt;
    }

    JsonRpcResponse response;
    response.jsonrpc = kJsonRpcVersion;
    response.id = task.request_id.value_or(nullptr);
    response.error = JsonRpcError{
        -32001,
        "Request timed out while waiting for execution",
        std::nullopt
    };

    return response;
}

} // namespace

JsonRpcTaskRuntime::JsonRpcTaskRuntime(
    JsonRpcDispatcher dispatcher,
    std::size_t queue_capacity,
    std::size_t worker_count
)
    : JsonRpcTaskRuntime(
        std::move(dispatcher),

        queue_capacity,
        worker_count,

        queue_capacity,
        worker_count,

        queue_capacity,
        worker_count,

        // Prompt lane 也沿用兼容构造函数的默认参数。
        queue_capacity,
        worker_count
    ) {}

JsonRpcTaskRuntime::JsonRpcTaskRuntime(
    JsonRpcDispatcher dispatcher,

    std::size_t default_queue_capacity,
    std::size_t default_worker_count,

    std::size_t tool_queue_capacity,
    std::size_t tool_worker_count,

    std::size_t resource_queue_capacity,
    std::size_t resource_worker_count,

    // Prompt lane 的队列容量与 worker 数。
    std::size_t prompt_queue_capacity,
    std::size_t prompt_worker_count
)
    : dispatcher_(std::move(dispatcher))

    , default_queue_(default_queue_capacity)
    , default_workers_(
        default_queue_,
        dispatcher_,
        cancellation_registry_,
        metrics_
    )

    , tool_queue_(tool_queue_capacity)
    , tool_workers_(
        tool_queue_,
        dispatcher_,
        cancellation_registry_,
        metrics_
    )

    , resource_queue_(resource_queue_capacity)
    , resource_workers_(
        resource_queue_,
        dispatcher_,
        cancellation_registry_,
        metrics_
    )

    // prompt worker 只消费 prompt_queue_ 中的任务。
    , prompt_queue_(prompt_queue_capacity)
    , prompt_workers_(
        prompt_queue_,
        dispatcher_,
        cancellation_registry_,
        metrics_
    ) {
    // 四组 worker 分别消费各自 lane 的队列。
    default_workers_.start(default_worker_count);
    tool_workers_.start(tool_worker_count);
    resource_workers_.start(resource_worker_count);
    prompt_workers_.start(prompt_worker_count);
}

BoundedJsonRpcTaskQueue& JsonRpcTaskRuntime::queue_for(
    JsonRpcTaskLane lane
) {
    if (lane == JsonRpcTaskLane::Tool) {
        return tool_queue_;
    }

    if (lane == JsonRpcTaskLane::Resource) {
        return resource_queue_;
    }

    if (lane == JsonRpcTaskLane::Prompt) {
        // prompts/get 进入独立的 prompt 队列。
        return prompt_queue_;
    }

    // Default lane 使用默认队列。
    return default_queue_;
}

TaskSubmission JsonRpcTaskRuntime::submit(
    JsonRpcTask task,
    std::optional<JsonRpcTaskCompletionCallback> on_completed
) {
    auto envelope =
        std::make_shared<JsonRpcTaskEnvelope>(std::move(task));

    metrics_.record_task_received();

    // 回调随任务一起进入队列，由最终执行它的 worker 调用。
    envelope->on_completed = std::move(on_completed);

    std::future<JsonRpcTaskResult> result =
        envelope->completion.get_future();

    // notification 没有 request id，无法被 $/cancelRequest 定位。
    if (envelope->task.request_id.has_value()) {
        cancellation_registry_.register_request(
            *envelope->task.request_id,
            envelope->task.cancellation
        );
    }

    // 根据任务类型选择默认队列或工具专用队列。
    BoundedJsonRpcTaskQueue& queue =
        queue_for(envelope->task.lane);

    TaskSubmitStatus status = queue.try_push(envelope);

    if (status == TaskSubmitStatus::Accepted) {
        metrics_.record_task_accepted(envelope->task.lane);
        MCP_LOG_DEBUG(
            "[trace_id={}] Task queued: task_id={}, method={}, lane={}, "
            "queue_size={}/{}",
            envelope->task.trace_id,
            envelope->task.task_id,
            envelope->task.method,
            jsonrpc_task_lane_name(envelope->task.lane),
            queue.size(),
            queue.capacity()
        );
    } else {
        metrics_.record_task_rejected(status);
        const char* reason =
            status == TaskSubmitStatus::QueueFull
                ? "queue_full"
                : "queue_closed";

        MCP_LOG_WARN(
            "[trace_id={}] Task rejected: task_id={}, method={}, lane={}, reason={}",
            envelope->task.trace_id,
            envelope->task.task_id,
            envelope->task.method,
            jsonrpc_task_lane_name(envelope->task.lane),
            reason
        );

        // 未入队的任务不会由 worker 清理，需要在这里立即移除登记。
        if (envelope->task.request_id.has_value()) {
            cancellation_registry_.unregister_request(
                *envelope->task.request_id
            );
        }

        JsonRpcTaskResult rejected_result =
            make_rejected_result(envelope->task, status);

        envelope->completion.set_value(rejected_result);

        // 被拒绝的任务不会进入 worker，也需要通知异步入口。
        if (envelope->on_completed.has_value()) {
            try {
                (*envelope->on_completed)(std::move(rejected_result));
            } catch (const std::exception& e) {
                MCP_LOG_ERROR(
                    "JSON-RPC completion callback failed: {}",
                    e.what()
                );
            } catch (...) {
                MCP_LOG_ERROR(
                    "JSON-RPC completion callback failed with unknown error"
                );
            }
        }
    }

    return TaskSubmission{status, std::move(result)};
}

void JsonRpcTaskRuntime::record_http_request() {
    metrics_.record_http_request();
}

void JsonRpcTaskRuntime::record_batch_request(
    std::size_t element_count
) {
    metrics_.record_batch_request(element_count);
}

bool JsonRpcTaskRuntime::cancel_request(
    const json& request_id
) {
    const bool cancelled =
        cancellation_registry_.cancel_request(request_id);

    if (cancelled) {
        MCP_LOG_INFO(
            "Cancellation requested for JSON-RPC id={}",
            request_id.dump()
        );
    } else {
        MCP_LOG_DEBUG(
            "Cancellation ignored: request id={} is not active",
            request_id.dump()
        );
    }

    return cancelled;
}

JsonRpcTaskResult JsonRpcTaskRuntime::wait_for_result(
    const JsonRpcTask& task,
    std::future<JsonRpcTaskResult>& result
) {
    // 未设置 deadline 时，保持原有的等待结果行为。
    if (!task.deadline.has_value()) {
        return result.get();
    }

    // 入口层最多等待到任务 deadline。
    const std::future_status status =
        result.wait_until(*task.deadline);

    if (status == std::future_status::timeout) {
        metrics_.record_wait_timeout();
        // 若任务仍在队列中，worker 取到时会跳过业务执行。
        task.cancellation.cancel();

        MCP_LOG_WARN(
            "[trace_id={}] Request timed out: "
            "task_id={}, method={}",
            task.trace_id,
            task.task_id,
            task.method
        );

        return make_timeout_result(task);
    }

    // worker 已发布执行结果，交还给 HTTP 或 stdio 入口层。
    return result.get();
}

JsonRpcMetricsSnapshot JsonRpcTaskRuntime::metrics_snapshot() const {
    JsonRpcMetricsSnapshot snapshot = metrics_.snapshot();

    const auto fill_queue = [](
        JsonRpcLaneMetricsSnapshot& lane,
        const BoundedJsonRpcTaskQueue& queue
    ) {
        lane.queue_size = queue.size();
        lane.queue_capacity = queue.capacity();
    };

    fill_queue(snapshot.default_lane, default_queue_);
    fill_queue(snapshot.tool_lane, tool_queue_);
    fill_queue(snapshot.resource_lane, resource_queue_);
    fill_queue(snapshot.prompt_lane, prompt_queue_);
    return snapshot;
}

void JsonRpcTaskRuntime::shutdown() {
    // 每个 worker pool 会先处理已入队任务，再退出。
    tool_workers_.stop();
    resource_workers_.stop();

    // 停止 prompts/get 的独立 worker pool。
    prompt_workers_.stop();

    default_workers_.stop();
}

} // namespace mcp
