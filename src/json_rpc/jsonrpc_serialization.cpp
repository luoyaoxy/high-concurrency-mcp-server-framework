

#include "jsonrpc_serialization.h"

namespace mcp {

// =========================
// JsonRpcRequest
// =========================
void to_json(json& j, const JsonRpcRequest& r) {
    j = json{{"jsonrpc", "2.0"}, {"method", r.method}};
    if (r.id.has_value()) {
        j["id"] = *r.id;
    }
    if (r.params.has_value()) {
        j["params"] = *r.params;
    }
}

void from_json(const json& j, JsonRpcRequest& r) {
    r.jsonrpc = j.at("jsonrpc").get<std::string>();
    r.method = j.at("method").get<std::string>();
    if (j.contains("id")) {
        r.id = j.at("id");
    } else {
        r.id.reset();
    }
    if (j.contains("params")) {
        r.params = j.at("params");
    } else {
        r.params.reset();
    }
}

void to_json(json& j, const JsonRpcError& e) {
    j = json{{"code", e.code}, {"message", e.message}};
    if (e.data) {
        j["data"] = *e.data;
    }
}

/// 从 JSON 解析 JsonRpcError
void from_json(const json& j, JsonRpcError& e) {
    e.code = j.at("code").get<int>();
    e.message = j.at("message").get<std::string>();
    if (j.contains("data")) {
        e.data = j.at("data");
    } else {
        e.data.reset();
    }
}


void to_json(json& j, const JsonRpcResponse& r) {
    j = json{{"jsonrpc", "2.0"}, {"id", r.id}};

    // Enforce mutual exclusivity per JSON-RPC 2.0
    if (r.error.has_value()) {
        j["error"] = *r.error;
    } else if (r.result.has_value()) {
        j["result"] = *r.result;
    }
}

/// 从 JSON 解析 JsonRpcResponse
void from_json(const json& j, JsonRpcResponse& r) {
    r.jsonrpc = j.at("jsonrpc").get<std::string>();
    r.id = j.at("id");

    // Only one of result or error may be present
    if (j.contains("error")) {
        r.error = j.at("error").get<JsonRpcError>();
        r.result.reset();
    } else if (j.contains("result")) {
        r.result = j.at("result");
        r.error.reset();
    } else {
        r.result.reset();
        r.error.reset();
    }
}

} // namespace mcp
