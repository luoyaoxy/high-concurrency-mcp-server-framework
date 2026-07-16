#include "jsonrpc_metrics.h"

namespace mcp {

json JsonRpcLaneMetricsSnapshot::to_json() const {
    return {
        {"tasks_accepted", tasks_accepted},
        {"active_workers", active_workers},
        {"queue_size", queue_size},
        {"queue_capacity", queue_capacity}
    };
}

json JsonRpcMetricsSnapshot::to_json() const {
    return {
        {"requests_total", requests_total},
        {"requests_in_flight", requests_in_flight},
        {"requests_completed", requests_completed},
        {"requests_failed", requests_failed},
        {"requests_timeout", requests_timeout},
        {"server_busy_rejections", server_busy_rejections},
        {"queue_closed_rejections", queue_closed_rejections},
        {"http_requests_total", http_requests_total},
        {"batch_requests_total", batch_requests_total},
        {"batch_elements_total", batch_elements_total},
        {"execution_duration_ms_total", execution_duration_ms_total},
        {"tool_call_duration_ms_total", tool_call_duration_ms_total},
        {"lanes", {
            {"default", default_lane.to_json()},
            {"tool", tool_lane.to_json()},
            {"resource", resource_lane.to_json()},
            {"prompt", prompt_lane.to_json()}
        }}
    };
}

void JsonRpcMetrics::record_http_request() {
    http_requests_total_.fetch_add(1, std::memory_order_relaxed);
}

void JsonRpcMetrics::record_batch_request(std::size_t element_count) {
    batch_requests_total_.fetch_add(1, std::memory_order_relaxed);
    batch_elements_total_.fetch_add(element_count, std::memory_order_relaxed);
}

void JsonRpcMetrics::record_task_received() {
    requests_total_.fetch_add(1, std::memory_order_relaxed);
}

void JsonRpcMetrics::record_task_accepted(JsonRpcTaskLane lane) {
    counters_for(lane).tasks_accepted.fetch_add(
        1,
        std::memory_order_relaxed
    );
}

void JsonRpcMetrics::record_task_rejected(TaskSubmitStatus status) {
    requests_failed_.fetch_add(1, std::memory_order_relaxed);

    if (status == TaskSubmitStatus::QueueFull) {
        server_busy_rejections_.fetch_add(1, std::memory_order_relaxed);
    } else if (status == TaskSubmitStatus::QueueClosed) {
        queue_closed_rejections_.fetch_add(1, std::memory_order_relaxed);
    }
}

void JsonRpcMetrics::record_worker_started(JsonRpcTaskLane lane) {
    requests_in_flight_.fetch_add(1, std::memory_order_relaxed);
    counters_for(lane).active_workers.fetch_add(
        1,
        std::memory_order_relaxed
    );
}

void JsonRpcMetrics::record_worker_finished(
    JsonRpcTaskLane lane,
    const JsonRpcTaskResult& result,
    std::uint64_t duration_ms
) {
    requests_in_flight_.fetch_sub(1, std::memory_order_relaxed);
    counters_for(lane).active_workers.fetch_sub(
        1,
        std::memory_order_relaxed
    );

    execution_duration_ms_total_.fetch_add(
        duration_ms,
        std::memory_order_relaxed
    );

    if (lane == JsonRpcTaskLane::Tool) {
        tool_call_duration_ms_total_.fetch_add(
            duration_ms,
            std::memory_order_relaxed
        );
    }

    if (result.has_value() && result->error.has_value()) {
        requests_failed_.fetch_add(1, std::memory_order_relaxed);
        if (result->error->code == -32001) {
            requests_timeout_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    requests_completed_.fetch_add(1, std::memory_order_relaxed);
}

void JsonRpcMetrics::record_wait_timeout() {
    requests_timeout_.fetch_add(1, std::memory_order_relaxed);
}

JsonRpcMetricsSnapshot JsonRpcMetrics::snapshot() const {
    const auto make_lane_snapshot = [](const LaneCounters& counters) {
        return JsonRpcLaneMetricsSnapshot{
            counters.tasks_accepted.load(std::memory_order_relaxed),
            counters.active_workers.load(std::memory_order_relaxed),
            0,
            0
        };
    };

    JsonRpcMetricsSnapshot result;
    result.requests_total = requests_total_.load(std::memory_order_relaxed);
    result.requests_in_flight =
        requests_in_flight_.load(std::memory_order_relaxed);
    result.requests_completed =
        requests_completed_.load(std::memory_order_relaxed);
    result.requests_failed = requests_failed_.load(std::memory_order_relaxed);
    result.requests_timeout = requests_timeout_.load(std::memory_order_relaxed);
    result.server_busy_rejections =
        server_busy_rejections_.load(std::memory_order_relaxed);
    result.queue_closed_rejections =
        queue_closed_rejections_.load(std::memory_order_relaxed);
    result.http_requests_total =
        http_requests_total_.load(std::memory_order_relaxed);
    result.batch_requests_total =
        batch_requests_total_.load(std::memory_order_relaxed);
    result.batch_elements_total =
        batch_elements_total_.load(std::memory_order_relaxed);
    result.execution_duration_ms_total =
        execution_duration_ms_total_.load(std::memory_order_relaxed);
    result.tool_call_duration_ms_total =
        tool_call_duration_ms_total_.load(std::memory_order_relaxed);

    result.default_lane = make_lane_snapshot(default_lane_);
    result.tool_lane = make_lane_snapshot(tool_lane_);
    result.resource_lane = make_lane_snapshot(resource_lane_);
    result.prompt_lane = make_lane_snapshot(prompt_lane_);
    return result;
}

JsonRpcMetrics::LaneCounters& JsonRpcMetrics::counters_for(
    JsonRpcTaskLane lane
) {
    switch (lane) {
        case JsonRpcTaskLane::Tool:
            return tool_lane_;
        case JsonRpcTaskLane::Resource:
            return resource_lane_;
        case JsonRpcTaskLane::Prompt:
            return prompt_lane_;
        case JsonRpcTaskLane::Default:
        default:
            return default_lane_;
    }
}

const JsonRpcMetrics::LaneCounters& JsonRpcMetrics::counters_for(
    JsonRpcTaskLane lane
) const {
    return const_cast<JsonRpcMetrics*>(this)->counters_for(lane);
}

} // namespace mcp
