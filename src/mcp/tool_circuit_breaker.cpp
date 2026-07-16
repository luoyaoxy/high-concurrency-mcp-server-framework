#include "tool_circuit_breaker.h"

#include <algorithm>

namespace mcp {

ToolCircuitBreaker::ToolCircuitBreaker(
    std::size_t failure_threshold,
    std::chrono::milliseconds open_duration
)
    : failure_threshold_(std::max<std::size_t>(1, failure_threshold))
    , open_duration_(std::max(std::chrono::milliseconds(1), open_duration)) {}

bool ToolCircuitBreaker::allow_call(const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& entry = entries_[tool_name];

    if (entry.state == State::Closed) {
        return true;
    }

    if (entry.state == State::Open) {
        const auto elapsed =
            std::chrono::steady_clock::now() - entry.opened_at;

        if (elapsed < open_duration_) {
            return false;
        }

        // 冷却期结束后，仅当前调用成为 HalfOpen 试探请求。
        entry.state = State::HalfOpen;
        return true;
    }

    // HalfOpen 试探尚未完成，不允许并发试探压垮恢复中的依赖。
    return false;
}

void ToolCircuitBreaker::record_success(const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& entry = entries_[tool_name];
    entry.state = State::Closed;
    entry.consecutive_failures = 0;
}

void ToolCircuitBreaker::record_failure(const std::string& tool_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& entry = entries_[tool_name];

    if (entry.state == State::HalfOpen) {
        entry.consecutive_failures = failure_threshold_;
    } else {
        ++entry.consecutive_failures;
    }

    if (entry.consecutive_failures >= failure_threshold_) {
        entry.state = State::Open;
        entry.opened_at = std::chrono::steady_clock::now();
    }
}

} // namespace mcp
