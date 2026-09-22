#include "http_jsonrpc.h"
#include "jsonrpc_request_context.h"
#include "jsonrpc_task.h"
#include "jsonrpc_task_runtime.h"
#include "logger.h"
#include "types.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
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

json modern_params(std::string version = kLatestProtocolVersion) {
    return {
        {"_meta", {
            {"io.modelcontextprotocol/protocolVersion", std::move(version)},
            {"io.modelcontextprotocol/clientInfo", {
                {"name", "http-test"}, {"version", "1.0.0"}
            }},
            {"io.modelcontextprotocol/clientCapabilities", json::object()}
        }}
    };
}

httplib::Headers modern_headers(
    const std::string& method,
    const std::string& version = kLatestProtocolVersion,
    const std::string& name = ""
) {
    httplib::Headers headers = {
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", version},
        {"Mcp-Method", method}
    };
    if (!name.empty()) {
        headers.emplace("Mcp-Name", name);
    }
    return headers;
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

TEST_F(HttpJsonRpcBatchTest, ManagesMcpSessionIdAndLegacyRequests) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("initialize", [](const json&) {
        return json{{"protocolVersion", "2025-11-25"}};
    });
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

    const json initialize = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", json::object()}
    };
    httplib::Client initialize_client("127.0.0.1", server.port());
    auto initialize_response = initialize_client.Post(
        "/jsonrpc",
        initialize.dump(),
        "application/json"
    );
    ASSERT_TRUE(initialize_response);
    ASSERT_EQ(initialize_response->status, 200);
    const std::string session_id =
        initialize_response->get_header_value("Mcp-Session-Id");
    ASSERT_EQ(session_id.size(), 64u);
    EXPECT_TRUE(std::all_of(
        session_id.begin(),
        session_id.end(),
        [](unsigned char character) {
            return std::isxdigit(character) != 0;
        }
    ));

    const json echo = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "echo"},
        {"params", json{{"value", "ok"}}}
    };
    httplib::Client session_client("127.0.0.1", server.port());
    auto session_response = session_client.Post(
        "/jsonrpc",
        {{"Mcp-Session-Id", session_id}},
        echo.dump(),
        "application/json"
    );
    ASSERT_TRUE(session_response);
    EXPECT_EQ(session_response->status, 200);

    httplib::Client invalid_session_client("127.0.0.1", server.port());
    auto invalid_session_response = invalid_session_client.Post(
        "/jsonrpc",
        {{"Mcp-Session-Id", "unknown-session"}},
        echo.dump(),
        "application/json"
    );
    ASSERT_TRUE(invalid_session_response);
    EXPECT_EQ(invalid_session_response->status, 404);

    // 未携带 session header 的旧 HTTP 客户端继续使用兼容路径。
    httplib::Client legacy_client("127.0.0.1", server.port());
    auto legacy_response = legacy_client.Post(
        "/jsonrpc",
        echo.dump(),
        "application/json"
    );
    ASSERT_TRUE(legacy_response);
    EXPECT_EQ(legacy_response->status, 200);
}

TEST_F(HttpJsonRpcBatchTest, CancelsAcrossConnectionsWithinSameSession) {
    std::promise<void> handlers_started;
    std::future<void> handlers_started_result = handlers_started.get_future();
    std::atomic_int started_count{0};
    std::atomic_bool client_a_cancelled{false};
    std::atomic_bool client_b_cancelled{false};
    std::promise<void> release_handlers;
    std::shared_future<void> release_signal =
        release_handlers.get_future().share();
    ReleaseGuard release_guard(release_handlers);

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("initialize", [](const json&) {
        return json{{"protocolVersion", "2025-11-25"}};
    });
    dispatcher.registerHandler(
        "slow",
        [
            &handlers_started,
            &started_count,
            &client_a_cancelled,
            &client_b_cancelled,
            release_signal
        ](const json& params) {
            if (started_count.fetch_add(1) + 1 == 2) {
                handlers_started.set_value();
            }

            const std::string client = params.at("client").get<std::string>();
            while (
                release_signal.wait_for(std::chrono::milliseconds(1)) !=
                std::future_status::ready
            ) {
                const JsonRpcRequestContext* context =
                    current_jsonrpc_request_context();
                if (context != nullptr && context->is_cancelled()) {
                    if (client == "a") {
                        client_a_cancelled.store(true);
                    } else {
                        client_b_cancelled.store(true);
                    }
                    return json{{"cancelled", true}};
                }
            }
            return json{{"cancelled", false}};
        }
    );

    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        4
    );
    HttpBatchTestServer server(runtime);
    server.start();

    const auto initialize_session = [&server](int request_id) {
        httplib::Client client("127.0.0.1", server.port());
        const json request = {
            {"jsonrpc", "2.0"},
            {"id", request_id},
            {"method", "initialize"},
            {"params", json::object()}
        };
        auto response = client.Post(
            "/jsonrpc",
            request.dump(),
            "application/json"
        );
        if (!response || response->status != 200) {
            return std::string{};
        }
        return response->get_header_value("Mcp-Session-Id");
    };

    const std::string session_a = initialize_session(10);
    const std::string session_b = initialize_session(11);
    ASSERT_FALSE(session_a.empty());
    ASSERT_FALSE(session_b.empty());
    ASSERT_NE(session_a, session_b);

    const auto call_slow = [&server](
        const std::string& session_id,
        const std::string& client_name
    ) {
        httplib::Client client("127.0.0.1", server.port());
        const json request = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "slow"},
            {"params", json{{"client", client_name}}}
        };
        return client.Post(
            "/jsonrpc",
            {{"Mcp-Session-Id", session_id}},
            request.dump(),
            "application/json"
        );
    };

    auto client_a_result = std::async(
        std::launch::async,
        call_slow,
        session_a,
        "a"
    );
    auto client_b_result = std::async(
        std::launch::async,
        call_slow,
        session_b,
        "b"
    );

    ASSERT_EQ(
        handlers_started_result.wait_for(std::chrono::seconds(1)),
        std::future_status::ready
    );

    // 取消请求使用另一条 HTTP 连接，但携带 Client A 的稳定 session ID。
    httplib::Client cancellation_client("127.0.0.1", server.port());
    const json cancellation = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
        {"params", json{{"requestId", 1}, {"reason", "test"}}}
    };
    auto cancellation_response = cancellation_client.Post(
        "/jsonrpc",
        {{"Mcp-Session-Id", session_a}},
        cancellation.dump(),
        "application/json"
    );
    ASSERT_TRUE(cancellation_response);
    EXPECT_EQ(cancellation_response->status, 200);

    for (
        int attempt = 0;
        attempt < 100 && !client_a_cancelled.load();
        ++attempt
    ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(client_a_cancelled.load());
    EXPECT_FALSE(client_b_cancelled.load());

    release_guard.release();
    ASSERT_TRUE(client_a_result.get());
    ASSERT_TRUE(client_b_result.get());
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

TEST_F(HttpJsonRpcBatchTest, ServesModernDiscoverWithoutSession) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("server/discover", [](const json&) {
        return json{
            {"resultType", "complete"},
            {"supportedVersions", json::array({kLatestProtocolVersion})},
            {"capabilities", json::object()}
        };
    });
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher), 8, 2
    );
    HttpBatchTestServer server(runtime);
    server.start();

    const json request = {
        {"jsonrpc", "2.0"}, {"id", 1},
        {"method", "server/discover"}, {"params", modern_params()}
    };
    httplib::Client client("127.0.0.1", server.port());
    auto response = client.Post(
        "/mcp", modern_headers("server/discover"),
        request.dump(), "application/json"
    );

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    EXPECT_FALSE(response->has_header("Mcp-Session-Id"));
    const json body = json::parse(response->body);
    EXPECT_EQ(body.at("result").at("resultType"), "complete");
}

TEST_F(HttpJsonRpcBatchTest, ValidatesModernHeadersAndVersion) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("server/discover", [](const json&) {
        return json{{"resultType", "complete"}};
    });
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher), 8, 2
    );
    HttpBatchTestServer server(runtime);
    server.start();
    httplib::Client client("127.0.0.1", server.port());

    const json valid_request = {
        {"jsonrpc", "2.0"}, {"id", 2},
        {"method", "server/discover"}, {"params", modern_params()}
    };
    auto mismatch = client.Post(
        "/mcp", modern_headers("tools/list"),
        valid_request.dump(), "application/json"
    );
    ASSERT_TRUE(mismatch);
    EXPECT_EQ(mismatch->status, 400);
    EXPECT_EQ(json::parse(mismatch->body)["error"]["code"], -32020)
        << mismatch->body;

    const std::string unsupported = "2099-01-01";
    json unsupported_request = valid_request;
    unsupported_request["id"] = 3;
    unsupported_request["params"] = modern_params(unsupported);
    auto version_response = client.Post(
        "/mcp", modern_headers("server/discover", unsupported),
        unsupported_request.dump(), "application/json"
    );
    ASSERT_TRUE(version_response);
    EXPECT_EQ(version_response->status, 400);
    EXPECT_EQ(json::parse(version_response->body)["error"]["code"], -32022)
        << version_response->body;

    json missing_capability = valid_request;
    missing_capability["id"] = 4;
    missing_capability["params"]["_meta"].erase(
        "io.modelcontextprotocol/clientCapabilities"
    );
    auto capability_response = client.Post(
        "/mcp", modern_headers("server/discover"),
        missing_capability.dump(), "application/json"
    );
    ASSERT_TRUE(capability_response);
    EXPECT_EQ(capability_response->status, 400);
    EXPECT_EQ(
        json::parse(capability_response->body)["error"]["code"],
        -32021
    );

    auto forbidden_headers = modern_headers("server/discover");
    forbidden_headers.emplace("Origin", "http://localhost:80@evil.example");
    auto forbidden = client.Post(
        "/mcp", forbidden_headers, valid_request.dump(), "application/json"
    );
    ASSERT_TRUE(forbidden);
    EXPECT_EQ(forbidden->status, 403);
}

TEST_F(HttpJsonRpcBatchTest, RejectsUnknownMethodBatchAndObsoleteVerbs) {
    JsonRpcDispatcher dispatcher;
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher), 8, 2
    );
    HttpBatchTestServer server(runtime);
    server.start();
    httplib::Client client("127.0.0.1", server.port());

    const json unknown_request = {
        {"jsonrpc", "2.0"}, {"id", 4},
        {"method", "unknown/method"}, {"params", modern_params()}
    };
    auto unknown = client.Post(
        "/mcp", modern_headers("unknown/method"),
        unknown_request.dump(), "application/json"
    );
    ASSERT_TRUE(unknown);
    EXPECT_EQ(unknown->status, 404);
    EXPECT_EQ(json::parse(unknown->body)["error"]["code"], -32601)
        << unknown->body;

    auto batch = client.Post(
        "/mcp", modern_headers("server/discover"),
        json::array({unknown_request}).dump(), "application/json"
    );
    ASSERT_TRUE(batch);
    EXPECT_EQ(batch->status, 400);

    auto get_response = client.Get("/mcp");
    auto delete_response = client.Delete("/mcp");
    ASSERT_TRUE(get_response);
    ASSERT_TRUE(delete_response);
    EXPECT_EQ(get_response->status, 405);
    EXPECT_EQ(delete_response->status, 405);
}

TEST_F(HttpJsonRpcBatchTest, StreamsToolCallWithoutSseEventIds) {
    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("tools/call", [](const json&) {
        return json{
            {"resultType", "complete"},
            {"content", json::array({json{{"type", "text"}, {"text", "ok"}}})}
        };
    });
    auto runtime = std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher), 8, 2
    );
    HttpBatchTestServer server(runtime);
    server.start();

    json params = modern_params();
    params["name"] = "echo";
    params["arguments"] = json::object();
    const json request = {
        {"jsonrpc", "2.0"}, {"id", 5},
        {"method", "tools/call"}, {"params", std::move(params)}
    };
    httplib::Client client("127.0.0.1", server.port());
    auto response = client.Post(
        "/mcp", modern_headers("tools/call", kLatestProtocolVersion, "echo"),
        request.dump(), "application/json"
    );

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    EXPECT_NE(
        response->get_header_value("Content-Type").find("text/event-stream"),
        std::string::npos
    );
    EXPECT_EQ(response->body.find("id:"), std::string::npos);
    ASSERT_EQ(response->body.rfind("data: ", 0), 0u);
    const json event = json::parse(response->body.substr(6));
    EXPECT_EQ(event.at("result").at("resultType"), "complete");
    EXPECT_FALSE(response->has_header("Mcp-Session-Id"));
}
