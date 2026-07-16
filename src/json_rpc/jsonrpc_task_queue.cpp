#include "jsonrpc_task_queue.h"

#include <stdexcept>
#include <utility>

namespace mcp {

BoundedJsonRpcTaskQueue::BoundedJsonRpcTaskQueue(
    std::size_t capacity
)
    : capacity_(capacity) {
    // 容量为 0 时永远不能接受任务，通常是配置错误。
    if (capacity_ == 0) {
        throw std::invalid_argument(
            "Task queue capacity must be greater than zero"
        );
    }
}

TaskSubmitStatus BoundedJsonRpcTaskQueue::try_push(
    const JsonRpcTaskEnvelopePtr& envelope
) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // shutdown 后不再接收新任务。
        if (closed_) {
            return TaskSubmitStatus::QueueClosed;
        }

        // 有界队列：满了立即拒绝，而不是继续无限占用内存。
        if (queue_.size() >= capacity_) {
            return TaskSubmitStatus::QueueFull;
        }

        queue_.push_back(envelope);
    }

    // 锁释放后再唤醒一个等待中的 worker。
    not_empty_.notify_one();
    return TaskSubmitStatus::Accepted;
}

JsonRpcTaskEnvelopePtr BoundedJsonRpcTaskQueue::wait_pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 没任务时让 worker 睡眠，避免 while 循环空转占用 CPU。
    not_empty_.wait(lock, [this] {
        return closed_ || !queue_.empty();
    });

    // 队列已经关闭且没有剩余任务：
    // 返回 nullptr，worker 收到后应退出线程函数。
    if (queue_.empty()) {
        return nullptr;
    }

    JsonRpcTaskEnvelopePtr envelope = std::move(queue_.front());
    queue_.pop_front();
    return envelope;
}

void BoundedJsonRpcTaskQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 重复关闭无需重复处理。
        if (closed_) {
            return;
        }

        closed_ = true;
    }

    // 唤醒所有阻塞在 wait_pop() 的 worker，让它们检查退出条件。
    not_empty_.notify_all();
}

std::size_t BoundedJsonRpcTaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t BoundedJsonRpcTaskQueue::capacity() const {
    return capacity_;
}

bool BoundedJsonRpcTaskQueue::is_closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

} // namespace mcp