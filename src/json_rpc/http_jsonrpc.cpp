/**
 * @file http_jsonrpc.cpp
 * @brief 基于 HTTP 的 JSON-RPC 2.0 服务器实现
 */

#include "http_jsonrpc.h"
#include "jsonrpc_serialization.h"
#include "logger.h"
#include "config.h"
#include "types.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "jsonrpc_task.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace mcp {
namespace {

constexpr char kMcpSessionIdHeader[] = "Mcp-Session-Id";
constexpr char kMcpProtocolVersionHeader[] = "MCP-Protocol-Version";
constexpr char kMcpMethodHeader[] = "Mcp-Method";
constexpr char kMcpNameHeader[] = "Mcp-Name";

struct ModernHttpFailure {
    int status = 400;
    json body;
};

json ErrorResponse(
    const json& id,
    int code,
    const std::string& message,
    std::optional<json> data = std::nullopt
) {
    json error = {{"code", code}, {"message", message}};
    if (data.has_value()) {
        error["data"] = std::move(*data);
    }
    return {
        {"jsonrpc", kJsonRpcVersion},
        {"id", id},
        {"error", std::move(error)}
    };
}

std::string Lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

bool ContainsMediaType(
    const std::string& header,
    const std::string& media_type
) {
    return Lowercase(header).find(media_type) != std::string::npos;
}

bool HasLocalOriginHost(
    const std::string& origin,
    const std::string& scheme_and_host
) {
    if (origin.compare(0, scheme_and_host.size(), scheme_and_host) != 0) {
        return false;
    }
    if (origin.size() == scheme_and_host.size()) {
        return true;
    }
    if (origin[scheme_and_host.size()] != ':') {
        return false;
    }
    const std::string_view port(
        origin.data() + scheme_and_host.size() + 1,
        origin.size() - scheme_and_host.size() - 1
    );
    if (port.empty() || port.size() > 5 ||
        !std::all_of(port.begin(), port.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return false;
    }
    const unsigned long port_number = std::stoul(std::string(port));
    return port_number <= 65535;
}

bool IsAllowedOrigin(const std::string& origin) {
    if (origin.empty()) {
        return true;
    }
    const std::string normalized = Lowercase(origin);
    return HasLocalOriginHost(normalized, "http://127.0.0.1") ||
        HasLocalOriginHost(normalized, "https://127.0.0.1") ||
        HasLocalOriginHost(normalized, "http://localhost") ||
        HasLocalOriginHost(normalized, "https://localhost") ||
        HasLocalOriginHost(normalized, "http://[::1]") ||
        HasLocalOriginHost(normalized, "https://[::1]");
}

std::optional<std::string> DecodeMcpHeaderValue(
    const std::string& value
) {
    constexpr char kPrefix[] = "=?base64?";
    constexpr char kSuffix[] = "?=";
    if (
        value.size() < sizeof(kPrefix) - 1 + sizeof(kSuffix) - 1 ||
        value.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 ||
        value.compare(
            value.size() - (sizeof(kSuffix) - 1),
            sizeof(kSuffix) - 1,
            kSuffix
        ) != 0
    ) {
        return value;
    }

    const std::string encoded = value.substr(
        sizeof(kPrefix) - 1,
        value.size() - (sizeof(kPrefix) - 1) - (sizeof(kSuffix) - 1)
    );
    if (encoded.empty() || encoded.size() % 4 != 0) {
        return std::nullopt;
    }

    std::string decoded((encoded.size() / 4) * 3, '\0');
    const int decoded_size = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(decoded.data()),
        reinterpret_cast<const unsigned char*>(encoded.data()),
        static_cast<int>(encoded.size())
    );
    if (decoded_size < 0) {
        return std::nullopt;
    }

    std::size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') {
        ++padding;
    }
    if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') {
        ++padding;
    }
    decoded.resize(static_cast<std::size_t>(decoded_size) - padding);
    return decoded;
}

std::optional<std::string> ExpectedMcpName(
    const JsonRpcRequest& request
) {
    if (!request.params.has_value() || !request.params->is_object()) {
        return std::nullopt;
    }
    const json& params = *request.params;
    if (request.method == "resources/read") {
        if (params.contains("uri") && params["uri"].is_string()) {
            return params["uri"].get<std::string>();
        }
        return std::nullopt;
    }
    if (request.method == "tools/call" || request.method == "prompts/get") {
        if (params.contains("name") && params["name"].is_string()) {
            return params["name"].get<std::string>();
        }
    }
    return std::nullopt;
}

bool RequiresMcpName(const std::string& method) {
    return method == "tools/call" ||
        method == "resources/read" ||
        method == "prompts/get";
}

bool ValidateModernHttpRequest(
    const httplib::Request& http_request,
    JsonRpcRequest& request,
    ModernHttpFailure& failure
) {
    if (!IsAllowedOrigin(http_request.get_header_value("Origin"))) {
        failure.status = 403;
        failure.body = ErrorResponse(
            nullptr,
            jsonrpc_errc::InvalidRequest,
            "Forbidden Origin"
        );
        return false;
    }

    if (!ContainsMediaType(
            http_request.get_header_value("Content-Type"),
            "application/json"
        )) {
        failure.status = 415;
        failure.body = ErrorResponse(
            nullptr,
            jsonrpc_errc::InvalidRequest,
            "Content-Type must be application/json"
        );
        return false;
    }

    const std::string accept = http_request.get_header_value("Accept");
    if (
        !ContainsMediaType(accept, "application/json") ||
        !ContainsMediaType(accept, "text/event-stream")
    ) {
        failure.status = 406;
        failure.body = ErrorResponse(
            nullptr,
            jsonrpc_errc::InvalidRequest,
            "Accept must include application/json and text/event-stream"
        );
        return false;
    }

    json body;
    try {
        body = json::parse(http_request.body);
    } catch (const json::parse_error& error) {
        failure.status = 400;
        failure.body = ErrorResponse(
            nullptr,
            jsonrpc_errc::ParseError,
            std::string("Parse error: ") + error.what()
        );
        return false;
    }

    if (!body.is_object()) {
        failure.status = 400;
        failure.body = ErrorResponse(
            nullptr,
            jsonrpc_errc::InvalidRequest,
            "Streamable HTTP accepts one JSON-RPC message per POST"
        );
        return false;
    }

    const json response_id = body.contains("id") ? body["id"] : json(nullptr);
    try {
        request = body.get<JsonRpcRequest>();
    } catch (const std::exception& error) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::InvalidRequest,
            error.what()
        );
        return false;
    }

    if (!request.params.has_value() || !request.params->is_object()) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::InvalidParams,
            "Modern MCP requests require object params"
        );
        return false;
    }

    const json& params = *request.params;
    const auto body_version = GetRequestProtocolVersion(params);
    const std::string header_version =
        http_request.get_header_value(kMcpProtocolVersionHeader);
    if (!body_version.has_value() || header_version.empty() ||
        header_version != *body_version) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::HeaderMismatch,
            "Header mismatch: MCP-Protocol-Version must match params._meta"
        );
        return false;
    }

    if (*body_version != kLatestProtocolVersion) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::UnsupportedProtocolVersion,
            "Unsupported protocol version",
            json{
                {"supported", SupportedProtocolVersionsJson()},
                {"requested", *body_version}
            }
        );
        return false;
    }

    const std::string header_method =
        http_request.get_header_value(kMcpMethodHeader);
    if (header_method.empty() || header_method != request.method) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::HeaderMismatch,
            "Header mismatch: Mcp-Method must match the JSON-RPC method"
        );
        return false;
    }

    if (RequiresMcpName(request.method)) {
        const auto expected_name = ExpectedMcpName(request);
        const auto header_name = DecodeMcpHeaderValue(
            http_request.get_header_value(kMcpNameHeader)
        );
        if (!expected_name.has_value() || !header_name.has_value() ||
            *header_name != *expected_name) {
            failure.status = 400;
            failure.body = ErrorResponse(
                response_id,
                jsonrpc_errc::HeaderMismatch,
                "Header mismatch: Mcp-Name must match request params"
            );
            return false;
        }
    }

    const json& metadata = params.at("_meta");
    constexpr char kCapabilitiesKey[] =
        "io.modelcontextprotocol/clientCapabilities";
    if (!metadata.contains(kCapabilitiesKey) ||
        !metadata[kCapabilitiesKey].is_object()) {
        failure.status = 400;
        failure.body = ErrorResponse(
            response_id,
            jsonrpc_errc::MissingRequiredClientCapability,
            "params._meta must include clientCapabilities"
        );
        return false;
    }

    return true;
}

int ModernHttpStatus(const JsonRpcResponse& response) {
    if (!response.error.has_value()) {
        return 200;
    }
    if (response.error->code == jsonrpc_errc::MethodNotFound) {
        return 404;
    }
    if (
        response.error->code == jsonrpc_errc::UnsupportedProtocolVersion ||
        response.error->code == jsonrpc_errc::HeaderMismatch ||
        response.error->code ==
            jsonrpc_errc::MissingRequiredClientCapability
    ) {
        return 400;
    }
    return 200;
}

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
            // 路由已经生成的协议错误（例如 MCP HeaderMismatch）必须原样保留。
            if (!res.body.empty()) {
                return;
            }
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
        "127.0.0.1",
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

    // MCP 2026-07-28 Streamable HTTP：无 Session、单一 POST 端点。
    impl_->server.Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        runtime_->record_http_request();

        const std::string origin = req.get_header_value("Origin");
        if (!origin.empty() && IsAllowedOrigin(origin)) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
        }

        JsonRpcRequest request;
        ModernHttpFailure failure;
        if (!ValidateModernHttpRequest(req, request, failure)) {
            res.status = failure.status;
            res.set_content(failure.body.dump(), "application/json");
            return;
        }

        const std::string client_id =
            "http:modern:" + GenerateSessionId();
        JsonRpcTask task = make_jsonrpc_task(
            request,
            client_id,
            std::chrono::milliseconds(MCP_CONFIG.GetRequestTimeoutMs())
        );
        TaskSubmission submission = runtime_->submit(task);

        // HTTP notification 被接收入队后立即确认；202 响应没有消息体。
        if (task.is_notification()) {
            if (submission.status == TaskSubmitStatus::Accepted) {
                res.status = 202;
                return;
            }

            JsonRpcTaskResult rejected = submission.result.get();
            res.status = 503;
            if (rejected.has_value()) {
                res.set_content(json(*rejected).dump(), "application/json");
            }
            return;
        }

        // tools/call 使用请求专属 SSE 响应。这里只发送最终响应；未来可在
        // 同一流中加入与该请求相关的 notifications/progress。
        if (request.method == "tools/call") {
            struct StreamState {
                JsonRpcTask task;
                std::future<JsonRpcTaskResult> result;
            };

            auto state = std::make_shared<StreamState>();
            state->task = task;
            state->result = std::move(submission.result);

            res.status = 200;
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "text/event-stream",
                [state](std::size_t /*offset*/, httplib::DataSink& sink) {
                    constexpr auto kPollInterval =
                        std::chrono::milliseconds(10);
                    JsonRpcTaskResult result;

                    while (
                        state->result.wait_for(kPollInterval) !=
                        std::future_status::ready
                    ) {
                        if (!sink.is_writable()) {
                            state->task.cancellation.cancel();
                            return false;
                        }

                        if (
                            state->task.deadline.has_value() &&
                            JsonRpcTask::Clock::now() >=
                                *state->task.deadline
                        ) {
                            state->task.cancellation.cancel();
                            JsonRpcResponse timeout;
                            timeout.id = state->task.request_id.value();
                            timeout.error = JsonRpcError{
                                -32001,
                                "Request timed out while streaming response",
                                std::nullopt
                            };
                            result = std::move(timeout);
                            break;
                        }
                    }

                    if (!result.has_value()) {
                        result = state->result.get();
                    }
                    if (!result.has_value()) {
                        sink.done();
                        return true;
                    }

                    const std::string payload =
                        "data: " + json(*result).dump() + "\n\n";
                    if (!sink.write(payload.data(), payload.size())) {
                        state->task.cancellation.cancel();
                        return false;
                    }
                    sink.done();
                    return true;
                },
                [state](bool success) {
                    if (!success) {
                        state->task.cancellation.cancel();
                    }
                }
            );
            return;
        }

        JsonRpcTaskResult result =
            runtime_->wait_for_result(task, submission.result);
        if (!result.has_value()) {
            res.status = 202;
            return;
        }

        res.status = ModernHttpStatus(*result);
        res.set_content(json(*result).dump(), "application/json");
    });

    impl_->server.Options("/mcp", [](const httplib::Request& req, httplib::Response& res) {
        const std::string origin = req.get_header_value("Origin");
        if (!IsAllowedOrigin(origin)) {
            res.status = 403;
            return;
        }
        if (!origin.empty()) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
        }
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Accept, MCP-Protocol-Version, Mcp-Method, Mcp-Name"
        );
        res.status = 204;
    });

    const auto reject_obsolete_mcp_method = [](
        const httplib::Request& req,
        httplib::Response& res
    ) {
        if (!IsAllowedOrigin(req.get_header_value("Origin"))) {
            res.status = 403;
            res.set_content(
                ErrorResponse(
                    nullptr,
                    jsonrpc_errc::InvalidRequest,
                    "Forbidden Origin"
                ).dump(),
                "application/json"
            );
            return;
        }
        res.status = 405;
        res.set_header("Allow", "POST, OPTIONS");
        res.set_content(
            ErrorResponse(
                nullptr,
                jsonrpc_errc::InvalidRequest,
                "MCP 2026 Streamable HTTP endpoint only accepts POST"
            ).dump(),
            "application/json"
        );
    };
    impl_->server.Get("/mcp", reject_obsolete_mcp_method);
    impl_->server.Delete("/mcp", reject_obsolete_mcp_method);

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
                {{"path", "/mcp"}, {"method", "POST"}, {"description", "MCP 2026 Streamable HTTP endpoint"}},
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
