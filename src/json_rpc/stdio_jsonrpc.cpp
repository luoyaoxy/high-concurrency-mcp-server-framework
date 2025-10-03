#include "mcp/jsonrpc.h"
#include "../include/mcp/jsonrpc_serialization.h"
#include "mcp/logger.h"

#include <iostream>
#include <sstream>
#include <limits>

namespace mcp {

// 序列化/反序列化已迁移到 jsonrpc_serialization.*

// ==============================
// Dispatcher 实现
// ==============================

void JsonRpcDispatcher::registerHandler(const std::string& method, Handler handler) {
    handlers_[method] = std::move(handler);
}

bool JsonRpcDispatcher::hasHandler(const std::string& method) const {
    return handlers_.find(method) != handlers_.end();
}

json JsonRpcDispatcher::call(const std::string& method, const json& params) const {
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
        throw std::runtime_error("Method not found");
    }
    return it->second(params);
}

// ==============================
// StdioJsonRpcServer 实现
// ==============================

StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher, std::istream& in, std::ostream& out)
    : dispatcher_(std::move(dispatcher)), in_(in), out_(out) {}

bool StdioJsonRpcServer::readMessage(std::string& out_body) {
    out_body.clear();

    std::string line;
    size_t content_length = 0;

    // 读取头部
    while (std::getline(in_, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break; // 头部结束
        }

        const std::string kHeader = "Content-Length:";
        if (line.rfind(kHeader, 0) == 0) {
            std::string value = line.substr(kHeader.size());
            // 去除前导空格
            size_t pos = value.find_first_not_of(' ');
            if (pos != std::string::npos) value = value.substr(pos);
            try {
                // 避免异常传播
                content_length = static_cast<size_t>(std::stoul(value));
            } catch (...) {
                content_length = 0;
            }
        }
    }

    if (content_length == 0) {
        return false; // 没有可读消息
    }

    // 按长度读取 body
    out_body.resize(content_length);
    size_t total_read = 0;
    while (total_read < content_length) {
        const std::streamsize to_read = static_cast<std::streamsize>(content_length - total_read);
        in_.read(&out_body[total_read], to_read);
        const std::streamsize just_read = in_.gcount();
        if (just_read <= 0) {
            break;
        }
        total_read += static_cast<size_t>(just_read);
        if (!in_.good() && !in_.eof()) {
            break;
        }
    }
    return total_read == content_length;
}

void StdioJsonRpcServer::writeMessage(const json& msg) {
    std::string payload = msg.dump();
    out_ << "Content-Length: " << payload.size() << "\r\n\r\n";
    out_ << payload;
    out_.flush();
}

JsonRpcResponse StdioJsonRpcServer::handleRequest(const json& req) {
    JsonRpcResponse resp;
    resp.jsonrpc = "2.0";
    resp.id = req.contains("id") ? req["id"] : nullptr;

    try {
        if (!req.contains("jsonrpc") || req["jsonrpc"] != "2.0") {
            throw std::invalid_argument("Invalid Request: jsonrpc must be 2.0");
        }
        if (!req.contains("method") || !req["method"].is_string()) {
            throw std::invalid_argument("Invalid Request: method missing");
        }

        const std::string method = req["method"].get<std::string>();
        json params = req.contains("params") ? req["params"] : json::object();

        if (!dispatcher_.hasHandler(method)) {
            resp.error = JsonRpcError{jsonrpc_errc::MethodNotFound, "Method not found", std::nullopt};
            return resp;
        }

        try {
            json result = dispatcher_.call(method, params);
            resp.result = std::move(result);
            resp.error.reset();
        } catch (const std::invalid_argument& ex) {
            resp.result.reset();
            resp.error = JsonRpcError{jsonrpc_errc::InvalidParams, ex.what(), std::nullopt};
        } catch (const std::exception& ex) {
            resp.result.reset();
            resp.error = JsonRpcError{jsonrpc_errc::InternalError, ex.what(), std::nullopt};
        }
    } catch (const std::invalid_argument& ex) {
        resp.result.reset();
        resp.error = JsonRpcError{jsonrpc_errc::InvalidRequest, ex.what(), std::nullopt};
    } catch (const std::exception& ex) {
        resp.result.reset();
        resp.error = JsonRpcError{jsonrpc_errc::ParseError, ex.what(), std::nullopt};
    }

    return resp;
}

void StdioJsonRpcServer::run() {
    MCP_LOG_INFO("JSON-RPC stdio server starting...");
    std::string body;
    while (true) {
        if (!readMessage(body)) {
            if (in_.eof()) {
                MCP_LOG_INFO("stdin EOF reached, exiting");
                break;
            }
            // 没有消息，继续读
            continue;
        }

        try {
            json req = json::parse(body);
            // JSON-RPC 通知：无 id，不应返回响应
            const bool is_notification = !req.contains("id");
            auto resp = handleRequest(req);
            if (!is_notification) {
                writeMessage(json(resp));
            }
        } catch (const std::exception& ex) {
            // 无法解析为对象：返回 Parse error，id 为 null
            JsonRpcResponse resp;
            resp.id = nullptr;
            resp.error = JsonRpcError{jsonrpc_errc::ParseError, ex.what(), std::nullopt};
            writeMessage(json(resp));
        }
    }
}

} // namespace mcp


