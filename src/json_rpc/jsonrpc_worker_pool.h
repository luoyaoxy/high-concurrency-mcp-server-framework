#pragma once

#include "jsonrpc_task_queue.h"

#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace mcp {

class JsonRpcCancellationRegistry;
class JsonRpcMetrics;

// 固定数量的 worker 线程，持续消费任务队列。
class JsonRpcWorkerPool {
public:
    JsonRpcWorkerPool(
        BoundedJsonRpcTaskQueue& queue,
        const JsonRpcDispatcher& dispatcher,
        JsonRpcCancellationRegistry& cancellation_registry,
        JsonRpcMetrics& metrics
    );

    ~JsonRpcWorkerPool();

    JsonRpcWorkerPool(const JsonRpcWorkerPool&) = delete;
    JsonRpcWorkerPool& operator=(const JsonRpcWorkerPool&) = delete;

    // 创建并启动固定数量的 worker 线程。
    void start(std::size_t worker_count);

    // 关闭队列，等待已入队任务处理完毕，然后回收所有 worker。
    void stop();

    std::size_t worker_count() const;

private:
    void worker_loop(std::size_t worker_index);

    BoundedJsonRpcTaskQueue& queue_;
    const JsonRpcDispatcher& dispatcher_;
    JsonRpcCancellationRegistry& cancellation_registry_;
    JsonRpcMetrics& metrics_;

    mutable std::mutex lifecycle_mutex_;
    std::vector<std::thread> workers_;
    bool started_ = false;
    bool stopped_ = false;
};

} // namespace mcp
