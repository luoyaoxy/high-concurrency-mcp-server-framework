#include "jsonrpc_task_runtime.h"
#include "jsonrpc_request_context.h"
#include "jsonrpc_cancellation_registry.h"

#include "logger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>



using namespace std::chrono_literals;
using namespace mcp;

class JsonRpcTaskRuntimeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MCP_LOG_INIT("mcp_runtime_test", "", 0, 0, true);
    }

    static void TearDownTestSuite() {
        MCP_LOG_SHUTDOWN();
    }
};

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

} // namespace

TEST_F(JsonRpcTaskRuntimeTest, ReturnsTimeoutWhenWorkerDoesNotFinish) {
    std::promise<void> started;
    std::future<void> started_result = started.get_future();

    std::promise<void> release;
    std::shared_future<void> release_signal =
        release.get_future().share();

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
        8,
        1
    );
    ReleaseGuard release_guard(release);

    JsonRpcRequest request;
    request.id = 1;
    request.method = "block";

    JsonRpcTask task = make_jsonrpc_task(
        request,
        "test",
        100ms
    );

    TaskSubmission submission = runtime->submit(task);

    ASSERT_EQ(
        started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcTaskResult response =
        runtime->wait_for_result(task, submission.result);

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->error.has_value());
    EXPECT_EQ(response->error->code, -32001);

    release_guard.release();
    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, ReturnsServerBusyWhenQueueIsFull) {
    std::promise<void> started;
    std::future<void> started_result = started.get_future();

    std::promise<void> release;
    std::shared_future<void> release_signal =
        release.get_future().share();

    JsonRpcDispatcher dispatcher;

    // 占住唯一 worker，制造后续任务无法立即执行的场景。
    dispatcher.registerHandler(
        "block",
        [&started, release_signal](const json&) {
            started.set_value();
            release_signal.wait();
            return json{{"ok", true}};
        }
    );

    dispatcher.registerHandler(
        "echo",
        [](const json& params) {
            return params;
        }
    );

    // 1 个 worker，队列最多只能等待 1 个任务。
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        1,
        1
    );
    ReleaseGuard release_guard(release);

    JsonRpcRequest blocking_request;
    blocking_request.id = 1;
    blocking_request.method = "block";

    JsonRpcTask blocking_task =
        make_jsonrpc_task(blocking_request, "test");
    auto first = runtime->submit(blocking_task);

    // 确保第一个任务已由 worker 开始执行并阻塞。
    ASSERT_EQ(
        started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest queued_request;
    queued_request.id = 2;
    queued_request.method = "echo";

    // 第二个任务进入容量为 1 的队列。
    JsonRpcTask queued_task =
        make_jsonrpc_task(queued_request, "test");
    auto second = runtime->submit(queued_task);

    ASSERT_EQ(second.status, TaskSubmitStatus::Accepted);

    JsonRpcRequest rejected_request;
    rejected_request.id = 3;
    rejected_request.method = "echo";

    // 队列已满，第三个任务应被拒绝。
    JsonRpcTask rejected_task =
        make_jsonrpc_task(rejected_request, "test");
    auto third = runtime->submit(rejected_task);

    ASSERT_EQ(third.status, TaskSubmitStatus::QueueFull);

    JsonRpcTaskResult response = third.result.get();

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->error.has_value());
    EXPECT_EQ(response->error->code, -32002);

    // 解除阻塞并正常关闭 worker。
    release_guard.release();
    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, CancelsQueuedTaskBeforeHandlerRuns) {
    std::promise<void> first_task_started;
    std::future<void> first_task_started_result =
        first_task_started.get_future();

    std::promise<void> release_first_task;
    std::shared_future<void> release_signal =
        release_first_task.get_future().share();

    std::atomic_bool cancelled_handler_called{false};

    JsonRpcDispatcher dispatcher;

    // 第一个任务占住唯一 Default worker。
    dispatcher.registerHandler(
        "block",
        [&first_task_started, release_signal](const json&) {
            first_task_started.set_value();
            release_signal.wait();
            return json{{"ok", true}};
        }
    );

    // 第二个任务若真的执行到 handler，测试应失败。
    dispatcher.registerHandler(
        "must_not_run",
        [&cancelled_handler_called](const json&) {
            cancelled_handler_called.store(true);
            return json{{"ok", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        1
    );

    // 防止断言失败时第一个 worker 永久阻塞。
    ReleaseGuard release_guard(release_first_task);

    JsonRpcRequest first_request;
    first_request.id = 1;
    first_request.method = "block";

    JsonRpcTask first_task =
        make_jsonrpc_task(first_request, "test");
    auto first_submission = runtime->submit(first_task);

    ASSERT_EQ(
        first_task_started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest cancelled_request;
    cancelled_request.id = 2;
    cancelled_request.method = "must_not_run";

    JsonRpcTask cancelled_task =
        make_jsonrpc_task(cancelled_request, "test");
    auto cancelled_submission = runtime->submit(cancelled_task);

    // 第二个任务仍在队列中，按 id 标记取消。
    EXPECT_TRUE(runtime->cancel_request(2));

    // 释放第一个任务后，worker 会取到已取消的第二个任务。
    release_guard.release();

    JsonRpcTaskResult response = runtime->wait_for_result(
        cancelled_task,
        cancelled_submission.result
    );

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->error.has_value());
    EXPECT_EQ(response->error->code, -32000);

    // execute_jsonrpc_task() 应在调用 handler 前发现取消状态。
    EXPECT_FALSE(cancelled_handler_called.load());

    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, ToolLaneDoesNotBlockDefaultLane) {
    std::promise<void> tool_started;
    std::future<void> tool_started_result =
        tool_started.get_future();

    std::promise<void> release_tool;
    std::shared_future<void> release_signal =
        release_tool.get_future().share();

    JsonRpcDispatcher dispatcher;

    // tools/call 占住 tool worker，模拟慢工具。
    dispatcher.registerHandler(
        "tools/call",
        [&tool_started, release_signal](const json&) {
            tool_started.set_value();
            release_signal.wait();
            return json{{"tool", "done"}};
        }
    );

    // initialize 属于 Default lane，应不受慢工具影响。
    dispatcher.registerHandler(
        "initialize",
        [](const json&) {
            return json{{"initialized", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),

        8, 1,  // Default lane: queue capacity, worker count
        8, 1,  // Tool lane: queue capacity, worker count
        8, 1,   // Resource lane: queue capacity, worker count

        // Prompt lane 不参与本测试，但仍需提供其构造参数。
        8, 1   // Prompt lane: queue capacity, worker count
    );

    ReleaseGuard release_guard(release_tool);

    JsonRpcRequest tool_request;
    tool_request.id = 1;
    tool_request.method = "tools/call";

    JsonRpcTask tool_task =
        make_jsonrpc_task(tool_request, "test");

    auto tool_submission = runtime->submit(tool_task);

    ASSERT_EQ(
        tool_started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest default_request;
    default_request.id = 2;
    default_request.method = "initialize";

    JsonRpcTask default_task = make_jsonrpc_task(
        default_request,
        "test",
        500ms
    );

    auto default_submission = runtime->submit(default_task);

    // 慢工具仍在运行时，默认 lane 也应能独立完成。
    JsonRpcTaskResult response = runtime->wait_for_result(
        default_task,
        default_submission.result
    );

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->result.has_value());
    EXPECT_TRUE(response->result->at("initialized").get<bool>());

    release_guard.release();
    runtime->shutdown();
}

TEST_F(
    JsonRpcTaskRuntimeTest,
    ToolNotificationUsesToolLaneAndReturnsNoResponse
) {
    // 用于确认无 id 的 tools/call 已在 Tool lane 开始执行。
    std::promise<void> notification_started;
    std::future<void> notification_started_result =
        notification_started.get_future();

    // 阻塞 tool handler，观察它是否会影响 Default lane。
    std::promise<void> release_notification;
    std::shared_future<void> release_signal =
        release_notification.get_future().share();

    JsonRpcDispatcher dispatcher;

    dispatcher.registerHandler(
        "tools/call",
        [&notification_started, release_signal](const json&) {
            notification_started.set_value();
            release_signal.wait();
            return json{{"tool", "done"}};
        }
    );

    // initialize 属于 Default lane，用来验证隔离仍然生效。
    dispatcher.registerHandler(
        "initialize",
        [](const json&) {
            return json{{"initialized", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),

        8, 1,  // Default lane
        8, 1,  // Tool lane
        8, 1,  // Resource lane
        8, 1   // Prompt lane
    );

    // 即使断言失败，也会释放阻塞中的 tool worker。
    ReleaseGuard release_guard(release_notification);

    JsonRpcRequest notification_request;
    notification_request.method = "tools/call";
    // 不设置 id，使该请求成为 JSON-RPC notification。

    JsonRpcTask notification_task =
        make_jsonrpc_task(notification_request, "test");

    EXPECT_EQ(notification_task.lane, JsonRpcTaskLane::Tool);

    auto notification_submission = runtime->submit(notification_task);

    ASSERT_EQ(
        notification_started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest default_request;
    default_request.id = 1;
    default_request.method = "initialize";

    JsonRpcTask default_task = make_jsonrpc_task(
        default_request,
        "test",
        500ms
    );

    auto default_submission = runtime->submit(default_task);

    // 慢 tool notification 不能阻塞 Default lane。
    JsonRpcTaskResult default_response = runtime->wait_for_result(
        default_task,
        default_submission.result
    );

    ASSERT_TRUE(default_response.has_value());
    ASSERT_TRUE(default_response->result.has_value());
    EXPECT_TRUE(default_response->result->at("initialized").get<bool>());

    // notification 执行完成，但协议层不产生响应。
    release_guard.release();
    JsonRpcTaskResult notification_response =
        notification_submission.result.get();
    EXPECT_FALSE(notification_response.has_value());

    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, WorkerExposesCurrentRequestContext) {
    std::promise<bool> context_visible;
    std::future<bool> context_visible_result =
        context_visible.get_future();

    JsonRpcDispatcher dispatcher;

    dispatcher.registerHandler(
        "check_context",
        [&context_visible](const json&) {
            // handler 应只能看到当前 worker 正在执行的请求 context。
            const JsonRpcRequestContext* context =
                current_jsonrpc_request_context();

            context_visible.set_value(
                context != nullptr &&
                !context->trace_id().empty() &&
                !context->should_stop()
            );

            return json{{"ok", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        1
    );

    JsonRpcRequest request;
    request.id = 1;
    request.method = "check_context";

    JsonRpcTask task = make_jsonrpc_task(request, "test");
    auto submission = runtime->submit(task);

    JsonRpcTaskResult response = runtime->wait_for_result(
        task,
        submission.result
    );

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->result.has_value());
    EXPECT_TRUE(response->result->at("ok").get<bool>());

    // handler 确认看到了有效且未停止的 context。
    EXPECT_TRUE(context_visible_result.get());

    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, ResourceLaneDoesNotBlockDefaultLane) {
    std::promise<void> resource_started;
    std::future<void> resource_started_result =
        resource_started.get_future();

    std::promise<void> release_resource;
    std::shared_future<void> release_signal =
        release_resource.get_future().share();

    JsonRpcDispatcher dispatcher;

    // resources/read 占住 resource worker，模拟慢资源读取。
    dispatcher.registerHandler(
        "resources/read",
        [&resource_started, release_signal](const json&) {
            resource_started.set_value();
            release_signal.wait();
            return json{{"resource", "done"}};
        }
    );

    dispatcher.registerHandler(
        "initialize",
        [](const json&) {
            return json{{"initialized", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),

        8, 1,  // Default lane
        8, 1,  // Tool lane
        8, 1,   // Resource lane

        // Prompt lane 不参与本测试，但仍需提供其构造参数。
        8, 1   // Prompt lane
    );
    ReleaseGuard release_guard(release_resource);

    JsonRpcRequest resource_request;
    resource_request.id = 1;
    resource_request.method = "resources/read";

    JsonRpcTask resource_task =
        make_jsonrpc_task(resource_request, "test");

    auto resource_submission = runtime->submit(resource_task);

    ASSERT_EQ(
        resource_started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest default_request;
    default_request.id = 2;
    default_request.method = "initialize";

    JsonRpcTask default_task = make_jsonrpc_task(
        default_request,
        "test",
        500ms
    );

    auto default_submission = runtime->submit(default_task);

    // 慢资源读取不应阻塞默认 lane。
    JsonRpcTaskResult response = runtime->wait_for_result(
        default_task,
        default_submission.result
    );

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->result.has_value());
    EXPECT_TRUE(response->result->at("initialized").get<bool>());

    release_guard.release();
    runtime->shutdown();
}

TEST_F(JsonRpcTaskRuntimeTest, PromptLaneDoesNotBlockDefaultLane) {
    // 用于确认慢 prompt 已经开始执行。
    std::promise<void> prompt_started;
    std::future<void> prompt_started_result =
        prompt_started.get_future();

    // 用于控制慢 prompt 何时结束，避免测试线程永久阻塞。
    std::promise<void> release_prompt;
    std::shared_future<void> release_signal =
        release_prompt.get_future().share();

    JsonRpcDispatcher dispatcher;

    // prompts/get 占住唯一的 prompt worker，模拟慢 prompt 生成。
    dispatcher.registerHandler(
        "prompts/get",
        [&prompt_started, release_signal](const json&) {
            prompt_started.set_value();
            release_signal.wait();
            return json{{"prompt", "done"}};
        }
    );

    // initialize 属于 Default lane，用于验证默认请求未被阻塞。
    dispatcher.registerHandler(
        "initialize",
        [](const json&) {
            return json{{"initialized", true}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),

        8, 1,  // Default lane
        8, 1,  // Tool lane
        8, 1,  // Resource lane
        8, 1   // Prompt lane
    );

    // 即使断言失败，也会释放慢 prompt，保证 worker 能正常退出。
    ReleaseGuard release_guard(release_prompt);

    JsonRpcRequest prompt_request;
    prompt_request.id = 1;
    prompt_request.method = "prompts/get";

    JsonRpcTask prompt_task =
        make_jsonrpc_task(prompt_request, "test");

    auto prompt_submission = runtime->submit(prompt_task);

    ASSERT_EQ(
        prompt_started_result.wait_for(1s),
        std::future_status::ready
    );

    JsonRpcRequest default_request;
    default_request.id = 2;
    default_request.method = "initialize";

    // 默认请求设置 deadline，防止隔离失效时测试无限等待。
    JsonRpcTask default_task = make_jsonrpc_task(
        default_request,
        "test",
        500ms
    );

    auto default_submission = runtime->submit(default_task);

    // 慢 prompt 运行期间，Default lane 仍应独立完成。
    JsonRpcTaskResult response = runtime->wait_for_result(
        default_task,
        default_submission.result
    );

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->result.has_value());
    EXPECT_TRUE(response->result->at("initialized").get<bool>());

    // 结束慢 prompt，再停止所有 worker。
    release_guard.release();
    runtime->shutdown();
}

TEST(JsonRpcCancellationRegistryTest, CancelsOnlyMatchingRequestId) {
    JsonRpcCancellationRegistry registry;

    CancellationToken numeric_id_token;
    CancellationToken string_id_token;

    // 数字 7 与字符串 "7" 是两个不同的 JSON-RPC id。
    registry.register_request(json(7), numeric_id_token);
    registry.register_request(json("7"), string_id_token);

    // 取消数字 id 只应影响对应的 token。
    EXPECT_TRUE(registry.cancel_request(json(7)));
    EXPECT_TRUE(numeric_id_token.is_cancelled());
    EXPECT_FALSE(string_id_token.is_cancelled());

    // 请求清理后，相同 id 不应再能被取消。
    registry.unregister_request(json(7));
    EXPECT_FALSE(registry.cancel_request(json(7)));
}
