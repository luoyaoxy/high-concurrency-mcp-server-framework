#pragma once

#include "jsonrpc_task.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

#include <utility>

namespace mcp {

// 一个任务执行完成后可能返回 JSON-RPC 响应；
// notification 则没有响应，因此是 std::nullopt。
using JsonRpcTaskResult = std::optional<JsonRpcResponse>;

using JsonRpcTaskCompletionCallback =
    std::function<void(JsonRpcTaskResult)>;

// 队列中的实际单元：
// - task：要执行的 JSON-RPC 任务
// - completion：worker 用它把执行结果交回入口层
//
// promise 不能复制，因此该对象通过 shared_ptr 在入口层和队列/worker 间共享。
struct JsonRpcTaskEnvelope {
    explicit JsonRpcTaskEnvelope(JsonRpcTask task_value)
        : task(std::move(task_value)) {}

    JsonRpcTask task;
    std::promise<JsonRpcTaskResult> completion;
    // 可选的完成通知，例如由 stdio 在任务结束后写回响应。
    std::optional<JsonRpcTaskCompletionCallback> on_completed;
};

using JsonRpcTaskEnvelopePtr = std::shared_ptr<JsonRpcTaskEnvelope>;

// 入队的结果。
// QueueFull 是后续实现“系统繁忙、明确拒绝”的基础。
enum class TaskSubmitStatus {
    Accepted,
    QueueFull,
    QueueClosed
};

// 线程安全、有最大容量的任务队列接口。
class BoundedJsonRpcTaskQueue {
public:
    explicit BoundedJsonRpcTaskQueue(std::size_t capacity);

    // 非阻塞入队：队满时立即返回 QueueFull，不无限堆积。
    TaskSubmitStatus try_push(const JsonRpcTaskEnvelopePtr& envelope);

    // worker 调用：队列为空时等待；队列关闭且已清空时返回 nullptr。
    JsonRpcTaskEnvelopePtr wait_pop();

    // 停止接收新任务，并唤醒正在等待的 worker。
    void close();

    std::size_t size() const;
    std::size_t capacity() const;
    bool is_closed() const;

private:
    const std::size_t capacity_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::deque<JsonRpcTaskEnvelopePtr> queue_;
    bool closed_ = false;
};

} // namespace mcp
