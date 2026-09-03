#include "http_jsonrpc.h"
#include "jsonrpc_task.h"
#include "jsonrpc_task_runtime.h"
#include "logger.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace mcp;

namespace {

using namespace std::chrono_literals;

std::atomic<int> g_next_http_test_port{19080};

json make_request(int id, const std::string& method) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
}

class HttpBatchTestServer {
public:
    explicit HttpBatchTestServer(
        std::shared_ptr<JsonRpcTaskRuntime> runtime
    )
        : port_(g_next_http_test_port.fetch_add(1))
        , server_(std::make_unique<HttpJsonRpcServer>(
            std::move(runtime),
            "127.0.0.1",
            port_
        )) {}

    ~HttpBatchTestServer() {
        stop();
    }

    void start() {
        thread_ = std::thread([this] {
            server_->run();
        });

        httplib::Client client("127.0.0.1", port_);
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (client.Get("/health")) {
                return;
            }
            std::this_thread::sleep_for(5ms);
        }

        stop();
        throw std::runtime_error("HTTP test server did not start");
    }

    int port() const {
        return port_;
    }

    void stop() {
        if (server_) {
            server_->stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    int port_;
    std::unique_ptr<HttpJsonRpcServer> server_;
    std::thread thread_;
};

class ReleaseGuard {
public:
    explicit ReleaseGuard(std::promise<void>& release)
        : release_(release) {}

    ~ReleaseGuard() {
        release();
    }

    void release() {
        if (!released_) {
            release_.set_value();
            released_ = true;
        }
    }

private:
    std::promise<void>& release_;
    bool released_ = false;
};

class HttpJsonRpcBatchTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        MCP_LOG_INIT("http_jsonrpc_test", "", 0, 0, false);
    }
};

} // namespace

TEST_F(HttpJsonRpcBatchTest, ExecutesBatchConcurrentlyAndKeepsResponseOrder) {
    std::promise<void> slow_tasks_started;
    std::future<void> slow_tasks_started_result =
        slow_tasks_started.get_future();
    std::atomic<int> slow_started{0};
    std::atomic_bool fast_finished{false};
    std::atomic_bool notification_called{false};

    std::promise<void> release_slow_tasks;
    std::shared_future<void> release_signal =
        release_slow_tasks.get_future().share();
    ReleaseGuard release_guard(release_slow_tasks);

    JsonRpcDispatcher dispatcher;
    const auto register_slow = [
        &slow_tasks_started,
        &slow_started,
        release_signal
    ](const std::string& method, const std::string& name) {
        return [
            &slow_tasks_started,
            &slow_started,
            release_signal,
            name
        ](const json&) {
            if (slow_started.fetch_add(1) + 1 == 2) {
                slow_tasks_started.set_value();
            }
            release_signal.wait();
            return json{{"name", name}};
        };
    };

    dispatcher.registerHandler("slow_first", register_slow("slow_first", "first"));
    dispatcher.registerHandler("fast", [&fast_finished](const json&) {
        fast_finished.store(true);
        return json{{"name", "fast"}};
    });
    dispatcher.registerHandler("slow_last", register_slow("slow_last", "last"));
    dispatcher.registerHandler("notify", [&notification_called](const json&) {
        notification_called.store(true);
        return json::object();
    });

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        3
    );
    HttpBatchTestServer server(runtime);
    server.start();

    json batch = json::array({
        make_request(1, "slow_first"),
        make_request(2, "fast"),
        make_request(3, "slow_last"),
        {{"jsonrpc", "2.0"}, {"method", "notify"}}
    });

    auto response_future = std::async(std::launch::async, [&server, batch] {
        httplib::Client client("127.0.0.1", server.port());
        return client.Post("/jsonrpc", batch.dump(), "application/json");
    });

    if (slow_tasks_started_result.wait_for(300ms) != std::future_status::ready) {
        release_guard.release();
        response_future.get();
        FAIL() << "batch requests were not submitted concurrently";
    }

    for (int attempt = 0; attempt < 50 && !fast_finished.load(); ++attempt) {
        std::this_thread::sleep_for(2ms);
    }
    EXPECT_TRUE(fast_finished.load());

    release_guard.release();
    auto response = response_future.get();
    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);

    json response_json = json::parse(response->body);
    ASSERT_TRUE(response_json.is_array());
    ASSERT_EQ(response_json.size(), 3u);
    EXPECT_EQ(response_json[0]["id"], 1);
    EXPECT_EQ(response_json[1]["id"], 2);
    EXPECT_EQ(response_json[2]["id"], 3);
    EXPECT_TRUE(notification_called.load());
}

TEST_F(HttpJsonRpcBatchTest, KeepsLocalFailuresInsideBatch) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("echo", [](const json& params) {
        return params;
    });

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        2
    );
    HttpBatchTestServer server(runtime);
    server.start();

    json batch = json::array({
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "echo"},
         {"params", json{{"value", "first"}}}},
        {{"jsonrpc", "2.0"}, {"id", 2}},
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "echo"},
         {"params", json{{"value", "last"}}}}
    });

    httplib::Client client("127.0.0.1", server.port());
    auto response = client.Post("/jsonrpc", batch.dump(), "application/json");
    ASSERT_TRUE(response);

    json response_json = json::parse(response->body);
    ASSERT_EQ(response_json.size(), 3u);
    EXPECT_EQ(response_json[0]["id"], 1);
    EXPECT_TRUE(response_json[1].contains("error"));
    EXPECT_EQ(response_json[2]["id"], 3);
}

TEST_F(HttpJsonRpcBatchTest, ReturnsServerBusyForOnlyTheFullLane) {
    std::promise<void> blocker_started;
    std::future<void> blocker_started_result = blocker_started.get_future();
    std::promise<void> release_blocker;
    std::shared_future<void> release_signal =
        release_blocker.get_future().share();
    ReleaseGuard release_guard(release_blocker);

    std::promise<void> tool_started;
    std::future<void> tool_started_result = tool_started.get_future();

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler(
        "block",
        [&blocker_started, release_signal](const json&) {
            blocker_started.set_value();
            release_signal.wait();
            return json{{"ok", true}};
        }
    );
    dispatcher.registerHandler("queued", [](const json&) {
        return json{{"queued", true}};
    });
    dispatcher.registerHandler("overflow", [](const json&) {
        return json{{"overflow", true}};
    });
    dispatcher.registerHandler(
        "tools/call",
        [&tool_started](const json&) {
            tool_started.set_value();
            return json{{"tool", true}};
        }
    );

    // Default queue 只能额外容纳一个任务；Tool lane 保持可用。
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        1, 1,
        4, 1,
        4, 1,
        4, 1
    );

    JsonRpcRequest blocker_request;
    blocker_request.id = 99;
    blocker_request.method = "block";
    JsonRpcTask blocker_task =
        make_jsonrpc_task(blocker_request, "test");
    TaskSubmission blocker_submission = runtime->submit(blocker_task);

    ASSERT_EQ(
        blocker_started_result.wait_for(1s),
        std::future_status::ready
    );

    HttpBatchTestServer server(runtime);
    server.start();

    json batch = json::array({
        make_request(1, "queued"),
        make_request(2, "overflow"),
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"}}
    });

    auto response_future = std::async(std::launch::async, [&server, batch] {
        httplib::Client client("127.0.0.1", server.port());
        return client.Post("/jsonrpc", batch.dump(), "application/json");
    });

    if (tool_started_result.wait_for(300ms) != std::future_status::ready) {
        release_guard.release();
        response_future.get();
        runtime->wait_for_result(blocker_task, blocker_submission.result);
        FAIL() << "batch did not submit independent lanes before waiting";
    }

    release_guard.release();
    auto response = response_future.get();
    ASSERT_TRUE(response);

    runtime->wait_for_result(blocker_task, blocker_submission.result);

    json response_json = json::parse(response->body);
    ASSERT_EQ(response_json.size(), 3u);
    EXPECT_EQ(response_json[0]["id"], 1);
    ASSERT_TRUE(response_json[1].contains("error"));
    EXPECT_EQ(response_json[1]["error"]["code"], -32002);
    EXPECT_EQ(response_json[2]["id"], 3);
}

TEST_F(HttpJsonRpcBatchTest, CancelsOnlyTheTargetTaskAndKeepsOtherLanesRunning) {
    std::promise<void> tool_blocker_started;
    std::future<void> tool_blocker_started_result =
        tool_blocker_started.get_future();
    std::promise<void> release_tool_blocker;
    std::shared_future<void> release_signal =
        release_tool_blocker.get_future().share();
    ReleaseGuard release_guard(release_tool_blocker);

    std::promise<void> resource_started;
    std::future<void> resource_started_result = resource_started.get_future();
    std::atomic_bool cancelled_tool_handler_called{false};

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler(
        "tools/call",
        [
            &tool_blocker_started,
            &cancelled_tool_handler_called,
            release_signal
        ](const json& params) {
            if (params.value("name", "") == "block") {
                tool_blocker_started.set_value();
                release_signal.wait();
                return json{{"tool", "blocker"}};
            }

            cancelled_tool_handler_called.store(true);
            return json{{"tool", "should_not_run"}};
        }
    );
    dispatcher.registerHandler(
        "resources/read",
        [&resource_started](const json&) {
            resource_started.set_value();
            return json{{"resource", "ready"}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        4, 1,
        4, 1,
        4, 1,
        4, 1
    );

    JsonRpcRequest blocker_request;
    blocker_request.id = 99;
    blocker_request.method = "tools/call";
    blocker_request.params = json{{"name", "block"}};
    JsonRpcTask blocker_task =
        make_jsonrpc_task(blocker_request, "test");
    TaskSubmission blocker_submission = runtime->submit(blocker_task);

    ASSERT_EQ(
        tool_blocker_started_result.wait_for(1s),
        std::future_status::ready
    );

    HttpBatchTestServer server(runtime);
    server.start();

    json batch = json::array({
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
         {"params", json{{"name", "target"}}}},
        {{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"},
         {"params", json{{"requestId", 1}, {"reason", "test"}}}},
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "resources/read"},
         {"params", json{{"uri", "test://resource"}}}}
    });

    auto response_future = std::async(std::launch::async, [&server, batch] {
        httplib::Client client("127.0.0.1", server.port());
        return client.Post("/jsonrpc", batch.dump(), "application/json");
    });

    if (resource_started_result.wait_for(300ms) != std::future_status::ready) {
        release_guard.release();
        response_future.get();
        runtime->wait_for_result(blocker_task, blocker_submission.result);
        FAIL() << "resource lane did not run while the tool lane was blocked";
    }

    release_guard.release();
    auto response = response_future.get();
    ASSERT_TRUE(response);

    runtime->wait_for_result(blocker_task, blocker_submission.result);

    json response_json = json::parse(response->body);
    ASSERT_EQ(response_json.size(), 2u);
    EXPECT_EQ(response_json[0]["id"], 1);
    ASSERT_TRUE(response_json[0].contains("error"));
    EXPECT_EQ(response_json[0]["error"]["code"], -32000);
    EXPECT_EQ(response_json[1]["id"], 2);
    EXPECT_FALSE(cancelled_tool_handler_called.load());
}
