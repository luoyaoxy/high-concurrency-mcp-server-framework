#include "sse_event_hub.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace mcp {

SseEventHub::SseEventHub(
    std::size_t max_clients,
    std::size_t max_pending_events_per_client,
    std::size_t max_replay_events
)
    // 即使构造函数被直接调用，也避免创建不可用的 0 容量 hub。
    : max_clients_(std::max<std::size_t>(1, max_clients))
    , max_pending_events_per_client_(
        std::max<std::size_t>(1, max_pending_events_per_client)
    )
    , max_replay_events_(std::max<std::size_t>(1, max_replay_events)) {}

std::optional<SseEventHub::SubscriptionId> SseEventHub::subscribe(
    std::optional<EventId> last_event_id
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 超过连接上限时拒绝新 SSE 客户端。
    if (subscribers_.size() >= max_clients_) {
        return std::nullopt;
    }

    const SubscriptionId subscription_id = next_subscription_id_++;

    auto subscriber = std::make_shared<Subscriber>();

    // 首次连接不补发；重连时仅补发仍在有界历史中的较新事件。
    if (last_event_id.has_value()) {
        for (const SseEvent& event : replay_events_) {
            if (event.id > *last_event_id) {
                if (
                    subscriber->pending_events.size() >=
                    max_pending_events_per_client_
                ) {
                    subscriber->pending_events.pop_front();
                }
                subscriber->pending_events.push_back(event);
            }
        }
    }

    subscribers_.emplace(subscription_id, std::move(subscriber));

    return subscription_id;
}

void SseEventHub::unsubscribe(SubscriptionId subscription_id) {
    std::shared_ptr<Subscriber> subscriber;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto it = subscribers_.find(subscription_id);
        if (it == subscribers_.end()) {
            return;
        }

        // 从 hub 移除后，不再接收后续广播事件。
        subscriber = it->second;
        subscribers_.erase(it);
        subscriber->closed = true;
    }

    // 唤醒可能正在等待该客户端事件的 SSE 线程。
    subscriber->event_ready.notify_all();
}

SseEventHub::EventId SseEventHub::publish(std::string payload) {
    std::vector<std::shared_ptr<Subscriber>> subscribers_to_notify;
    EventId event_id;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        SseEvent event{
            next_event_id_++,
            std::chrono::system_clock::now(),
            std::move(payload)
        };
        event_id = event.id;

        replay_events_.push_back(event);
        if (replay_events_.size() > max_replay_events_) {
            replay_events_.pop_front();
        }

        for (const auto& [subscription_id, subscriber] : subscribers_) {
            (void)subscription_id;

            // 慢客户端队列满时丢弃最旧事件，保留最新状态。
            if (
                subscriber->pending_events.size() >=
                max_pending_events_per_client_
            ) {
                subscriber->pending_events.pop_front();
            }

            subscriber->pending_events.push_back(event);
            subscribers_to_notify.push_back(subscriber);
        }
    }

    // 在锁外唤醒客户端，缩短 publish 对其他线程的阻塞时间。
    for (const auto& subscriber : subscribers_to_notify) {
        subscriber->event_ready.notify_one();
    }

    return event_id;
}

std::optional<SseEvent> SseEventHub::wait_and_pop(
    SubscriptionId subscription_id,
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(mutex_);

    const auto it = subscribers_.find(subscription_id);
    if (it == subscribers_.end()) {
        return std::nullopt;
    }

    const std::shared_ptr<Subscriber> subscriber = it->second;

    // 等待新事件、取消订阅或调用方指定的超时。
    const bool ready = subscriber->event_ready.wait_for(
        lock,
        timeout,
        [&subscriber] {
            return subscriber->closed ||
                   !subscriber->pending_events.empty();
        }
    );

    if (!ready || subscriber->closed) {
        return std::nullopt;
    }

    // 一次只取一条，保留其他待发送事件的顺序。
    SseEvent event = std::move(
        subscriber->pending_events.front()
    );
    subscriber->pending_events.pop_front();

    return event;
}

std::size_t SseEventHub::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 返回当前仍处于订阅状态的客户端数量。
    return subscribers_.size();
}

} // namespace mcp
