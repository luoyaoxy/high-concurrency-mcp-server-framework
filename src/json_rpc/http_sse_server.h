#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace mcp {

// 只承载 SSE 长连接的独立 HTTP 服务。
class HttpSseServer {
public:
    // 回调通过 send(event_id, data) 推送数据；false 表示连接已断开。
    using SseSend = std::function<bool(
        std::optional<std::uint64_t> event_id,
        const std::string& data
    )>;

    using SseCallback = std::function<
        void(std::optional<std::uint64_t> last_event_id, const SseSend& send)
    >;

    HttpSseServer(
        std::string host,
        int port,
        std::size_t worker_count
    );

    ~HttpSseServer();

    HttpSseServer(const HttpSseServer&) = delete;
    HttpSseServer& operator=(const HttpSseServer&) = delete;

    // 注册一个流式 SSE GET 端点。
    void register_sse_endpoint(
        const std::string& path,
        SseCallback callback
    );

    // 在专用 SSE worker pool 上开始监听。
    void run();

    // 停止监听并让 run() 返回。
    void stop();

    bool is_running() const {
        return running_.load();
    }

    const std::string& get_host() const {
        return host_;
    }

    int get_port() const {
        return port_;
    }

private:
    // 隐藏 httplib::Server，避免它出现在公开头文件中。
    class Impl;

    std::string host_;
    int port_;
    std::size_t worker_count_;
    std::atomic<bool> running_{false};
    std::unique_ptr<Impl> impl_;
};

} // namespace mcp
