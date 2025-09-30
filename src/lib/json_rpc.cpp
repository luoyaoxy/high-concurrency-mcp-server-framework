#include "mcp/json_rpc.h"
#include <stdexcept>

namespace mcp {

json JsonRpcRequest::ToJson() const {
    json j;
    j["jsonrpc"] = jsonrpc;
    j["method"] = method;
    if (!params.is_null()) {
        j["params"] = params;
    }
    if (id.has_value()) {
        j["id"] = id.value();
    }
    return j;
}

JsonRpcRequest JsonRpcRequest::FromJson(const json& j) {
    JsonRpcRequest req;
    
    if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
        throw std::invalid_argument("Invalid JSON-RPC version");
    }
    
    if (!j.contains("method") || !j["method"].is_string()) {
        throw std::invalid_argument("Missing or invalid method");
    }
    
    req.jsonrpc = j["jsonrpc"];
    req.method = j["method"];
    
    if (j.contains("params")) {
        req.params = j["params"];
    }
    
    if (j.contains("id")) {
        if (j["id"].is_string()) {
            req.id = j["id"].get<std::string>();
        } else if (j["id"].is_number()) {
            req.id = std::to_string(j["id"].get<int>());
        }
    }
    
    return req;
}

json JsonRpcResponse::ToJson() const {
    json j;
    j["jsonrpc"] = jsonrpc;
    
    if (result.has_value()) {
        j["result"] = result.value();
    }
    
    if (error.has_value()) {
        j["error"] = error.value();
    }
    
    if (id.has_value()) {
        j["id"] = id.value();
    } else {
        j["id"] = nullptr;
    }
    
    return j;
}

JsonRpcResponse JsonRpcResponse::FromJson(const json& j) {
    JsonRpcResponse resp;
    
    if (!j.contains("jsonrpc") || j["jsonrpc"] != "2.0") {
        throw std::invalid_argument("Invalid JSON-RPC version");
    }
    
    resp.jsonrpc = j["jsonrpc"];
    
    if (j.contains("result")) {
        resp.result = j["result"];
    }
    
    if (j.contains("error")) {
        resp.error = j["error"];
    }
    
    if (j.contains("id")) {
        if (j["id"].is_string()) {
            resp.id = j["id"].get<std::string>();
        } else if (j["id"].is_number()) {
            resp.id = std::to_string(j["id"].get<int>());
        }
    }
    
    return resp;
}

json JsonRpcError::ToJson() const {
    json j;
    j["code"] = code;
    j["message"] = message;
    if (data.has_value()) {
        j["data"] = data.value();
    }
    return j;
}

JsonRpcError JsonRpcError::FromJson(const json& j) {
    JsonRpcError err;
    
    if (!j.contains("code") || !j["code"].is_number()) {
        throw std::invalid_argument("Missing or invalid error code");
    }
    
    if (!j.contains("message") || !j["message"].is_string()) {
        throw std::invalid_argument("Missing or invalid error message");
    }
    
    err.code = j["code"];
    err.message = j["message"];
    
    if (j.contains("data")) {
        err.data = j["data"];
    }
    
    return err;
}

std::string JsonRpcHandler::ProcessMessage(const std::string& message) {
    try {
        auto j = json::parse(message);
        auto request = JsonRpcRequest::FromJson(j);
        auto response = HandleRequest(request);
        return response.ToJson().dump();
    } catch (const json::parse_error& e) {
        auto error_resp = CreateErrorResponse(std::nullopt, 
            error_codes::PARSE_ERROR, "Parse error");
        return error_resp.ToJson().dump();
    } catch (const std::invalid_argument& e) {
        auto error_resp = CreateErrorResponse(std::nullopt, 
            error_codes::INVALID_REQUEST, e.what());
        return error_resp.ToJson().dump();
    } catch (const std::exception& e) {
        auto error_resp = CreateErrorResponse(std::nullopt, 
            error_codes::INTERNAL_ERROR, e.what());
        return error_resp.ToJson().dump();
    }
}

JsonRpcResponse JsonRpcHandler::CreateErrorResponse(const std::optional<std::string>& id, 
                                                     int code, const std::string& message) {
    JsonRpcResponse response;
    response.id = id;
    
    JsonRpcError error;
    error.code = code;
    error.message = message;
    response.error = error.ToJson();
    
    return response;
}

JsonRpcResponse JsonRpcHandler::CreateSuccessResponse(const std::optional<std::string>& id, 
                                                       const json& result) {
    JsonRpcResponse response;
    response.id = id;
    response.result = result;
    return response;
}

} // namespace mcp