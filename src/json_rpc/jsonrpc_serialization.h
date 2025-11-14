#pragma once

#include "jsonrpc.h"

namespace mcp {

// JSON-RPC 2.0 类型的 nlohmann::json 序列化定义
void to_json(json& j, const JsonRpcRequest& r);
void from_json(const json& j, JsonRpcRequest& r);
void to_json(json& j, const JsonRpcError& e);
void from_json(const json& j, JsonRpcError& e);
void to_json(json& j, const JsonRpcResponse& r);
void from_json(const json& j, JsonRpcResponse& r);

} // namespace mcp

