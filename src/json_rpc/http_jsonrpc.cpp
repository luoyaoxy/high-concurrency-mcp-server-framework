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

#include "jsonrpc_task.h"

#include <stdexcept>

#include <chrono>
#include <optional>
#include <vector>

namespace mcp {

// Pimpl 实现类
class HttpJsonRpcServer::Impl {
public:
    httplib::Server server;

    Impl() {
        // 设置日志回调
        // server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        //     MCP_LOG_INFO("HTTP {} {} -> {}", req.method, req.path, res.status);
        // });

        // 设置错误处理
        server.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
            json error_response = {
                {"jsonrpc", "2.0"},
                {"error", {
                    {"code", -32603},
                    {"message", "Internal server error"}
                }},
                {"id", nullptr}
            };
            res.set_content(error_response.dump(), "application/json");
        });
    }
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        try {
            std::string response = handle_request(req.body);
            res.set_content(response, "application/json");
            res.status = 200;
        } catch (const std::exception& e) {
            MCP_LOG_ERROR("Error handling request: {}", e.what());
            json error_response = {
                {"jsonrpc", "2.0"},
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
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

std::string HttpJsonRpcServer::handle_request(const std::string& request_body) {
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
                    // 从 JSON 解析请求
                    JsonRpcRequest req;
                    req.jsonrpc = single_req_json.value("jsonrpc", "2.0");
                    req.method = single_req_json.at("method").get<std::string>();

                    if (single_req_json.contains("id")) {
                        req.id = single_req_json["id"];
                    }

                    if (single_req_json.contains("params")) {
                        req.params = single_req_json["params"];
                    }

                    // 批量中的取消命令同样不能排入业务队列。
                    if (req.method == "$/cancelRequest") {
                        if (
                            !req.params.has_value() ||
                            !req.params->is_object() ||
                            !req.params->contains("id")
                        ) {
                            throw std::invalid_argument(
                                "$/cancelRequest requires params.id"
                            );
                        }

                        runtime_->cancel_request((*req.params)["id"]);

                        // 控制 notification 没有响应，不加入 batch_response。
                        entry.is_notification = true;
                        MCP_LOG_DEBUG(
                            "HTTP batch_index={} processed $/cancelRequest",
                            entry.index
                        );
                        continue;
                    }

                    // 每个批量元素都有独立的超时截止时间。
                    entry.task = make_jsonrpc_task(
                        req,
                        "http",
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
                    error_response.jsonrpc = "2.0";
                    error_response.id = nullptr;
                    error_response.error = JsonRpcError{
                        jsonrpc_errc::InternalError,
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
        JsonRpcRequest request;
        request.jsonrpc = request_json.value("jsonrpc", "2.0");
        request.method = request_json.at("method").get<std::string>();

        if (request_json.contains("id")) {
            request.id = request_json["id"];
        }

        if (request_json.contains("params")) {
            request.params = request_json["params"];
        }

        // $/cancelRequest 是控制命令，直接作用于 runtime，
        // 不应排入任何业务 lane。
        if (request.method == "$/cancelRequest") {
            if (
                !request.params.has_value() ||
                !request.params->is_object() ||
                !request.params->contains("id")
            ) {
                throw std::invalid_argument(
                    "$/cancelRequest requires params.id"
                );
            }

            runtime_->cancel_request((*request.params)["id"]);

            // $/cancelRequest 按 notification 使用，不返回 JSON-RPC 响应。
            return "";
        }

        JsonRpcTask task = make_jsonrpc_task(
            request,
            "http",
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
            {"jsonrpc", "2.0"},
            {"error", {
                {"code", jsonrpc_errc::ParseError},
                {"message", std::string("Parse error: ") + e.what()}
            }},
            {"id", nullptr}
        };
        return error_response.dump();
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Error handling request: {}", e.what());
        json error_response = {
            {"jsonrpc", "2.0"},
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
