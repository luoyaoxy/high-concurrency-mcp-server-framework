#include "jsonrpc_request_context.h"
#include "logger.h"
#include "mcp_server.h"
#include "tool_execution_error.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace mcp;

namespace {

Tool make_tool(
    const std::string& name,
    bool idempotent,
    int max_retries,
    int timeout_ms = 0
) {
    Tool tool;
    tool.name = name;
    tool.execution_policy.idempotent = idempotent;
    tool.execution_policy.max_retries = max_retries;
    tool.execution_policy.timeout_ms = timeout_ms;
    return tool;
}

ToolResult success_result() {
    ToolResult result;
    result.content.push_back(ContentItem{
        .type = "text",
        .text = "ok"
    });
    return result;
}

ToolResult business_error_result() {
    ToolResult result;
    result.is_error = true;
    result.content.push_back(ContentItem{
        .type = "text",
        .text = "invalid business input"
    });
    return result;
}

class ToolRetryTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MCP_LOG_INIT("tool_retry_test", "", 0, 0, false);
    }
};

} // namespace

TEST_F(ToolRetryTest, RetriesIdempotentToolUntilItSucceeds) {
    std::atomic_int attempts{0};
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("remote_read", true, 2),
        [&attempts](const json&) {
            if (attempts.fetch_add(1) < 2) {
                throw RetryableToolError("temporary upstream failure");
            }
            return success_result();
        }
    );

    ToolResult result = server.call_tool("remote_read", json::object());
    EXPECT_EQ(attempts.load(), 3);
    EXPECT_FALSE(result.is_error);
}

TEST_F(ToolRetryTest, DoesNotRetryNonIdempotentTool) {
    std::atomic_int attempts{0};
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("send_message", false, 2),
        [&attempts](const json&) -> ToolResult {
            ++attempts;
            throw RetryableToolError("temporary mail gateway failure");
        }
    );

    ToolResult result = server.call_tool("send_message", json::object());
    EXPECT_EQ(attempts.load(), 1);
    EXPECT_TRUE(result.is_error);
}

TEST_F(ToolRetryTest, StopsAfterConfiguredRetryLimit) {
    std::atomic_int attempts{0};
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("remote_read", true, 2),
        [&attempts](const json&) -> ToolResult {
            ++attempts;
            throw RetryableToolError("still unavailable");
        }
    );

    ToolResult result = server.call_tool("remote_read", json::object());
    EXPECT_EQ(attempts.load(), 3);
    EXPECT_TRUE(result.is_error);
}

TEST_F(ToolRetryTest, DoesNotRetryAfterRequestCancellation) {
    std::atomic_int attempts{0};
    CancellationToken cancellation;
    JsonRpcRequestContext context(
        "retry-cancelled",
        std::nullopt,
        cancellation
    );
    JsonRpcRequestContextScope context_scope(context);
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("remote_read", true, 2),
        [&attempts, cancellation](const json&) -> ToolResult {
            ++attempts;
            cancellation.cancel();
            throw RetryableToolError("connection reset");
        }
    );

    ToolResult result = server.call_tool("remote_read", json::object());
    EXPECT_EQ(attempts.load(), 1);
    EXPECT_TRUE(result.is_error);
}

TEST_F(ToolRetryTest, DoesNotRetryAfterToolDeadlineExpires) {
    using namespace std::chrono_literals;

    std::atomic_int attempts{0};
    CancellationToken cancellation;
    JsonRpcRequestContext context(
        "retry-timeout",
        JsonRpcRequestContext::Clock::now() + 1s,
        cancellation
    );
    JsonRpcRequestContextScope context_scope(context);
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("remote_read", true, 2, 10),
        [&attempts](const json&) -> ToolResult {
            ++attempts;
            std::this_thread::sleep_for(20ms);
            throw RetryableToolError("request timed out");
        }
    );

    ToolResult result = server.call_tool("remote_read", json::object());
    EXPECT_EQ(attempts.load(), 1);
    EXPECT_TRUE(result.is_error);
}

TEST_F(ToolRetryTest, DoesNotRetryBusinessErrorResult) {
    std::atomic_int attempts{0};
    McpServer server("test", "1.0");

    server.register_tool(
        make_tool("validate", true, 2),
        [&attempts](const json&) {
            ++attempts;
            return business_error_result();
        }
    );

    ToolResult result = server.call_tool("validate", json::object());
    EXPECT_EQ(attempts.load(), 1);
    EXPECT_TRUE(result.is_error);
}
