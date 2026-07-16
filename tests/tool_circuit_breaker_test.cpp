#include "tool_circuit_breaker.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace mcp;

TEST(ToolCircuitBreakerTest, OpensAndRecoversPerTool) {
    using namespace std::chrono_literals;

    ToolCircuitBreaker breaker(2, 20ms);

    EXPECT_TRUE(breaker.allow_call("unstable"));
    breaker.record_failure("unstable");

    EXPECT_TRUE(breaker.allow_call("unstable"));
    breaker.record_failure("unstable");

    // 阈值达到后，仅拒绝故障工具，不影响其他工具。
    EXPECT_FALSE(breaker.allow_call("unstable"));
    EXPECT_TRUE(breaker.allow_call("healthy"));

    std::this_thread::sleep_for(30ms);

    // 冷却后只放行一个 HalfOpen 试探调用。
    EXPECT_TRUE(breaker.allow_call("unstable"));
    EXPECT_FALSE(breaker.allow_call("unstable"));

    breaker.record_success("unstable");
    EXPECT_TRUE(breaker.allow_call("unstable"));
}

TEST(ToolCircuitBreakerTest, FailedHalfOpenProbeReopensCircuit) {
    using namespace std::chrono_literals;

    ToolCircuitBreaker breaker(1, 20ms);

    breaker.record_failure("remote_api");
    EXPECT_FALSE(breaker.allow_call("remote_api"));

    std::this_thread::sleep_for(30ms);
    EXPECT_TRUE(breaker.allow_call("remote_api"));

    breaker.record_failure("remote_api");
    EXPECT_FALSE(breaker.allow_call("remote_api"));
}
