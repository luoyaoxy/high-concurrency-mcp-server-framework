/**
 * @file http_jsonrpc.cpp
 * @brief 基于 HTTP 的 JSON-RPC 2.0 服务器实现
 */

#include "http_jsonrpc.h"
#include "jsonrpc_serialization.h"
#include "logger.h"
#include "config.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>

#include "jsonrpc_task.h"

#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace mcp {
namespace {

constexpr char kMcpSessionIdHeader[] = "Mcp-Session-Id";

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

bool IsInitializeRequest(const std::string& request_body) {
    try {
        const json request = json::parse(request_body);
        return request.is_object() &&
            request.contains("method") &&
            request["method"].is_string() &&
            request["method"] == "initialize";
    } catch (const json::parse_error&) {
        return false;
    }
}

bool IsSuccessfulResponse(const std::string& response_body) {
    try {
        const json response = json::parse(response_body);
        return response.is_object() && response.contains("result");
    } catch (const json::parse_error&) {
        return false;
    }
}

std::string GenerateSessionId() {
    std::array<unsigned char, 32> random_bytes{};
    if (
        RAND_bytes(
            random_bytes.data(),
            static_cast<int>(random_bytes.size())
        ) != 1
    ) {
        throw std::runtime_error("Failed to generate MCP session ID");
    }

    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string session_id;
    session_id.reserve(random_bytes.size() * 2);
    for (unsigned char byte : random_bytes) {
        session_id.push_back(kHexDigits[byte >> 4]);
        session_id.push_back(kHexDigits[byte & 0x0f]);
    }
    return session_id;
}

std::string SessionClientId(const std::string& session_id) {
    return "http:session:" + session_id;
}

std::string LegacyClientId() {
    // 无 session header 时无法安全关联多个 HTTP 请求；每个 POST 使用
    // 独立标识，既保留普通请求与单个 batch，又避免取消其他客户端任务。
    return "http:legacy:" + GenerateSessionId();
}

}  // namespace

// Pimpl 实现类
class HttpJsonRpcServer::Impl {
public:
    httplib::Server server;

    void register_session(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (!sessions_.insert(session_id).second) {
            throw std::runtime_error("Duplicate MCP session ID");
        }
    }

    bool has_session(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        return sessions_.find(session_id) != sessions_.end();
    }

    Impl() {
        // 设置日志回调
        // server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        //     MCP_LOG_INFO("HTTP {} {} -> {}", req.method, req.path, res.status);
        // });

        // 设置错误处理
        server.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
            json error_response = {
                {"jsonrpc", kJsonRpcVersion},
                {"error", {
                    {"code", -32603},
                    {"message", "Internal server error"}
                }},
                {"id", nullptr}
            };
            res.set_content(error_response.dump(), "application/json");
        });
    }

private:
    std::mutex sessions_mutex_;
    std::unordered_set<std::string> sessions_;
};

HttpJsonRpcServer::HttpJsonRpcServer(
    std::shared_ptr<JsonRpcTaskRuntime> runtime
)
    : HttpJsonRpcServer(
        std::move(runtime),
        "0.0.0.0",
        MCP_CONFIG.GetServerPort()
    ) {
    MCP_LOG_INFO("HTTP JSON-RPC server initialized from config");
}

HttpJsonRpcServer::HttpJsonRpcServer(
    std::shared_ptr<JsonRpcTaskRuntime> runtime,
    const std::string& host,
    int port
)
    : runtime_(std::move(runtime))
    , host_(host)
    , port_(port)
    , impl_(std::make_unique<Impl>())
{
    if (!runtime_) {
        throw std::invalid_argument("JSON-RPC task runtime is required");
    }

    MCP_LOG_INFO("HTTP JSON-RPC server created on {}:{}", host_, port_);

    // 注册 POST /jsonrpc 端点
    impl_->server.Post("/jsonrpc", [this](const httplib::Request& req, httplib::Response& res) {
        // MCP_LOG_DEBUG("Received JSON-RPC request, body size: {}", req.body.size());

        // 设置 CORS 头
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Mcp-Session-Id"
        );
        res.set_header("Access-Control-Expose-Headers", kMcpSessionIdHeader);

        try {
            const bool is_initialize = IsInitializeRequest(req.body);
            std::string session_id =
                req.get_header_value(kMcpSessionIdHeader);

            if (!session_id.empty() && !impl_->has_session(session_id)) {
                json error_response = {
                    {"jsonrpc", kJsonRpcVersion},
                    {"error", {
                        {"code", -32000},
                        {"message", "MCP session not found"}
                    }},
                    {"id", nullptr}
                };
                res.set_content(error_response.dump(), "application/json");
                res.status = 404;
                return;
            }

            const bool create_session = is_initialize && session_id.empty();
            if (create_session) {
                session_id = GenerateSessionId();
            }

            const std::string client_id = session_id.empty()
                ? LegacyClientId()
                : SessionClientId(session_id);
            std::string response = handle_request(
                req.body,
                client_id
            );

            if (create_session && IsSuccessfulResponse(response)) {
                impl_->register_session(session_id);
                res.set_header(kMcpSessionIdHeader, session_id);
            }
            res.set_content(response, "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            MCP_LOG_ERROR("Error handling request: {}", e.what());
            json error_response = {
                {"jsonrpc", kJsonRpcVersion},
                {"error", {
                    {"code", -32603},
                    {"message", e.what()}
                }},
                {"id", nullptr}
            };
            res.set_content(error_response.dump(), "application/json");
            res.status = 500;
        }
    });

    // 处理 OPTIONS 请求（CORS 预检）
    impl_->server.Options("/jsonrpc", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Mcp-Session-Id"
        );
        res.status = 204;
    });

    // 注册健康检查端点
    impl_->server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
        json health = {
            {"status", "ok"},
            {"service", "mcp-http-jsonrpc"}
        };
        res.set_content(health.dump(), "application/json");
    });

    // 注册根路径（返回服务信息）
    impl_->server.Get("/", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json info = {
            {"service", "MCP HTTP JSON-RPC Server"},
            {"version", "1.0.0"},
            {"endpoints", {
                {{"path", "/jsonrpc"}, {"method", "POST"}, {"description", "JSON-RPC 2.0 endpoint"}},
                {{"path", "/health"}, {"method", "GET"}, {"description", "Health check"}},
                {{"path", "/"}, {"method", "GET"}, {"description", "Server information"}}
            }}
        };
        res.set_content(info.dump(2), "application/json");
    });
}

HttpJsonRpcServer::~HttpJsonRpcServer() {
    stop();
}

void HttpJsonRpcServer::run() {
    if (running_.exchange(true)) {
        MCP_LOG_WARN("Server is already running");
        return;
    }

    MCP_LOG_INFO("Starting HTTP JSON-RPC server on {}:{}", host_, port_);

    // 启动服务器（阻塞）
    if (!impl_->server.listen(host_, port_)) {
        running_ = false;
        throw std::runtime_error("Failed to start HTTP server on " + host_ + ":" + std::to_string(port_));
    }

    MCP_LOG_INFO("HTTP JSON-RPC server stopped");
    running_ = false;
}

void HttpJsonRpcServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    MCP_LOG_INFO("Stopping HTTP JSON-RPC server...");
    impl_->server.stop();
}

std::string HttpJsonRpcServer::handle_request(
    const std::string& request_body,
    const std::string& client_id
) {
    MCP_LOG_DEBUG("Request body: {}", request_body);

    // 传输层请求数包含解析失败的 HTTP JSON-RPC 请求。
    runtime_->record_http_request();

    try {
        // 解析 JSON
        json request_json = json::parse(request_body);

        // 检查是否为批量请求
        if (request_json.is_array()) {
            runtime_->record_batch_request(request_json.size());
            json batch_response = json::array();

            // 每个输入元素独立保存任务与结果，后续可并发 map、统一 gather。
            struct BatchEntry {
                std::size_t index;
                std::optional<JsonRpcTask> task;
                std::optional<TaskSubmission> submission;
                std::optional<JsonRpcResponse> immediate_response;
                std::optional<JsonRpcResponse> completed_response;
                bool is_notification = false;
            };

            std::vector<BatchEntry> batch_entries;
            batch_entries.reserve(request_json.size());

            for (
                std::size_t index = 0;
                index < request_json.size();
                ++index
            ) {
                const json& single_req_json = request_json[index];
                batch_entries.push_back(BatchEntry{index});
                BatchEntry& entry = batch_entries.back();

                try {
                    JsonRpcRequest req =
                        single_req_json.get<JsonRpcRequest>();

                    // 批量中的取消 notification 同样不能排入业务队列。
                    if (IsCancellationNotification(req)) {
                        const std::optional<json> request_id =
                            CancellationRequestId(req);
                        if (!request_id.has_value()) {
                            throw std::invalid_argument(
                                "cancellation notification requires a request id"
                            );
                        }

                        runtime_->cancel_request(client_id, *request_id);

                        // 控制 notification 没有响应，不加入 batch_response。
                        entry.is_notification = true;
                        MCP_LOG_DEBUG(
                            "HTTP batch_index={} processed cancellation",
                            entry.index
                        );
                        continue;
                    }

                    // 每个批量元素都有独立的超时截止时间。
                    entry.task = make_jsonrpc_task(
                        req,
                        client_id,
                        std::chrono::milliseconds(MCP_CONFIG.GetRequestTimeoutMs())
                    );

                    entry.is_notification = entry.task->is_notification();
                    entry.submission = runtime_->submit(*entry.task);

                    // Map 阶段只提交任务；所有结果在后续 Gather 阶段统一收集。
                    MCP_LOG_DEBUG(
                        "[trace_id={}] batch_index={} submitted: task_id={}, method={}",
                        entry.task->trace_id,
                        entry.index,
                        entry.task->task_id,
                        entry.task->method
                    );

                } catch (const std::exception& e) {
                    MCP_LOG_ERROR("Error in batch request: {}", e.what());
                    JsonRpcResponse error_response;
                    error_response.jsonrpc = kJsonRpcVersion;
                    error_response.id = nullptr;
                    error_response.error = JsonRpcError{
                        jsonrpc_errc::InvalidRequest,
                        e.what(),
                        std::nullopt
                    };
                    entry.immediate_response = error_response;
                    MCP_LOG_DEBUG(
                        "HTTP batch_index={} stored immediate error response",
                        entry.index
                    );
                }
            }

            // Gather 阶段：所有任务已提交后，再统一取得各自结果。
            for (BatchEntry& entry : batch_entries) {
                if (entry.immediate_response.has_value()) {
                    MCP_LOG_DEBUG(
                        "HTTP batch_index={} gathered immediate response",
                        entry.index
                    );
                    continue;
                }

                // notification 已执行，但 JSON-RPC 规定不返回响应。
                if (entry.is_notification || !entry.submission.has_value()) {
                    MCP_LOG_DEBUG(
                        "HTTP batch_index={} gathered notification",
                        entry.index
                    );
                    continue;
                }

                JsonRpcTaskResult response = runtime_->wait_for_result(
                    *entry.task,
                    entry.submission->result
                );

                if (response.has_value()) {
                    entry.completed_response = std::move(*response);
                }

                MCP_LOG_DEBUG(
                    "[trace_id={}] batch_index={} gathered: task_id={}, method={}, has_response={}",
                    entry.task->trace_id,
                    entry.index,
                    entry.task->task_id,
                    entry.task->method,
                    response.has_value()
                );
            }

            // 使用原始下标组装响应，完成顺序不会影响客户端看到的顺序。
            std::vector<std::optional<JsonRpcResponse>> responses_by_index(
                request_json.size()
            );

            for (const BatchEntry& entry : batch_entries) {
                if (entry.immediate_response.has_value()) {
                    responses_by_index[entry.index] =
                        entry.immediate_response;
                } else if (entry.completed_response.has_value()) {
                    responses_by_index[entry.index] =
                        entry.completed_response;
                }
            }

            for (const auto& response : responses_by_index) {
                if (response.has_value()) {
                    batch_response.push_back(json(*response));
                }
            }

            // 一个批量请求可能全部是 notification；此时不应返回 JSON-RPC 响应。
            if (batch_response.empty()) {
                MCP_LOG_DEBUG("HTTP batch contains only notifications, no response");
                return "";
            }

            std::string response = batch_response.dump();
            MCP_LOG_DEBUG("Batch response: {}", response);
            return response;
        }

        // 单个请求
        JsonRpcRequest request = request_json.get<JsonRpcRequest>();

        // 取消 notification 直接作用于 runtime，不排入业务 lane。
        if (IsCancellationNotification(request)) {
            const std::optional<json> request_id =
                CancellationRequestId(request);
            if (!request_id.has_value()) {
                throw std::invalid_argument(
                    "cancellation notification requires a request id"
                );
            }

            runtime_->cancel_request(client_id, *request_id);

            // cancellation notification 不返回 JSON-RPC 响应。
            return "";
        }

        JsonRpcTask task = make_jsonrpc_task(
            request,
            client_id,
            std::chrono::milliseconds(MCP_CONFIG.GetRequestTimeoutMs())
        );

        TaskSubmission submission = runtime_->submit(task);
        JsonRpcTaskResult response =
            runtime_->wait_for_result(task, submission.result);

        // notification 没有 id，执行后不返回 JSON-RPC 响应。
        if (!response.has_value()) {
            MCP_LOG_DEBUG(
                "[trace_id={}] HTTP notification finished, no response",
                task.trace_id
            );
            return "";
        }

        // 复用已有 JsonRpcResponse 的 JSON 序列化定义。
        return json(*response).dump();

    } catch (const json::parse_error& e) {
        MCP_LOG_ERROR("JSON parse error: {}", e.what());
        json error_response = {
            {"jsonrpc", kJsonRpcVersion},
            {"error", {
                {"code", jsonrpc_errc::ParseError},
                {"message", std::string("Parse error: ") + e.what()}
            }},
            {"id", nullptr}
        };
        return error_response.dump();
    } catch (const std::invalid_argument& e) {
        MCP_LOG_ERROR("Invalid JSON-RPC request: {}", e.what());
        json error_response = {
            {"jsonrpc", kJsonRpcVersion},
            {"error", {
                {"code", jsonrpc_errc::InvalidRequest},
                {"message", std::string("Invalid Request: ") + e.what()}
            }},
            {"id", nullptr}
        };
        return error_response.dump();
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Error handling request: {}", e.what());
        json error_response = {
            {"jsonrpc", kJsonRpcVersion},
            {"error", {
                {"code", jsonrpc_errc::InternalError},
                {"message", std::string("Internal error: ") + e.what()}
            }},
            {"id", nullptr}
        };
        return error_response.dump();
    }
}

} // namespace mcp
