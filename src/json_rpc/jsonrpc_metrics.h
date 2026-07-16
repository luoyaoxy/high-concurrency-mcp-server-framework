#pragma once

#include "jsonrpc_task_queue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mcp {

// 单个执行 lane 的无锁快照。
struct JsonRpcLaneMetricsSnapshot {
    std::uint64_t tasks_accepted = 0;
    std::uint64_t active_workers = 0;
    std::size_t queue_size = 0;
    std::size_t queue_capacity = 0;

    json to_json() const;
};

// 供日志和 mcp://server/metrics 使用的统一指标快照。
struct JsonRpcMetricsSnapshot {
    std::uint64_t requests_total = 0;
    std::uint64_t requests_in_flight = 0;
    std::uint64_t requests_completed = 0;
    std::uint64_t requests_failed = 0;
    std::uint64_t requests_timeout = 0;
    std::uint64_t server_busy_rejections = 0;
    std::uint64_t queue_closed_rejections = 0;
    std::uint64_t http_requests_total = 0;
    std::uint64_t batch_requests_total = 0;
    std::uint64_t batch_elements_total = 0;
    std::uint64_t execution_duration_ms_total = 0;
    std::uint64_t tool_call_duration_ms_total = 0;

    JsonRpcLaneMetricsSnapshot default_lane;
    JsonRpcLaneMetricsSnapshot tool_lane;
    JsonRpcLaneMetricsSnapshot resource_lane;
    JsonRpcLaneMetricsSnapshot prompt_lane;

    json to_json() const;
};

// 调度路径写入原子计数器；读取方通过 snapshot() 获取一致的近似视图。
class JsonRpcMetrics {
public:
    void record_http_request();
    void record_batch_request(std::size_t element_count);

    void record_task_received();
    void record_task_accepted(JsonRpcTaskLane lane);
    void record_task_rejected(TaskSubmitStatus status);

    void record_worker_started(JsonRpcTaskLane lane);
    void record_worker_finished(
        JsonRpcTaskLane lane,
        const JsonRpcTaskResult& result,
        std::uint64_t duration_ms
    );

    // HTTP/stdio 等等待入口到达自身 deadline 时调用。
    void record_wait_timeout();

    JsonRpcMetricsSnapshot snapshot() const;

private:
    struct LaneCounters {
        std::atomic<std::uint64_t> tasks_accepted{0};
        std::atomic<std::uint64_t> active_workers{0};
    };

    LaneCounters& counters_for(JsonRpcTaskLane lane);
    const LaneCounters& counters_for(JsonRpcTaskLane lane) const;

    std::atomic<std::uint64_t> requests_total_{0};
    std::atomic<std::uint64_t> requests_in_flight_{0};
    std::atomic<std::uint64_t> requests_completed_{0};
    std::atomic<std::uint64_t> requests_failed_{0};
    std::atomic<std::uint64_t> requests_timeout_{0};
    std::atomic<std::uint64_t> server_busy_rejections_{0};
    std::atomic<std::uint64_t> queue_closed_rejections_{0};
    std::atomic<std::uint64_t> http_requests_total_{0};
    std::atomic<std::uint64_t> batch_requests_total_{0};
    std::atomic<std::uint64_t> batch_elements_total_{0};
    std::atomic<std::uint64_t> execution_duration_ms_total_{0};
    std::atomic<std::uint64_t> tool_call_duration_ms_total_{0};

    LaneCounters default_lane_;
    LaneCounters tool_lane_;
    LaneCounters resource_lane_;
    LaneCounters prompt_lane_;
};

} // namespace mcp
