#pragma once

#include "jsonrpc_task.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace mcp {

// 将 JSON-RPC request id 映射到可共享的取消令牌。
class JsonRpcCancellationRegistry {
public:
    // 请求成功提交前登记其取消令牌。
    void register_request(
        const std::string& client_id,
        const json& request_id,
        CancellationToken cancellation
    );

    // 按 client id 和 request id 标记取消；找不到时返回 false。
    bool cancel_request(
        const std::string& client_id,
        const json& request_id
    );

    // 请求完成、拒绝或超时后移除登记，避免表持续增长。
    void unregister_request(
        const std::string& client_id,
        const json& request_id
    );

private:
    // client id 与 JSON-RPC id 共同组成稳定 map key。
    static std::string make_key(
        const std::string& client_id,
        const json& request_id
    );

    std::mutex mutex_;
    std::unordered_map<std::string, CancellationToken> requests_;
};

} // namespace mcp
