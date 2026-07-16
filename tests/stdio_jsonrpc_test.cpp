#include "jsonrpc.h"
#include "jsonrpc_request_context.h"
#include "logger.h"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <thread>

#include "jsonrpc_task_runtime.h"

#include <memory>

using namespace mcp;

// 在所有测试之前初始化日志系统
class StdioJsonRpcServerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // 初始化日志系统并设置为 debug 级别
        MCP_LOG_INIT("mcp_test", "", 0, 0, true);
        MCP_LOG_SET_LEVEL(spdlog::level::debug);
    }
};

static JsonRpcDispatcher makeDispatcher() {
    JsonRpcDispatcher d;
    d.registerHandler("echo", [](const json& params) {
        return params;
    });
    d.registerHandler("add", [](const json& params) {
        if (!params.is_array() || params.size() != 2 || !params[0].is_number() || !params[1].is_number()) {
            throw std::invalid_argument("params must be [number, number]");
        }
        return json(params[0].get<int>() + params[1].get<int>());
    });
    return d;
}

static std::shared_ptr<JsonRpcTaskRuntime> makeRuntime(
    JsonRpcDispatcher dispatcher
) {
    return std::make_shared<JsonRpcTaskRuntime>(
        std::move(dispatcher),
        8,
        1
    );
}

static std::string makeFrame(const json& obj) {
    std::string payload = obj.dump();
    std::ostringstream os;
    os << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    return os.str();
}

static json parseFirstFramePayload(const std::string& framed) {
    // 期望格式："Content-Length: <n>\r\n\r\n<payload>"
    const std::string header = "Content-Length:";
    auto header_pos = framed.find(header);
    if (header_pos == std::string::npos) return json();
    auto eol = framed.find("\r\n", header_pos);
    if (eol == std::string::npos) return json();
    auto sep = framed.find("\r\n\r\n", eol);
    if (sep == std::string::npos) return json();
    std::string len_str = framed.substr(header_pos + header.size(), eol - (header_pos + header.size()));
    // trim leading spaces
    size_t pos = len_str.find_first_not_of(' ');
    if (pos != std::string::npos) len_str = len_str.substr(pos);
    size_t content_length = static_cast<size_t>(std::stoul(len_str));
    std::string payload = framed.substr(sep + 4, content_length);
    return json::parse(payload);
}

// 支持测试线程分批写入 JSON-RPC 帧，并控制 EOF 到达时机。
class ControllableInputBuffer : public std::streambuf {
public:
    void append(std::string data) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ += std::move(data);
        data_ready_.notify_all();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        data_ready_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock<std::mutex> lock(mutex_);
        data_ready_.wait(lock, [this] {
            return position_ < data_.size() || closed_;
        });

        if (position_ == data_.size()) {
            return traits_type::eof();
        }

        current_char_ = data_[position_++];
        setg(&current_char_, &current_char_, &current_char_ + 1);
        return traits_type::to_int_type(current_char_);
    }

private:
    std::mutex mutex_;
    std::condition_variable data_ready_;
    std::string data_;
    std::size_t position_ = 0;
    char current_char_ = '\0';
    bool closed_ = false;
};

TEST_F(StdioJsonRpcServerTest, BasicSuccess) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    json req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "add"},
        {"params", json::array({1, 2})}
    };
    in << makeFrame(req);

    auto runtime = makeRuntime(std::move(d));
    StdioJsonRpcServer server(runtime, in, out);

    server.run();
    auto out_str = out.str();
    auto resp_json = parseFirstFramePayload(out_str);
    ASSERT_TRUE(resp_json.contains("result"));
    ASSERT_EQ(resp_json["result"].get<int>(), 3);
}

TEST_F(StdioJsonRpcServerTest, MethodNotFound) {
    JsonRpcDispatcher d;
    std::stringstream in;
    std::stringstream out;

    json req = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "unknown"}
    };
    in << makeFrame(req);

    auto runtime = makeRuntime(std::move(d));
    StdioJsonRpcServer server(runtime, in, out);

    server.run();
    auto out_str = out.str();
    auto resp_json = parseFirstFramePayload(out_str);
    ASSERT_TRUE(resp_json.contains("error"));
    ASSERT_EQ(resp_json["error"]["code"].get<int>(), jsonrpc_errc::MethodNotFound);
}

TEST_F(StdioJsonRpcServerTest, InvalidRequestAndParseError) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    // Invalid Request: missing method
    json bad = {
        {"jsonrpc", "2.0"},
        {"id", 1}
    };
    in << makeFrame(bad);

    auto runtime = makeRuntime(std::move(d));
    StdioJsonRpcServer server(runtime, in, out);

    server.run();
    auto out_str1 = out.str();
    auto resp_json1 = parseFirstFramePayload(out_str1);
    ASSERT_TRUE(resp_json1.contains("error"));
    ASSERT_EQ(resp_json1["error"]["code"].get<int>(), jsonrpc_errc::InvalidRequest);

    // Parse Error: invalid json
    std::stringstream in2;
    std::stringstream out2;
    std::string invalid_json = "{"; // malformed
    std::ostringstream os;
    os << "Content-Length: " << invalid_json.size() << "\r\n\r\n" << invalid_json;
    in2 << os.str();

    auto runtime2 = makeRuntime(JsonRpcDispatcher{});
    StdioJsonRpcServer server2(runtime2, in2, out2);

    server2.run();
    std::string written = out2.str();
    ASSERT_NE(std::string::npos, written.find("Content-Length:"));
    auto resp_json2 = parseFirstFramePayload(written);
    ASSERT_TRUE(resp_json2.contains("error"));
    ASSERT_EQ(resp_json2["error"]["code"].get<int>(), jsonrpc_errc::ParseError);
}

TEST_F(StdioJsonRpcServerTest, NotificationNoResponse) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    json notify = {
        {"jsonrpc", "2.0"},
        {"method", "echo"},
        {"params", json{{"k", 1}}}
    }; // no id
    in << makeFrame(notify);

    auto runtime = makeRuntime(std::move(d));
    StdioJsonRpcServer server(runtime, in, out);

    server.run();
    ASSERT_TRUE(out.str().empty());
}

TEST_F(StdioJsonRpcServerTest, CancelsRunningRequestWithoutBlockingReader) {
    using namespace std::chrono_literals;

    std::promise<void> handler_started;
    std::future<void> handler_started_result = handler_started.get_future();
    std::atomic_bool cancellation_observed{false};
    std::atomic_bool stop_test{false};

    JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler(
        "slow",
        [&handler_started, &cancellation_observed, &stop_test](const json&) {
            handler_started.set_value();

            while (!stop_test.load()) {
                const JsonRpcRequestContext* context =
                    current_jsonrpc_request_context();

                if (context != nullptr && context->should_stop()) {
                    cancellation_observed.store(true);
                    return json{{"stopped", true}};
                }

                std::this_thread::sleep_for(1ms);
            }

            return json{{"stopped", false}};
        }
    );

    ControllableInputBuffer input_buffer;
    std::istream in(&input_buffer);
    std::stringstream out;

    auto runtime = makeRuntime(std::move(dispatcher));
    StdioJsonRpcServer server(runtime, in, out);

    json slow_request = {
        {"jsonrpc", "2.0"},
        {"id", 10},
        {"method", "slow"}
    };
    input_buffer.append(makeFrame(slow_request));

    std::thread server_thread([&server] {
        server.run();
    });

    if (handler_started_result.wait_for(1s) != std::future_status::ready) {
        stop_test.store(true);
        input_buffer.close();
        server_thread.join();
        FAIL() << "slow handler did not start";
    }

    json cancel_request = {
        {"jsonrpc", "2.0"},
        {"method", "$/cancelRequest"},
        {"params", json{{"id", 10}}}
    };
    input_buffer.append(makeFrame(cancel_request));
    input_buffer.close();
    server_thread.join();

    EXPECT_TRUE(cancellation_observed.load());

    json response = parseFirstFramePayload(out.str());
    EXPECT_EQ(response["id"], 10);
    ASSERT_TRUE(response.contains("result"));
    EXPECT_TRUE(response["result"]["stopped"].get<bool>());
}

