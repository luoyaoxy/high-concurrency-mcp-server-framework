#include "jsonrpc_task_runtime.h"

#include "logger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

using namespace std::chrono_literals;
using namespace mcp;

namespace {

class ReleaseGuard {
public:
    explicit ReleaseGuard(std::promise<void>& promise)
        : promise_(promise) {}

    ~ReleaseGuard() {
        release();
    }

    void release() {
        if (!released_) {
            promise_.set_value();
            released_ = true;
        }
    }

private:
    std::promise<void>& promise_;
    bool released_ = false;
};

JsonRpcTask make_task(int id, const std::string& method) {
    JsonRpcRequest request;
    request.id = id;
    request.method = method;
    return make_jsonrpc_task(request, "metrics-test");
}

} // namespace

class JsonRpcMetricsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MCP_LOG_INIT("jsonrpc_metrics_test", "", 0, 0, false);
    }

    static void TearDownTestSuite() {
        MCP_LOG_SHUTDOWN();
    }
};

TEST_F(JsonRpcMetricsTest, RecordsCompletedDefaultTask) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("initialize", [](const json&) {
        return json{{"ok", true}};
    });

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        4,
        1
    );

    JsonRpcTask task = make_task(1, "initialize");
    TaskSubmission submission = runtime->submit(task);
    ASSERT_TRUE(submission.result.get().has_value());

    const JsonRpcMetricsSnapshot snapshot = runtime->metrics_snapshot();
    EXPECT_EQ(snapshot.requests_total, 1u);
    EXPECT_EQ(snapshot.requests_completed, 1u);
    EXPECT_EQ(snapshot.requests_in_flight, 0u);
    EXPECT_EQ(snapshot.requests_failed, 0u);
    EXPECT_EQ(snapshot.default_lane.tasks_accepted, 1u);
    EXPECT_EQ(snapshot.default_lane.queue_size, 0u);
    EXPECT_EQ(snapshot.default_lane.queue_capacity, 4u);

    runtime->shutdown();
}

TEST_F(JsonRpcMetricsTest, RecordsBusyRejectionAndWaitTimeout) {
    std::promise<void> started;
    std::future<void> started_result = started.get_future();
    std::promise<void> release;
    std::shared_future<void> release_signal = release.get_future().share();
    ReleaseGuard release_guard(release);

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler(
        "block",
        [&started, release_signal](const json&) {
            started.set_value();
            release_signal.wait();
            return json{{"ok", true}};
        }
    );
    dispatcher.registerHandler("echo", [](const json&) {
        return json{{"ok", true}};
    });

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        1,
        1
    );

    JsonRpcTask first_task = make_task(1, "block");
    TaskSubmission first = runtime->submit(first_task);
    ASSERT_EQ(started_result.wait_for(1s), std::future_status::ready);

    JsonRpcTask queued_task = make_task(2, "echo");
    TaskSubmission queued = runtime->submit(queued_task);
    ASSERT_EQ(queued.status, TaskSubmitStatus::Accepted);

    JsonRpcTask rejected_task = make_task(3, "echo");
    TaskSubmission rejected = runtime->submit(rejected_task);
    ASSERT_EQ(rejected.status, TaskSubmitStatus::QueueFull);
    ASSERT_TRUE(rejected.result.get().has_value());

    JsonRpcMetricsSnapshot snapshot = runtime->metrics_snapshot();
    EXPECT_EQ(snapshot.requests_total, 3u);
    EXPECT_EQ(snapshot.server_busy_rejections, 1u);
    EXPECT_EQ(snapshot.requests_failed, 1u);
    EXPECT_EQ(snapshot.default_lane.tasks_accepted, 2u);

    release_guard.release();
    first.result.get();
    queued.result.get();
    runtime->shutdown();
}

TEST_F(JsonRpcMetricsTest, RecordsWaitTimeout) {
    std::promise<void> started;
    std::future<void> started_result = started.get_future();
    std::promise<void> release;
    std::shared_future<void> release_signal = release.get_future().share();
    ReleaseGuard release_guard(release);

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler(
        "block",
        [&started, release_signal](const json&) {
            started.set_value();
            release_signal.wait();
            return json{{"ok", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        4,
        1
    );

    JsonRpcRequest request;
    request.id = 1;
    request.method = "block";
    JsonRpcTask task = make_jsonrpc_task(request, "metrics-test", 20ms);
    TaskSubmission submission = runtime->submit(task);

    ASSERT_EQ(started_result.wait_for(1s), std::future_status::ready);
    JsonRpcTaskResult response = runtime->wait_for_result(
        task,
        submission.result
    );
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->error.has_value());
    EXPECT_EQ(response->error->code, -32001);
    EXPECT_EQ(runtime->metrics_snapshot().requests_timeout, 1u);

    release_guard.release();
    runtime->shutdown();
}

TEST_F(JsonRpcMetricsTest, SeparatesLanesAndSupportsConcurrentSnapshots) {
    std::promise<void> tool_started;
    std::future<void> tool_started_result = tool_started.get_future();
    std::promise<void> release_tool;
    std::shared_future<void> release_signal =
        release_tool.get_future().share();
    ReleaseGuard release_guard(release_tool);

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("initialize", [](const json&) {
        return json::object();
    });
    dispatcher.registerHandler(
        "tools/call",
        [&tool_started, release_signal](const json&) {
            tool_started.set_value();
            release_signal.wait();
            return json::object();
        }
    );
    dispatcher.registerHandler("resources/read", [](const json&) {
        return json::object();
    });
    dispatcher.registerHandler("prompts/get", [](const json&) {
        return json::object();
    });

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        4, 1,
        4, 1,
        4, 1,
        4, 1
    );

    TaskSubmission default_submission =
        runtime->submit(make_task(1, "initialize"));
    TaskSubmission tool_submission =
        runtime->submit(make_task(2, "tools/call"));
    TaskSubmission resource_submission =
        runtime->submit(make_task(3, "resources/read"));
    TaskSubmission prompt_submission =
        runtime->submit(make_task(4, "prompts/get"));

    ASSERT_EQ(tool_started_result.wait_for(1s), std::future_status::ready);

    // 读取快照不应等待正在执行的 tool handler。
    const JsonRpcMetricsSnapshot running_snapshot =
        runtime->metrics_snapshot();
    EXPECT_GE(running_snapshot.requests_in_flight, 1u);
    EXPECT_EQ(running_snapshot.tool_lane.active_workers, 1u);
    EXPECT_EQ(running_snapshot.tool_lane.queue_capacity, 4u);

    // 多个观测线程可同时读取原子快照，不应影响慢 tool 的执行。
    std::vector<std::future<bool>> snapshot_readers;
    for (int index = 0; index < 4; ++index) {
        snapshot_readers.push_back(std::async(
            std::launch::async,
            [runtime] {
                for (int attempt = 0; attempt < 100; ++attempt) {
                    const JsonRpcMetricsSnapshot snapshot =
                        runtime->metrics_snapshot();
                    if (
                        snapshot.tool_lane.queue_size >
                        snapshot.tool_lane.queue_capacity
                    ) {
                        return false;
                    }
                }
                return true;
            }
        ));
    }

    for (auto& reader : snapshot_readers) {
        EXPECT_TRUE(reader.get());
    }

    release_guard.release();
    default_submission.result.get();
    tool_submission.result.get();
    resource_submission.result.get();
    prompt_submission.result.get();

    const JsonRpcMetricsSnapshot snapshot = runtime->metrics_snapshot();
    EXPECT_EQ(snapshot.default_lane.tasks_accepted, 1u);
    EXPECT_EQ(snapshot.tool_lane.tasks_accepted, 1u);
    EXPECT_EQ(snapshot.resource_lane.tasks_accepted, 1u);
    EXPECT_EQ(snapshot.prompt_lane.tasks_accepted, 1u);
    EXPECT_EQ(snapshot.requests_in_flight, 0u);

    runtime->shutdown();
}
