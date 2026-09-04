/**
 * @file stdio_jsonrpc.cpp
 * @brief 基于 stdio 的 JSON-RPC 2.0 服务器
 * 通过标准输入输出通信，每条 JSON-RPC 消息占一行
 */

#include "jsonrpc.h"
#include "jsonrpc_serialization.h"

#include "config.h"

#include "logger.h"

#include <iostream>
#include <sstream>
#include <limits>
#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <chrono>


#include "jsonrpc_task.h"

#include "jsonrpc_task_runtime.h"



namespace mcp {
namespace {

bool IsCancellationNotification(const JsonRpcRequest& request) {
    return !request.id.has_value() &&
        (request.method == "notifications/cancelled" ||
         request.method == "$/cancelRequest");
}

std::optional<json> CancellationRequestId(const JsonRpcRequest& request) {
    if (!request.params.has_value() || !request.params->is_object()) {
        return std::nullopt;
    }

    const char* id_field = request.method == "notifications/cancelled"
        ? "requestId"
        : "id";
    if (!request.params->contains(id_field)) {
        return std::nullopt;
    }
    return std::optional<json>{(*request.params)[id_field]};
}

std::string Lowercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

}  // namespace

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

StdioJsonRpcServer::StdioJsonRpcServer(
    std::shared_ptr<JsonRpcTaskRuntime> runtime
)
    : StdioJsonRpcServer(
        std::move(runtime),
        std::cin,
        std::cout
    ) {}

StdioJsonRpcServer::StdioJsonRpcServer(
    std::shared_ptr<JsonRpcTaskRuntime> runtime,
    std::istream& in,
    std::ostream& out
)
    : runtime_(std::move(runtime))
    , in_(in)
    , out_(out) {
    if (!runtime_) {
        throw std::invalid_argument("JSON-RPC task runtime is required");
    }
}


bool StdioJsonRpcServer::readMessage(std::string& out_body) {
    out_body.clear();

    std::string line;
    if (!std::getline(in_, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    if (framing_ == Framing::kUnknown) {
        const std::size_t colon = line.find(':');
        const std::string first_field = colon == std::string::npos
            ? std::string()
            : Lowercase(line.substr(0, colon));
        framing_ = first_field == "content-length"
            ? Framing::kContentLength
            : Framing::kNewlineDelimited;
    }

    if (framing_ == Framing::kNewlineDelimited) {
        out_body = std::move(line);
        return true;
    }

    std::size_t content_length = 0;
    bool found_content_length = false;
    while (true) {
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string key = Lowercase(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            const std::size_t value_start = value.find_first_not_of(" \t");
            value = value_start == std::string::npos
                ? std::string()
                : value.substr(value_start);

            if (key == "content-length") {
                try {
                    content_length = static_cast<std::size_t>(
                        std::stoull(value)
                    );
                    found_content_length = true;
                } catch (const std::exception&) {
                    MCP_LOG_ERROR("Invalid Content-Length: {}", value);
                    return false;
                }
            }
        }

        if (!std::getline(in_, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
    }

    if (!found_content_length || content_length == 0) {
        MCP_LOG_ERROR("Missing or empty Content-Length header");
        return false;
    }

    out_body.resize(content_length);
    std::size_t total_read = 0;

    while (total_read < content_length) {
        const std::streamsize to_read = static_cast<std::streamsize>(
            content_length - total_read
        );
        in_.read(&out_body[total_read], to_read);

        const std::streamsize just_read = in_.gcount();
        if (just_read <= 0) {
            break;
        }

        total_read += static_cast<std::size_t>(just_read);

        if (!in_.good() && !in_.eof()) {
            break;
        }
    }

    if (total_read != content_length) {
        MCP_LOG_ERROR(
            "Incomplete message: expected {} bytes, got {}",
            content_length,
            total_read
        );
        return false;
    }

    return true;
}

void StdioJsonRpcServer::writeMessage(const json& msg) {
    std::lock_guard<std::mutex> lock(output_mutex_);

    const std::string payload = msg.dump();
    if (framing_ == Framing::kContentLength) {
        out_ << "Content-Length: " << payload.size() << "\r\n\r\n";
        out_ << payload;
    } else {
        out_ << payload << '\n';
    }
    out_.flush();
}

void StdioJsonRpcServer::begin_pending_response() {
    std::lock_guard<std::mutex> lock(pending_response_mutex_);
    ++pending_response_count_;
}

void StdioJsonRpcServer::finish_pending_response() {
    std::lock_guard<std::mutex> lock(pending_response_mutex_);

    if (pending_response_count_ == 0) {
        MCP_LOG_ERROR("stdio pending response count underflow");
        return;
    }

    --pending_response_count_;
    pending_response_cv_.notify_all();
}

void StdioJsonRpcServer::wait_for_pending_responses() {
    std::unique_lock<std::mutex> lock(pending_response_mutex_);
    pending_response_cv_.wait(lock, [this] {
        return pending_response_count_ == 0;
    });
}

std::optional<JsonRpcResponse> StdioJsonRpcServer::handleRequest(
    const JsonRpcRequest& req
) {
    // 标准 MCP cancellation notification 不进入业务任务队列。
    if (IsCancellationNotification(req)) {
        const std::optional<json> request_id = CancellationRequestId(req);
        if (!request_id.has_value()) {
            MCP_LOG_WARN(
                "Ignoring invalid stdio cancellation notification"
            );
            return std::nullopt;
        }

        runtime_->cancel_request("stdio", *request_id);

        // 取消命令本身没有 JSON-RPC 响应。
        return std::nullopt;
    }

    JsonRpcTask task = make_jsonrpc_task(
        req,
        "stdio",
        std::chrono::milliseconds(MCP_CONFIG.GetRequestTimeoutMs())
    );

    // 任务完成后由 worker 写回响应，读取线程无需等待执行结果。
    begin_pending_response();

    runtime_->submit(
        std::move(task),
        [this](JsonRpcTaskResult result) {
            try {
                if (result.has_value()) {
                    writeMessage(json(*result));
                }
            } catch (const std::exception& e) {
                MCP_LOG_ERROR("Failed to write stdio JSON-RPC response: {}", e.what());
            } catch (...) {
                MCP_LOG_ERROR("Failed to write stdio JSON-RPC response");
            }

            finish_pending_response();
        }
    );

    // 响应由完成回调异步写出。
    return std::nullopt;
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

            // 当前服务器不会主动向客户端发 request；合法 response 仅记录并忽略。
            if (j.is_object() && !j.contains("method") &&
                (j.contains("result") || j.contains("error"))) {
                MCP_LOG_DEBUG("Ignoring unsolicited JSON-RPC response");
                continue;
            }

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

            std::optional<JsonRpcResponse> response = handleRequest(req);

            if (response.has_value()) {
                writeMessage(json(*response));
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

    // EOF 后等待已提交请求完成，避免回调访问已销毁的 server。
    wait_for_pending_responses();
}

} // namespace mcp
