#pragma once

#include "jsonrpc_cancellation_registry.h"
#include "jsonrpc_metrics.h"
#include "jsonrpc_task_queue.h"
#include "jsonrpc_worker_pool.h"

#include <cstddef>
#include <future>


namespace mcp {

// 无论任务是否成功入队，result 都可取得最终响应。
struct TaskSubmission {
    TaskSubmitStatus status;
    std::future<JsonRpcTaskResult> result;
};

// 统一管理 dispatcher、任务队列和 worker pool。
class JsonRpcTaskRuntime {
public:
    // 兼容现有调用：默认池和工具池使用同一组参数。
    JsonRpcTaskRuntime(
        JsonRpcDispatcher dispatcher,
        std::size_t queue_capacity,
        std::size_t worker_count
    );

    JsonRpcTaskRuntime(
        JsonRpcDispatcher dispatcher,

        // Default lane 配置。
        std::size_t default_queue_capacity,
        std::size_t default_worker_count,

        // Tool lane 配置。
        std::size_t tool_queue_capacity,
        std::size_t tool_worker_count,

        // Resource lane 配置。
        std::size_t resource_queue_capacity,
        std::size_t resource_worker_count,

        // Prompt lane 配置。
        std::size_t prompt_queue_capacity,
        std::size_t prompt_worker_count

    );

    JsonRpcTaskRuntime(const JsonRpcTaskRuntime&) = delete;
    JsonRpcTaskRuntime& operator=(const JsonRpcTaskRuntime&) = delete;

    // 非阻塞提交任务；入口层可等待 future 或接收完成回调。
    TaskSubmission submit(
        JsonRpcTask task,
        std::optional<JsonRpcTaskCompletionCallback> on_completed = std::nullopt
    );

    // 按 JSON-RPC request id 取消仍在排队或执行中的任务。
    bool cancel_request(const json& request_id);

    // HTTP 等入口记录传输层请求，不计入 JSON-RPC task 总数。
    void record_http_request();
    void record_batch_request(std::size_t element_count);

    // 等待任务结果直到 deadline；超时后返回 JSON-RPC timeout 错误。
    JsonRpcTaskResult wait_for_result(
        const JsonRpcTask& task,
        std::future<JsonRpcTaskResult>& result
    );

    // 原子计数器与各 lane 实时队列长度的组合快照。
    JsonRpcMetricsSnapshot metrics_snapshot() const;

    void shutdown();

private:
    BoundedJsonRpcTaskQueue& queue_for(
        JsonRpcTaskLane lane
    );

    JsonRpcDispatcher dispatcher_;

    JsonRpcMetrics metrics_;

    // Runtime 统一维护所有带 request id 任务的取消令牌。
    JsonRpcCancellationRegistry cancellation_registry_;

    // 默认请求使用默认执行池。
    BoundedJsonRpcTaskQueue default_queue_;
    JsonRpcWorkerPool default_workers_;

    // tools/call 使用独立队列与 worker，隔离慢工具。
    BoundedJsonRpcTaskQueue tool_queue_;
    JsonRpcWorkerPool tool_workers_;

    // resources/read 使用独立队列与 worker。
    BoundedJsonRpcTaskQueue resource_queue_;
    JsonRpcWorkerPool resource_workers_;

    // prompts/get 使用独立队列与 worker，隔离慢 prompt 生成。
    BoundedJsonRpcTaskQueue prompt_queue_;
    JsonRpcWorkerPool prompt_workers_;
};

} // namespace mcp
