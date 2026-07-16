#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace mcp {

// 可重放的 SSE 事件。客户端可通过 id 识别断线或队列溢出造成的缺口。
struct SseEvent {
    using EventId = std::uint64_t;

    EventId id;
    std::chrono::system_clock::time_point timestamp;
    std::string payload;
};

// 管理 SSE 订阅者及其独立、有界的事件缓冲。
class SseEventHub {
public:
    using SubscriptionId = std::uint64_t;
    using EventId = SseEvent::EventId;

    SseEventHub(
        std::size_t max_clients,
        std::size_t max_pending_events_per_client,
        std::size_t max_replay_events = 256
    );

    // 注册一个 SSE 客户端。提供 last_event_id 时先补发仍在历史缓冲中的事件。
    std::optional<SubscriptionId> subscribe(
        std::optional<EventId> last_event_id = std::nullopt
    );

    // 移除客户端，并唤醒正在等待事件的线程。
    void unsubscribe(SubscriptionId subscription_id);

    // 分配单调递增 ID、写入有限历史并广播给所有客户端。
    EventId publish(std::string payload);

    // 等待并取出该客户端的一条事件；超时或已取消订阅时返回空。
    std::optional<SseEvent> wait_and_pop(
        SubscriptionId subscription_id,
        std::chrono::milliseconds timeout
    );

    // 用于日志和健康检查。
    std::size_t subscriber_count() const;

private:
    struct Subscriber {
        // 每个客户端独占自己的待发送事件队列。
        std::deque<SseEvent> pending_events;

        // 发布事件或取消订阅时唤醒等待中的 SSE 连接。
        std::condition_variable event_ready;

        bool closed{false};
    };

    const std::size_t max_clients_;
    const std::size_t max_pending_events_per_client_;
    const std::size_t max_replay_events_;

    mutable std::mutex mutex_;
    SubscriptionId next_subscription_id_{1};
    EventId next_event_id_{1};

    // 有界历史仅用于断线重连补发，不参与普通客户端之间的消费竞争。
    std::deque<SseEvent> replay_events_;

    // shared_ptr 保证等待中的订阅者不会在并发取消时被提前销毁。
    std::unordered_map<
        SubscriptionId,
        std::shared_ptr<Subscriber>
    > subscribers_;
};

} // namespace mcp
