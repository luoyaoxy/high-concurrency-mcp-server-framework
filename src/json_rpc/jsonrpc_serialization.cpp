#include "mcp/jsonrpc_serialization.h"

namespace mcp {

void to_json(json& j, const JsonRpcError& e) {
    j = json{{"code", e.code}, {"message", e.message}};
    if (e.data) j["data"] = *e.data;
}

void from_json(const json& j, JsonRpcError& e) {
    e.code = j.at("code").get<int>();
    e.message = j.at("message").get<std::string>();
    if (j.contains("data")) e.data = j.at("data");
}

void to_json(json& j, const JsonRpcResponse& r) {
    j = json{{"jsonrpc", "2.0"}, {"id", r.id}};
    if (r.result) j["result"] = *r.result;
    if (r.error) j["error"] = *r.error;
}

void from_json(const json& j, JsonRpcResponse& r) {
    r.jsonrpc = j.at("jsonrpc").get<std::string>();
    r.id = j.at("id");
    if (j.contains("result")) r.result = j.at("result");
    if (j.contains("error")) r.error = j.at("error").get<JsonRpcError>();
}

} // namespace mcp


