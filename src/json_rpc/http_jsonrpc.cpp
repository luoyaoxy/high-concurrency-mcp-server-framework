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

// 构造函数（使用配置文件）
HttpJsonRpcServer::HttpJsonRpcServer(JsonRpcDispatcher dispatcher)
    : HttpJsonRpcServer(std::move(dispatcher), "0.0.0.0", MCP_CONFIG.GetServerPort())
{
    MCP_LOG_INFO("HTTP JSON-RPC server initialized from config");
}

// 构造函数（手动指定参数）
HttpJsonRpcServer::HttpJsonRpcServer(
    JsonRpcDispatcher dispatcher,
    const std::string& host,
    int port
)
    : dispatcher_(std::move(dispatcher))
    , host_(host)
    , port_(port)
    , impl_(std::make_unique<Impl>())
{
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
                {{"path", "/sse/events"}, {"method", "GET"}, {"description", "Server status event stream (SSE)"}},
                {{"path", "/sse/tool_calls"}, {"method", "GET"}, {"description", "Tool call monitoring stream (SSE)"}},
                {{"path", "/"}, {"method", "GET"}, {"description", "Server information"}}
            }}
        };
        res.set_content(info.dump(2), "application/json");
    });
}

void HttpJsonRpcServer::register_sse_endpoint(const std::string& path, SseCallback callback) {
    impl_->server.Get(path, [callback](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("X-Accel-Buffering", "no");

        res.set_chunked_content_provider(
            "text/event-stream",
            [callback](size_t /*offset*/, httplib::DataSink& sink) {
                auto send_event = [&sink](const std::string& data) {
                    std::string event = "data: " + data + "\n\n";
                    sink.write(event.c_str(), event.size());
                };

                try {
                    callback(send_event);
                } catch (const std::exception& e) {
                    MCP_LOG_ERROR("SSE callback error: {}", e.what());
                }

                return true;
            }
        );
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

    try {
        // 解析 JSON
        json request_json = json::parse(request_body);

        // 检查是否为批量请求
        if (request_json.is_array()) {
            json batch_response = json::array();

            for (const auto& single_req_json : request_json) {
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

                    // 处理请求
                    if (!req.id.has_value()) {
                        // 通知请求，无需响应
                        if (dispatcher_.hasHandler(req.method)) {
                            dispatcher_.call(req.method, req.params.value_or(json::object()));
                        }
                        continue;
                    }

                    // 构造响应
                    JsonRpcResponse resp;
                    resp.jsonrpc = "2.0";
                    resp.id = *req.id;

                    try {
                        if (!dispatcher_.hasHandler(req.method)) {
                            resp.error = JsonRpcError{
                                jsonrpc_errc::MethodNotFound,
                                "Method not found: " + req.method,
                                std::nullopt
                            };
                        } else {
                            resp.result = dispatcher_.call(req.method, req.params.value_or(json::object()));
                        }
                    } catch (const std::exception& e) {
                        resp.error = JsonRpcError{
                            jsonrpc_errc::InternalError,
                            e.what(),
                            std::nullopt
                        };
                    }

                    // 序列化响应
                    json resp_json = {{"jsonrpc", resp.jsonrpc}, {"id", resp.id}};
                    if (resp.result.has_value()) {
                        resp_json["result"] = *resp.result;
                    }
                    if (resp.error.has_value()) {
                        resp_json["error"] = {
                            {"code", resp.error->code},
                            {"message", resp.error->message}
                        };
                        if (resp.error->data.has_value()) {
                            resp_json["error"]["data"] = *resp.error->data;
                        }
                    }

                    batch_response.push_back(resp_json);

                } catch (const std::exception& e) {
                    MCP_LOG_ERROR("Error in batch request: {}", e.what());
                    json error_resp = {
                        {"jsonrpc", "2.0"},
                        {"error", {
                            {"code", jsonrpc_errc::InternalError},
                            {"message", e.what()}
                        }},
                        {"id", nullptr}
                    };
                    batch_response.push_back(error_resp);
                }
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

        // 如果是通知（无 id），执行后不返回响应
        if (!request.id.has_value()) {
            if (dispatcher_.hasHandler(request.method)) {
                dispatcher_.call(request.method, request.params.value_or(json::object()));
            }
            MCP_LOG_DEBUG("Notification request, no response");
            return "";
        }

        // 构造响应
        JsonRpcResponse response;
        response.jsonrpc = "2.0";
        response.id = *request.id;

        try {
            if (!dispatcher_.hasHandler(request.method)) {
                response.error = JsonRpcError{
                    jsonrpc_errc::MethodNotFound,
                    "Method not found: " + request.method,
                    std::nullopt
                };
            } else {
                response.result = dispatcher_.call(request.method, request.params.value_or(json::object()));
            }
        } catch (const std::exception& e) {
            response.error = JsonRpcError{
                jsonrpc_errc::InternalError,
                e.what(),
                std::nullopt
            };
        }

        // 序列化响应
        json response_json = {{"jsonrpc", response.jsonrpc}, {"id", response.id}};
        if (response.result.has_value()) {
            response_json["result"] = *response.result;
        }
        if (response.error.has_value()) {
            response_json["error"] = {
                {"code", response.error->code},
                {"message", response.error->message}
            };
            if (response.error->data.has_value()) {
                response_json["error"]["data"] = *response.error->data;
            }
        }

        std::string response_str = response_json.dump();
        // MCP_LOG_DEBUG("Response: {}", response_str);
        return response_str;

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
