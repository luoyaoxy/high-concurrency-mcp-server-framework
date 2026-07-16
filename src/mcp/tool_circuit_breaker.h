#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mcp {

// 按工具名称隔离故障：异常工具不会持续占用 tool worker。
class ToolCircuitBreaker {
public:
    ToolCircuitBreaker(
        std::size_t failure_threshold,
        std::chrono::milliseconds open_duration
    );

    // Open 状态拒绝调用；冷却结束后只允许一次试探调用。
    bool allow_call(const std::string& tool_name);

    void record_success(const std::string& tool_name);
    void record_failure(const std::string& tool_name);

private:
    enum class State {
        Closed,
        Open,
        HalfOpen
    };

    struct Entry {
        State state = State::Closed;
        std::size_t consecutive_failures = 0;
        std::chrono::steady_clock::time_point opened_at{};
    };

    const std::size_t failure_threshold_;
    const std::chrono::milliseconds open_duration_;

    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace mcp
