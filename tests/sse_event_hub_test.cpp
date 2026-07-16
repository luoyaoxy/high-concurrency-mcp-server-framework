#include "sse_event_hub.h"

#include <gtest/gtest.h>

namespace mcp {
namespace {

TEST(SseEventHubTest, RejectsSubscriptionWhenClientLimitReached) {
    // Hub 最多只允许两个 SSE 客户端订阅。
    SseEventHub hub(2, 8);

    const auto first_client = hub.subscribe();
    const auto second_client = hub.subscribe();

    // 前两个客户端应成功订阅。
    ASSERT_TRUE(first_client.has_value());
    ASSERT_TRUE(second_client.has_value());

    // 达到连接上限后，新客户端应被拒绝。
    const auto rejected_client = hub.subscribe();
    EXPECT_FALSE(rejected_client.has_value());

    // 已订阅客户端数量保持为上限值。
    EXPECT_EQ(hub.subscriber_count(), 2u);
}

TEST(SseEventHubTest, BroadcastsEventToEverySubscriber) {
    // 两个客户端均可订阅，并且每个客户端有独立缓冲。
    SseEventHub hub(2, 8);

    const auto first_client = hub.subscribe();
    const auto second_client = hub.subscribe();

    ASSERT_TRUE(first_client.has_value());
    ASSERT_TRUE(second_client.has_value());

    const std::string event = R"({"type":"tool_call"})";

    // 一次发布应复制给所有已订阅客户端。
    hub.publish(event);

    const auto first_event = hub.wait_and_pop(
        *first_client,
        std::chrono::milliseconds(10)
    );
    const auto second_event = hub.wait_and_pop(
        *second_client,
        std::chrono::milliseconds(10)
    );

    // 两个客户端都应收到相同事件，而不是互相抢占。
    ASSERT_TRUE(first_event.has_value());
    ASSERT_TRUE(second_event.has_value());
    EXPECT_EQ(first_event->id, 1u);
    EXPECT_EQ(second_event->id, 1u);
    EXPECT_EQ(first_event->payload, event);
    EXPECT_EQ(second_event->payload, event);
}


TEST(SseEventHubTest, DropsOldestEventOnlyForSlowSubscriber) {
    // 两个客户端各自最多积压两条事件。
    SseEventHub hub(2, 2);

    const auto slow_client = hub.subscribe();
    const auto fast_client = hub.subscribe();

    ASSERT_TRUE(slow_client.has_value());
    ASSERT_TRUE(fast_client.has_value());

    // 两个客户端先收到第一条事件。
    hub.publish("event-1");

    // 快客户端立即消费，慢客户端故意不消费。
    const auto fast_first_event = hub.wait_and_pop(
        *fast_client,
        std::chrono::milliseconds(10)
    );
    ASSERT_TRUE(fast_first_event.has_value());
    EXPECT_EQ(fast_first_event->id, 1u);
    EXPECT_EQ(fast_first_event->payload, "event-1");

    // 慢客户端积压三条事件，容量为二时应丢弃最旧的 event-1。
    hub.publish("event-2");
    hub.publish("event-3");

    const auto slow_first_event = hub.wait_and_pop(
        *slow_client,
        std::chrono::milliseconds(10)
    );
    const auto slow_second_event = hub.wait_and_pop(
        *slow_client,
        std::chrono::milliseconds(10)
    );

    // 慢客户端只保留最新两条事件。
    ASSERT_TRUE(slow_first_event.has_value());
    ASSERT_TRUE(slow_second_event.has_value());
    EXPECT_EQ(slow_first_event->id, 2u);
    EXPECT_EQ(slow_second_event->id, 3u);
    EXPECT_EQ(slow_first_event->payload, "event-2");
    EXPECT_EQ(slow_second_event->payload, "event-3");

    // 快客户端仍保有自己的 event-2、event-3，不受慢客户端影响。
    const auto fast_second_event = hub.wait_and_pop(
        *fast_client,
        std::chrono::milliseconds(10)
    );
    const auto fast_third_event = hub.wait_and_pop(
        *fast_client,
        std::chrono::milliseconds(10)
    );

    ASSERT_TRUE(fast_second_event.has_value());
    ASSERT_TRUE(fast_third_event.has_value());
    EXPECT_EQ(fast_second_event->id, 2u);
    EXPECT_EQ(fast_third_event->id, 3u);
    EXPECT_EQ(fast_second_event->payload, "event-2");
    EXPECT_EQ(fast_third_event->payload, "event-3");
}

TEST(SseEventHubTest, ReplaysEventsAfterLastEventId) {
    SseEventHub hub(2, 8, 8);
    hub.publish("event-1");
    hub.publish("event-2");
    hub.publish("event-3");

    // 未提供 Last-Event-ID 的首次连接不重放历史。
    const auto first_connection = hub.subscribe();
    ASSERT_TRUE(first_connection.has_value());
    EXPECT_FALSE(hub.wait_and_pop(*first_connection, std::chrono::milliseconds(1)));
    hub.unsubscribe(*first_connection);

    // 重连客户端从最后确认的 event-1 之后继续消费。
    const auto resumed_connection = hub.subscribe(1);
    ASSERT_TRUE(resumed_connection.has_value());

    const auto replayed_second = hub.wait_and_pop(
        *resumed_connection,
        std::chrono::milliseconds(10)
    );
    const auto replayed_third = hub.wait_and_pop(
        *resumed_connection,
        std::chrono::milliseconds(10)
    );

    ASSERT_TRUE(replayed_second.has_value());
    ASSERT_TRUE(replayed_third.has_value());
    EXPECT_EQ(replayed_second->id, 2u);
    EXPECT_EQ(replayed_third->id, 3u);
}

TEST(SseEventHubTest, ExposesIdGapWhenReplayHistoryHasEvictedEvents) {
    // 历史只保留最新两条，客户端可通过事件 ID 发现缺口。
    SseEventHub hub(2, 8, 2);
    hub.publish("event-1");
    hub.publish("event-2");
    hub.publish("event-3");

    const auto resumed_connection = hub.subscribe(0);
    ASSERT_TRUE(resumed_connection.has_value());

    const auto first_available = hub.wait_and_pop(
        *resumed_connection,
        std::chrono::milliseconds(10)
    );
    const auto second_available = hub.wait_and_pop(
        *resumed_connection,
        std::chrono::milliseconds(10)
    );

    ASSERT_TRUE(first_available.has_value());
    ASSERT_TRUE(second_available.has_value());
    EXPECT_EQ(first_available->id, 2u);
    EXPECT_EQ(second_available->id, 3u);
}

TEST(SseEventHubTest, ExposesIdGapWhenClientQueueOverflows) {
    SseEventHub hub(1, 2, 8);
    const auto client = hub.subscribe();
    ASSERT_TRUE(client.has_value());

    hub.publish("event-1");
    const auto first_event = hub.wait_and_pop(
        *client,
        std::chrono::milliseconds(10)
    );
    ASSERT_TRUE(first_event.has_value());
    EXPECT_EQ(first_event->id, 1u);

    // 客户端停顿期间积压三条，容量为二时 event-2 会被淘汰。
    hub.publish("event-2");
    hub.publish("event-3");
    hub.publish("event-4");

    const auto resumed_event = hub.wait_and_pop(
        *client,
        std::chrono::milliseconds(10)
    );
    ASSERT_TRUE(resumed_event.has_value());
    EXPECT_EQ(resumed_event->id, 3u);
}

} // namespace
} // namespace mcp
