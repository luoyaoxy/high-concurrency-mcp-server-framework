/**
 * @file stdio_jsonrpc.cpp
 * @brief 基于 stdio 的 JSON-RPC 2.0 服务器
 * 通过标准输入输出通信，使用 Content-Length 头分隔消息
 */

#include "jsonrpc.h"
#include "jsonrpc_serialization.h"
#include "logger.h"

#include <iostream>
#include <sstream>
#include <limits>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mcp {

// ============================================================================
// JsonRpcDispatcher - 方法调度器
// ============================================================================

/// 注册 RPC 方法处理器
void JsonRpcDispatcher::registerHandler(const std::string& method, Handler handler) {
    handlers_[method] = std::move(handler);
}

/// 检查方法是否已注册
bool JsonRpcDispatcher::hasHandler(const std::string& method) const {
    return handlers_.find(method) != handlers_.end();
}

/// 调用已注册的方法
json JsonRpcDispatcher::call(const std::string& method, const json& params) const {
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
        throw std::runtime_error("Method not found");
    }
    return it->second(params);
}

// ============================================================================
// StdioJsonRpcServer
// ============================================================================

StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

StdioJsonRpcServer::StdioJsonRpcServer(JsonRpcDispatcher dispatcher, std::istream& in, std::ostream& out)
    : dispatcher_(std::move(dispatcher)), in_(in), out_(out) {}


 // 读取一条完整的 JSON-RPC 消息

bool StdioJsonRpcServer::readMessage(std::string& out_body) {
    out_body.clear();

    std::string line;
    size_t content_length = 0;
    bool found_content_length = false;

    // 读取头部，大小写不敏感，允许额外头部（Content-Type 等）
    while (std::getline(in_, line)) {
        // 每行末尾如果带有回车（Windows 风格 CRLF），手动去掉
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // 空行表示头部结束，后面就是消息体
        if (line.empty()) {
            break;  // 头部结束
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) {
            // 没有冒号视为非法头，跳过
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // 去掉 value 前导空格，避免“Content-Length:  42”这种情况
        size_t pos = value.find_first_not_of(' ');
        if (pos != std::string::npos) {
            MCP_LOG_DEBUG("Value: {}", value);
            value = value.substr(pos);
        }

        // key 转小写以实现大小写不敏感
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });

        if (key == "content-length") {
            try {
                content_length = static_cast<size_t>(std::stoul(value));
                found_content_length = true;
            } catch (...) {
                MCP_LOG_ERROR("Invalid Content-Length: {}", value);
                return false;
            }
        } else if (key == "content-type") {
            // Content-Type 头部仅记录日志，保持兼容
            MCP_LOG_DEBUG("Content-Type: {}", value);
        } else {
            // 其他自定义头部：记录后忽略
            MCP_LOG_DEBUG("Ignore header: {}: {}", key, value);
        }
    }
    MCP_LOG_INFO("Found Content-Length: {}", found_content_length);    
    MCP_LOG_INFO("Content-Length: {}", content_length);

    // 改进：区分 "未找到 Content-Length" 和 "长度为 0"
    if (!found_content_length) {
        return false;
    }

    if (content_length == 0) {
        // 允许空消息体
        return true;
    }

    // 按长度读取消息体
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

    // 改进：添加日志，便于调试
    if (total_read != content_length) {
        MCP_LOG_ERROR("Incomplete message: expected {} bytes, got {}", content_length, total_read);
        return false;
    }

    return true;
}

/// 写入消息（添加 Content-Length 头）
void StdioJsonRpcServer::writeMessage(const json& msg) {
    std::string payload = msg.dump();
    out_ << "Content-Length: " << payload.size() << "\r\n\r\n";
    out_ << payload;
    out_.flush();
}

/**
 * 处理单个 JSON-RPC 请求
 * 错误码: -32700(Parse), -32600(Invalid), -32601(NotFound), -32602(Params), -32603(Internal)
 */
JsonRpcResponse StdioJsonRpcServer::handleRequest(const JsonRpcRequest& req) {
    JsonRpcResponse resp;
    resp.jsonrpc = "2.0";
    if (req.id.has_value()) {
        resp.id = *req.id;
    } else {
        resp.id = nullptr;
    }

    try {
        // 验证请求格式
        if (req.jsonrpc != "2.0") {
            throw std::invalid_argument("Invalid Request: jsonrpc must be 2.0");
        }
        if (req.method.empty()) {
            throw std::invalid_argument("Invalid Request: method missing");
        }

        const std::string method = req.method;
        json params = req.params.has_value() ? *req.params : json::object();

        MCP_LOG_DEBUG("Handling method: {}", method);

        // 检查方法是否存在
        if (!dispatcher_.hasHandler(method)) {
            resp.error = JsonRpcError{jsonrpc_errc::MethodNotFound, "Method not found", std::nullopt};
            return resp;
        }

        // 调用方法处理器
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
    } catch (const json::parse_error& ex) {
        // 区分 JSON 解析错误
        resp.result.reset();
        resp.error = JsonRpcError{jsonrpc_errc::ParseError, ex.what(), std::nullopt};
    } catch (const std::exception& ex) {
        resp.result.reset();
        resp.error = JsonRpcError{jsonrpc_errc::InternalError, ex.what(), std::nullopt};
    }

    return resp;
}

/// 运行服务器主循环（阻塞），从 stdin 读取请求并输出到 stdout
void StdioJsonRpcServer::run() {
    MCP_LOG_INFO("JSON-RPC stdio server starting...");

    std::string body;

    while (true) {
        // 读取消息
        if (!readMessage(body)) {
            if (in_.eof()) {
                MCP_LOG_INFO("stdin EOF reached, exiting");
                break;
            }
            continue;
        }

        // 解析并处理
        try {
            json j = json::parse(body);

            // 尝试解析成请求对象；若结构不合法，返回 InvalidRequest
            JsonRpcRequest req;
            try {
                req = j.get<JsonRpcRequest>();
            } catch (const std::exception& ex) {
                JsonRpcResponse resp;
                resp.id = j.contains("id") ? j["id"] : nullptr;
                resp.error = JsonRpcError{jsonrpc_errc::InvalidRequest, ex.what(), std::nullopt};
                writeMessage(json(resp));
                continue;
            }

            const bool is_notification = !req.id.has_value();  // 通知无需响应
            auto resp = handleRequest(req);

            if (!is_notification) {
                writeMessage(json(resp));
            }
        } catch (const json::parse_error& ex) {
            // 单独捕获 JSON 解析错误
            MCP_LOG_ERROR("JSON parse error: {}", ex.what());
            JsonRpcResponse resp;
            resp.id = nullptr;
            resp.error = JsonRpcError{jsonrpc_errc::ParseError, ex.what(), std::nullopt};
            writeMessage(json(resp));
        }
    }
}

} // namespace mcp
