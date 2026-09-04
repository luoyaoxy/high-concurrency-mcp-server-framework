#include "jsonrpc_cancellation_registry.h"

#include <utility>

namespace mcp {

void JsonRpcCancellationRegistry::register_request(
    const std::string& client_id,
    const json& request_id,
    CancellationToken cancellation
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 同一客户端相同 id 的新请求覆盖旧登记，使用最新 token。
    requests_[make_key(client_id, request_id)] = std::move(cancellation);
}

bool JsonRpcCancellationRegistry::cancel_request(
    const std::string& client_id,
    const json& request_id
) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = requests_.find(make_key(client_id, request_id));
    if (it == requests_.end()) {
        return false;
    }

    // token 副本共享同一个原子取消状态。
    it->second.cancel();
    return true;
}

void JsonRpcCancellationRegistry::unregister_request(
    const std::string& client_id,
    const json& request_id
) {
    std::lock_guard<std::mutex> lock(mutex_);

    requests_.erase(make_key(client_id, request_id));
}

std::string JsonRpcCancellationRegistry::make_key(
    const std::string& client_id,
    const json& request_id
) {
    // dump() 保留 JSON 类型差异：数字 7 与字符串 "7" 不会混淆。
    return json::array({client_id, request_id}).dump();
}

} // namespace mcp
