/**
 * @file http_jsonrpc.h
 * @brief 基于 HTTP 的 JSON-RPC 2.0 服务器
 * 支持 HTTP POST 请求和响应
 */

#pragma once

#include "jsonrpc.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>

namespace mcp {

class HttpJsonRpcServer {
public:
 
    explicit HttpJsonRpcServer(JsonRpcDispatcher dispatcher);

    HttpJsonRpcServer(
        JsonRpcDispatcher dispatcher,
        const std::string& host,
        int port
    );

    ~HttpJsonRpcServer();

    // 禁止拷贝
    HttpJsonRpcServer(const HttpJsonRpcServer&) = delete;
    HttpJsonRpcServer& operator=(const HttpJsonRpcServer&) = delete;

    void run();

    void stop();

    bool is_running() const { return running_.load(); }

    const std::string& get_host() const { return host_; }

    int get_port() const { return port_; }

private:
    JsonRpcDispatcher dispatcher_;
    std::string host_;
    int port_;
    std::atomic<bool> running_{false};

    // 使用 pimpl 模式隐藏 httplib 实现细节
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::string handle_request(const std::string& request_body);
};

} // namespace mcp
