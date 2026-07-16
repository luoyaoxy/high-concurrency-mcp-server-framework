#include "http_sse_server.h"

#include "logger.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace mcp {
namespace {

std::optional<std::uint64_t> last_event_id_from_request(
    const httplib::Request& request
) {
    const std::string value = request.get_header_value("Last-Event-ID");
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t event_id = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto [parsed_end, error] = std::from_chars(
        begin,
        end,
        event_id
    );

    if (error != std::errc{} || parsed_end != end) {
        MCP_LOG_WARN("Ignoring invalid Last-Event-ID: {}", value);
        return std::nullopt;
    }

    return event_id;
}

} // namespace

// 将 httplib 实现细节限制在当前编译单元。
class HttpSseServer::Impl {
public:
    httplib::Server server;
};

HttpSseServer::HttpSseServer(
    std::string host,
    int port,
    std::size_t worker_count
)
    : host_(std::move(host))
    , port_(port)
    , worker_count_(std::max<std::size_t>(1, worker_count))
    , impl_(std::make_unique<Impl>()) {
    // 该线程池只服务 SSE 监听端口，不与 JSON-RPC HTTP 服务共享。
    impl_->server.new_task_queue = [worker_count = worker_count_]() {
        return new httplib::ThreadPool(worker_count);
    };

    // 独立 SSE 服务的自描述端点。
    impl_->server.Get(
        "/",
        [](const httplib::Request& /*request*/,
           httplib::Response& response) {
            nlohmann::json info = {
                {"service", "MCP SSE Server"},
                {"version", "1.0.0"},
                {"endpoints", {
                    {
                        {"path", "/sse/events"},
                        {"method", "GET"},
                        {"description", "Server status event stream"}
                    },
                    {
                        {"path", "/sse/tool_calls"},
                        {"method", "GET"},
                        {"description", "Tool call monitoring stream"}
                    }
                }}
            };

            response.set_content(info.dump(2), "application/json");
        }
    );

    MCP_LOG_INFO(
        "SSE HTTP server created on {}:{} with {} workers",
        host_,
        port_,
        worker_count_
    );
}

HttpSseServer::~HttpSseServer() {
    // 析构时确保阻塞中的 listen() 能退出。
    stop();
}

void HttpSseServer::register_sse_endpoint(
    const std::string& path,
    SseCallback callback
) {
    impl_->server.Get(
        path,
        [callback = std::move(callback)](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            // SSE 协议所需的响应头。
            response.set_header("Content-Type", "text/event-stream");
            response.set_header("Cache-Control", "no-cache");
            response.set_header("Connection", "keep-alive");
            response.set_header("Access-Control-Allow-Origin", "*");
            response.set_header("X-Accel-Buffering", "no");

            const auto last_event_id = last_event_id_from_request(request);

            response.set_chunked_content_provider(
                "text/event-stream",
                [callback, last_event_id](
                    std::size_t /*offset*/,
                    httplib::DataSink& sink
                ) {
                    // 每条 SSE 消息带可恢复的 id，并按协议逐行写入 data。
                    const auto send_event = [
                        &sink
                    ](
                        std::optional<std::uint64_t> event_id,
                        const std::string& data
                    ) {
                        std::string event;
                        if (event_id.has_value()) {
                            event += "id: " + std::to_string(*event_id) + "\n";
                        }

                        std::size_t line_start = 0;
                        while (line_start <= data.size()) {
                            const std::size_t line_end = data.find('\n', line_start);
                            const std::size_t length =
                                line_end == std::string::npos
                                    ? data.size() - line_start
                                    : line_end - line_start;
                            event += "data: ";
                            event.append(data, line_start, length);
                            event += "\n";

                            if (line_end == std::string::npos) {
                                break;
                            }
                            line_start = line_end + 1;
                        }
                        event += "\n";
                        sink.write(event.c_str(), event.size());
                    };

                    try {
                        callback(
                            last_event_id,
                            send_event
                        );
                    } catch (const std::exception& e) {
                        MCP_LOG_ERROR(
                            "SSE callback error: {}",
                            e.what()
                        );
                    }

                    // 回调控制连接生命周期，provider 本身保持有效。
                    return true;
                }
            );
        }
    );
}

void HttpSseServer::run() {
    running_.store(true);

    MCP_LOG_INFO(
        "Starting SSE HTTP server on {}:{} with {} workers",
        host_,
        port_,
        worker_count_
    );

    // listen() 在独立端口阻塞，连接由专用 ThreadPool 调度。
    const bool listening = impl_->server.listen(
        host_.c_str(),
        port_
    );

    if (!listening && running_.load()) {
        MCP_LOG_ERROR(
            "Failed to start SSE HTTP server on {}:{}",
            host_,
            port_
        );
    }

    running_.store(false);
    MCP_LOG_INFO("SSE HTTP server stopped");
}

void HttpSseServer::stop() {
    // stop() 会解除 listen() 阻塞，并关闭 SSE 服务的连接处理。
    running_.store(false);
    impl_->server.stop();
}

} // namespace mcp
