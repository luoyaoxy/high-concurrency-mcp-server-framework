

#include "config.h"
#include "logger.h"
#include "http_jsonrpc.h"
#include "http_sse_server.h"
#include "jsonrpc.h"
#include "jsonrpc_request_context.h"
#include "mcp_server.h"
#include "tool_circuit_breaker.h"
#include "tool_execution_error.h"
#include "weather_client.h"

#include "jsonrpc_task_runtime.h"

#include "sse_event_hub.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <memory>
#include <ctime>
#include <fstream>
#include <sstream>

#include <array>
#include <filesystem>
#include <functional>



using namespace mcp;

// 全局服务器实例指针（用于信号处理）
static std::atomic<bool> g_running{true};
static std::unique_ptr<HttpJsonRpcServer> g_http_server{nullptr};

// 独立 SSE 服务，用于承载长连接。
static std::unique_ptr<HttpSseServer> g_sse_server{nullptr};

namespace {

std::string format_local_time(std::time_t time_value) {
    std::tm local_time{};

    // localtime_r 将结果写入调用方自己的对象，可安全并发调用。
    if (localtime_r(&time_value, &local_time) == nullptr) {
        return "unknown";
    }

    char buffer[100];
    std::strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d %H:%M:%S",
        &local_time
    );

    return buffer;
}

constexpr std::size_t kFileWriteLockStripes = 64;
std::array<std::mutex, kFileWriteLockStripes> g_file_write_locks;

std::string normalize_file_path(const std::string& path) {
    return std::filesystem::absolute(path)
        .lexically_normal()
        .string();
}

std::mutex& file_write_mutex(const std::string& normalized_path) {
    // 同一路径总是映射到同一把锁。
    const std::size_t index =
        std::hash<std::string>{}(normalized_path) %
        g_file_write_locks.size();

    return g_file_write_locks[index];
}

bool current_request_should_stop() {
    const JsonRpcRequestContext* context =
        current_jsonrpc_request_context();
    return context != nullptr && context->should_stop();
}

} // namespace

// 信号处理函数
void signal_handler(int signal) {
    std::cerr << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    if (g_http_server) {
        g_http_server->stop();
    }

    // 信号到来时，同时停止独立 SSE 监听器。
    if (g_sse_server) {
        g_sse_server->stop();
    }
}

// 字符串转日志级别
spdlog::level::level_enum StringToLogLevel(const std::string& level_str) {
    if (level_str == "trace") {
        return spdlog::level::trace;
    } else if (level_str == "debug") {
        return spdlog::level::debug;
    } else if (level_str == "info") {
        return spdlog::level::info;
    } else if (level_str == "warn") {
        return spdlog::level::warn;
    } else if (level_str == "error") {
        return spdlog::level::err;
    } else if (level_str == "critical") {
        return spdlog::level::critical;
    }
    // Default to info if unknown
    return spdlog::level::info;
}

// 注册 MCP 工具、资源、提示词
void setup_mcp_server(McpServer& mcp) {
    // Echo 工具
    {
        Tool tool;
        tool.name = "echo";
        tool.description = "Echo back the input message";
        tool.input_schema.properties = {
            {"message", {{"type", "string"}, {"description", "Message to echo"}}}
        };
        tool.input_schema.required = {"message"};
        tool.execution_policy.idempotent = true;
        tool.execution_policy.max_retries = 2;

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = "Echo: " + args.at("message").get<std::string>()
            });
            return result;
        });
    }

    // Calculate 工具
    {
        Tool tool;
        tool.name = "calculate";
        tool.description = "Perform basic arithmetic operations";
        tool.input_schema.properties = {
            {"operation", {{"type", "string"}, {"enum", json::array({"add", "subtract", "multiply", "divide"})}}},
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        };
        tool.input_schema.required = {"operation", "a", "b"};
        tool.execution_policy.idempotent = true;
        tool.execution_policy.max_retries = 2;

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string op = args.at("operation").get<std::string>();
            double a = args.at("a").get<double>();
            double b = args.at("b").get<double>();
            double result_val = 0;

            if (op == "add") result_val = a + b;
            else if (op == "subtract") result_val = a - b;
            else if (op == "multiply") result_val = a * b;
            else if (op == "divide") {
                if (b == 0) {
                    ToolResult error;
                    error.is_error = true;
                    error.content.push_back(ContentItem{
                        .type = "text",
                        .text = "Error: Division by zero"
                    });
                    return error;
                }
                result_val = a / b;
            }

            ToolResult result;
            result.content.push_back(ContentItem{
                .type = "text",
                .text = std::to_string(result_val)
            });
            return result;
        });
    }

    // GetTime 工具
    {
        Tool tool;
        tool.name = "get_time";
        tool.description = "Get current system time";
        tool.input_schema.properties = json::object();
        tool.execution_policy.idempotent = true;
        tool.execution_policy.max_retries = 2;

        mcp.register_tool(tool, [](const json& /*args*/) -> ToolResult {

            ToolResult result;

            result.content.push_back(ContentItem{
                .type = "text",
                .text = format_local_time(std::time(nullptr))
            });
            return result;
        });
    }

    // GetWeather 工具
    {
        Tool tool;
        tool.name = "get_weather";
        tool.description = "Get weather information for a city";
        tool.input_schema.properties = {
            {"city", {{"type", "string"}, {"description", "City name (e.g., Beijing, Shanghai)"}}}
        };
        tool.input_schema.required = {"city"};
        // 查询没有副作用；临时网络故障可使用已有重试与熔断机制。
        tool.execution_policy.idempotent = true;
        // 一次查询包含地理编码和天气数据两个串行 HTTP 请求；总预算还需
        // 覆盖临时网络故障时的两次重试及退避时间。
        tool.execution_policy.timeout_ms = 20000;
        tool.execution_policy.max_retries = 2;

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string city = args.at("city").get<std::string>();
            ToolResult result;

            try {
                WeatherClient weather_client(
                    {},
                    [] { return current_request_should_stop(); }
                );
                const WeatherReport weather =
                    weather_client.get_current_weather(city);

                std::ostringstream weather_info;
                weather_info << "天气：" << weather.resolved_city;
                if (!weather.country.empty()) {
                    weather_info << "，" << weather.country;
                }
                weather_info << "\n观测时间：" << weather.observed_at
                             << "\n天气状况：" << weather.condition
                             << "\n温度：" << weather.temperature_c << " °C"
                             << "\n体感温度："
                             << weather.apparent_temperature_c << " °C"
                             << "\n湿度：" << weather.humidity_percent << "%"
                             << "\n风速：" << weather.wind_speed_kmh << " km/h";

                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = weather_info.str()
                });
            } catch (const WeatherRequestCancelled& e) {
                result.is_error = true;
                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = e.what()
                });
            } catch (const RetryableToolError&) {
                // 交给 McpServer::call_tool() 的重试与熔断逻辑处理。
                throw;
            } catch (const std::exception& e) {
                result.is_error = true;
                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = std::string("Failed to get weather: ") + e.what()
                });
            }

            return result;
        });
    }

    // WriteFile 工具
    {
        Tool tool;
        tool.name = "write_file";
        tool.description = "Write content to a file";
        tool.input_schema.properties = {
            {"path", {{"type", "string"}, {"description", "File path to write to"}}},
            {"content", {{"type", "string"}, {"description", "Content to write to the file"}}}
        };
        tool.input_schema.required = {"path", "content"};
        // 写文件会产生副作用，默认禁止自动重试。
        tool.execution_policy.idempotent = false;
        tool.execution_policy.max_retries = 0;

        mcp.register_tool(tool, [](const json& args) -> ToolResult {
            std::string path = args.at("path").get<std::string>();
            std::string content = args.at("content").get<std::string>();

            ToolResult result;

            const auto cancelled_result = [] {
                ToolResult cancelled;
                cancelled.is_error = true;
                cancelled.content.push_back(ContentItem{
                    .type = "text",
                    .text = "Request cancelled before file write"
                });
                return cancelled;
            };

            // 文件写入有副作用，取消后不再开始新的写操作。
            if (current_request_should_stop()) {
                return cancelled_result();
            }

                try {
                    // 同一路径的写操作串行化，不同路径通常可并发执行。
                    const std::string normalized_path =
                        normalize_file_path(path);

                    std::lock_guard<std::mutex> lock(
                        file_write_mutex(normalized_path)
                    );

                    // 等待路径锁期间可能收到取消，需要再次确认。
                    if (current_request_should_stop()) {
                        return cancelled_result();
                    }

                    std::ofstream file(path);

                if (!file.is_open()) {
                    result.is_error = true;
                    result.content.push_back(ContentItem{
                        .type = "text",
                        .text = "Error: Failed to open file: " + path
                    });
                    return result;
                    }

                if (current_request_should_stop()) {
                    file.close();
                    return cancelled_result();
                }

                file << content;
                file.close();

                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = "Successfully wrote to file: " + path
                });
            } catch (const std::exception& e) {
                result.is_error = true;
                result.content.push_back(ContentItem{
                    .type = "text",
                    .text = std::string("Error writing file: ") + e.what()
                });
            }

            return result;
        });
    }

    // ===== 注册资源 =====

    // 系统信息
    {
        Resource res;
        res.uri = "system://info";
        res.name = "System Information";
        res.description = "Basic system information";
        res.mime_type = "text/plain";

        mcp.register_resource(res, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "text/plain";

            std::ostringstream oss;
            oss << "MCP Server - System Info\n";
            oss << "========================\n";

            oss << "Time: "
                << format_local_time(std::time(nullptr))
                << "\n";

            content.text = oss.str();
            return content;
        });
    }

    // 服务器配置
    {
        Resource res;
        res.uri = "config://server";
        res.name = "Server Configuration";
        res.mime_type = "application/json";

        mcp.register_resource(res, [](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "application/json";
            content.text = json({
                {"port", MCP_CONFIG.GetServerPort()},
                {"log_level", MCP_CONFIG.GetLogLevel()}
            }).dump(2);
            return content;
        });
    }

    // ===== 注册提示词 =====

    // 代码审查
    {
        Prompt prompt;
        prompt.name = "code_review";
        prompt.description = "Generate code review prompt";
        prompt.arguments.push_back(PromptArgument{.name = "code", .required = true});
        prompt.arguments.push_back(PromptArgument{.name = "language", .required = true});

        mcp.register_prompt(prompt, [](const json& args) -> std::vector<PromptMessage> {
            std::vector<PromptMessage> msgs;
            PromptMessage msg;
            msg.role = Role::User;
            msg.content = {
                {"type", "text"},
                {"text", "Please review this " + args.at("language").get<std::string>() +
                         " code:\n\n" + args.at("code").get<std::string>()}
            };
            msgs.push_back(msg);
            return msgs;
        });
    }

    MCP_LOG_INFO("MCP setup complete: {} tools, {} resources, {} prompts",
                 mcp.list_tools().size(), mcp.list_resources().size(), mcp.list_prompts().size());
}

// 创建 JSON-RPC 调度器（绑定到 MCP 服务器）
JsonRpcDispatcher create_dispatcher(McpServer& mcp_server) {
    JsonRpcDispatcher dispatcher;

    const auto protocol_result = [&mcp_server](
        const json& params,
        json result,
        bool cacheable = false,
        int ttl_ms = 0
    ) {
        if (!IsModernProtocolRequest(params)) {
            return result;
        }
        return mcp_server.decorate_modern_result(
            std::move(result),
            cacheable,
            ttl_ms,
            "private"
        );
    };

    auto tool_circuit_breaker = std::make_shared<ToolCircuitBreaker>(
        MCP_CONFIG.GetToolCircuitFailureThreshold(),
        std::chrono::milliseconds(MCP_CONFIG.GetToolCircuitOpenMs())
    );

    // 2026-07-28 起使用无状态能力发现，不再依赖 initialize 握手。
    dispatcher.registerHandler(
        "server/discover",
        [&mcp_server](const json& /*params*/) -> json {
            return mcp_server.get_discover_result();
        }
    );

    // initialize
    dispatcher.registerHandler("initialize", [&mcp_server](const json& params) -> json {
        if (!params.is_object()) {
            throw std::invalid_argument("initialize params must be an object");
        }
        if (!params.contains("protocolVersion") ||
            !params["protocolVersion"].is_string()) {
            throw std::invalid_argument(
                "initialize requires a string protocolVersion"
            );
        }

        const std::string requested_version =
            params["protocolVersion"].get<std::string>();
        MCP_LOG_INFO("Client initialized");
        return mcp_server.get_initialize_result(requested_version).to_json();
    });

    // 客户端完成初始化后的生命周期 notification。
    dispatcher.registerHandler(
        "notifications/initialized",
        [](const json& /*params*/) -> json { return json::object(); }
    );

    dispatcher.registerHandler(
        "ping",
        [](const json& /*params*/) -> json { return json::object(); }
    );

    // tools/list
    dispatcher.registerHandler("tools/list", [&mcp_server, protocol_result](const json& params) -> json {
        json tools_arr = json::array();
        for (const auto& tool : mcp_server.list_tools()) {
            tools_arr.push_back(tool.to_json());
        }
        return protocol_result(
            params,
            json{{"tools", tools_arr}},
            true,
            30000
        );
    });

    // tools/call
    dispatcher.registerHandler(
        "tools/call",
        [&mcp_server, tool_circuit_breaker, protocol_result](const json& params) -> json {
        if (!params.is_object()) {
            throw std::invalid_argument(
                "tools/call params must be an object"
            );
        }
        if (!params.contains("name") || !params["name"].is_string()) {
            throw std::invalid_argument(
                "tools/call requires a string name"
            );
        }
        if (params.contains("arguments") && !params["arguments"].is_object()) {
            throw std::invalid_argument(
                "tools/call arguments must be an object"
            );
        }

        const std::string name = params["name"].get<std::string>();
        const json arguments = params.value("arguments", json::object());

        if (!tool_circuit_breaker->allow_call(name)) {
            MCP_LOG_WARN("Tool circuit is open: {}", name);

            ToolResult unavailable;
            unavailable.is_error = true;
            unavailable.content.push_back(ContentItem{
                .type = "text",
                .text = "Tool temporarily unavailable"
            });
            return protocol_result(params, unavailable.to_json());
        }

        MCP_LOG_INFO("Calling tool: {}", name);
        // call_tool() 已在内部完成全部重试；熔断器只记录这次逻辑调用的最终结果。
        auto result = mcp_server.call_tool(name, arguments);

        const JsonRpcRequestContext* context =
            current_jsonrpc_request_context();

        // 客户端主动取消不表示工具依赖故障，不计入熔断。
        if (context != nullptr && context->is_cancelled()) {
            return protocol_result(params, result.to_json());
        }

        if (
            result.is_error ||
            (context != nullptr && context->deadline_exceeded())
        ) {
            tool_circuit_breaker->record_failure(name);
        } else {
            tool_circuit_breaker->record_success(name);
        }

        return protocol_result(params, result.to_json());
        }
    );

    // resources/list
    dispatcher.registerHandler("resources/list", [&mcp_server, protocol_result](const json& params) -> json {
        json resources_arr = json::array();
        for (const auto& resource : mcp_server.list_resources()) {
            resources_arr.push_back(resource.to_json());
        }
        return protocol_result(
            params,
            json{{"resources", resources_arr}},
            true,
            30000
        );
    });

    // resources/read
    dispatcher.registerHandler("resources/read", [&mcp_server, protocol_result](const json& params) -> json {
        std::string uri = params.at("uri").get<std::string>();
        if (!mcp_server.has_resource(uri)) {
            throw std::invalid_argument("Resource not found: " + uri);
        }
        MCP_LOG_INFO("Reading resource: {}", uri);
        auto content = mcp_server.read_resource(uri);
        json contents_arr = json::array();
        contents_arr.push_back(content.to_json());
        return protocol_result(
            params,
            json{{"contents", contents_arr}},
            true,
            1000
        );
    });

    // 当前项目未注册参数化资源，仍按协议返回可缓存的空模板列表。
    dispatcher.registerHandler(
        "resources/templates/list",
        [&protocol_result](const json& params) -> json {
            return protocol_result(
                params,
                json{{"resourceTemplates", json::array()}},
                true,
                30000
            );
        }
    );

    // prompts/list
    dispatcher.registerHandler("prompts/list", [&mcp_server, protocol_result](const json& params) -> json {
        json prompts_arr = json::array();
        for (const auto& prompt : mcp_server.list_prompts()) {
            prompts_arr.push_back(prompt.to_json());
        }
        return protocol_result(
            params,
            json{{"prompts", prompts_arr}},
            true,
            30000
        );
    });

    // prompts/get
    dispatcher.registerHandler("prompts/get", [&mcp_server, protocol_result](const json& params) -> json {
        std::string name = params.at("name").get<std::string>();
        json arguments = params.value("arguments", json::object());
        MCP_LOG_INFO("Getting prompt: {}", name);
        auto messages = mcp_server.get_prompt(name, arguments);
        json messages_arr = json::array();
        for (const auto& msg : messages) {
            messages_arr.push_back(msg.to_json());
        }
        return protocol_result(params, json{{"messages", messages_arr}});
    });

    return dispatcher;
}

std::shared_ptr<JsonRpcTaskRuntime> create_task_runtime(
    McpServer& mcp_server
) {
    // HTTP 与 stdio 共用同一运行时；运行时再按任务 lane 路由到独立队列与 worker。
    return std::make_shared<JsonRpcTaskRuntime>(
        create_dispatcher(mcp_server),

        // 默认请求执行池配置。
        MCP_CONFIG.GetMaxPendingTasks(),
        MCP_CONFIG.GetWorkerThreads(),

        // tools/call 专用执行池配置。
        MCP_CONFIG.GetToolMaxPendingTasks(),
        MCP_CONFIG.GetToolWorkers(),

        // resources/read 专用执行池配置。
        MCP_CONFIG.GetResourceMaxPendingTasks(),
        MCP_CONFIG.GetResourceWorkers(),

        // prompts/get 专用执行池配置。
        MCP_CONFIG.GetPromptMaxPendingTasks(),
        MCP_CONFIG.GetPromptWorkers()
    );
}

// 将运行时调度状态作为只读 MCP resource 暴露给客户端。
void register_metrics_resource(
    McpServer& mcp_server,
    const std::shared_ptr<JsonRpcTaskRuntime>& runtime
) {
    Resource resource;
    resource.uri = "mcp://server/metrics";
    resource.name = "Server Metrics";
    resource.description = "Current JSON-RPC scheduler metrics";
    resource.mime_type = "application/json";

    mcp_server.register_resource(
        resource,
        [runtime](const std::string& uri) -> ResourceContent {
            ResourceContent content;
            content.uri = uri;
            content.mime_type = "application/json";
            content.text = runtime->metrics_snapshot().to_json().dump(2);

            MCP_LOG_DEBUG("Scheduler metrics resource read");
            return content;
        }
    );
}

// HTTP 模式
void run_http_mode(
    McpServer& mcp_server,
    const std::string& host,
    int port,
    const std::shared_ptr<JsonRpcTaskRuntime>& runtime
) {
    MCP_LOG_INFO("Starting HTTP server on {}:{}", host, port);

    g_http_server = std::make_unique<HttpJsonRpcServer>(
        runtime,
        host,
        port,
        [&mcp_server](const std::string& tool_name) {
            return mcp_server.find_tool_input_schema(tool_name);
        }
    );

    // SSE 使用独立端口和专用 HTTP worker。
    g_sse_server = std::make_unique<HttpSseServer>(
        host,
        MCP_CONFIG.GetSsePort(),
        MCP_CONFIG.GetSseWorkers()
    );

    // HTTP 模式运行期间共享的 SSE 订阅中心。
    auto sse_event_hub = std::make_shared<SseEventHub>(
        MCP_CONFIG.GetSseMaxClients(),
        MCP_CONFIG.GetSseMaxPendingEventsPerClient(),
        MCP_CONFIG.GetSseReplayBufferEvents()
    );

    // 状态流与工具调用流使用不同 Hub，避免相互抢占连接名额。
    auto status_sse_hub = std::make_shared<SseEventHub>(
        MCP_CONFIG.GetSseMaxClients(),
        MCP_CONFIG.GetSseMaxPendingEventsPerClient(),
        MCP_CONFIG.GetSseReplayBufferEvents()
    );

    // 工具调用事件写入 SSE Hub，由 Hub 广播给各个独立客户端队列。
    mcp_server.set_sse_callback([sse_event_hub](const json& event) {
        sse_event_hub->publish(event.dump());
    });

    // 状态事件由单一发布者产生，所有 SSE 客户端共享同一事件序列。
    std::atomic_bool status_publisher_running{true};
    std::thread status_publisher([
        status_sse_hub,
        &mcp_server,
        &status_publisher_running
    ] {
        int count = 0;

        while (status_publisher_running.load()) {
            json status = {
                {"type", "server_status"},
                {"timestamp", std::time(nullptr)},
                {"tools_count", mcp_server.list_tools().size()},
                {"resources_count", mcp_server.list_resources().size()},
                {"prompts_count", mcp_server.list_prompts().size()},
                {"uptime_seconds", count++}
            };
            status_sse_hub->publish(status.dump());

            // 短间隔等待，停止服务时无需等待完整五秒周期。
            for (
                int interval = 0;
                interval < 50 && status_publisher_running.load();
                ++interval
            ) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    // 注册 SSE 端点 - 服务器状态流。
    g_sse_server->register_sse_endpoint(
        "/sse/events",
        [status_sse_hub](
            std::optional<SseEvent::EventId> last_event_id,
            const auto& send
        ) {
            // 状态流也必须先取得独立的 SSE 连接名额。
            const auto subscription = status_sse_hub->subscribe(last_event_id);
            if (!subscription.has_value()) {
                MCP_LOG_WARN("SSE events client rejected: limit reached");
                send(std::nullopt, json({
                    {"type", "error"},
                    {"message", "SSE client limit reached"}
                }).dump());
                return;
            }

            // 连接退出时自动释放状态流的订阅名额。
            struct SubscriptionGuard {
                std::shared_ptr<SseEventHub> hub;
                SseEventHub::SubscriptionId subscription_id;

                ~SubscriptionGuard() {
                    hub->unsubscribe(subscription_id);
                }
            } guard{status_sse_hub, *subscription};

            MCP_LOG_INFO(
                "SSE events client connected: subscription_id={}",
                *subscription
            );

            if (!send(std::nullopt, json({
                {"type", "connected"},
                {"message", "Server events stream"}
            }).dump())) {
                return;
            }

            while (g_running.load()) {
                const auto event = status_sse_hub->wait_and_pop(
                    *subscription,
                    std::chrono::seconds(1)
                );

                if (event.has_value()) {
                    if (!send(event->id, event->payload)) {
                        break;
                    }
                }
            }

            MCP_LOG_INFO(
                "SSE events client disconnected: subscription_id={}",
                *subscription
            );
        }
    );

    // 注册 SSE 端点 - 工具调用实时流。
    g_sse_server->register_sse_endpoint(
        "/sse/tool_calls",
        [sse_event_hub](
            std::optional<SseEvent::EventId> last_event_id,
            const auto& send
        ) {
            // 超过连接上限时，不再创建新的长期 SSE 订阅。
            const auto subscription = sse_event_hub->subscribe(last_event_id);
            if (!subscription.has_value()) {
                MCP_LOG_WARN("SSE tool_calls client rejected: limit reached");
                send(std::nullopt, json({
                    {"type", "error"},
                    {"message", "SSE client limit reached"}
                }).dump());
                return;
            }

            // 无论正常退出还是异常退出，都会取消订阅并释放客户端队列。
            struct SubscriptionGuard {
                std::shared_ptr<SseEventHub> hub;
                SseEventHub::SubscriptionId subscription_id;

                ~SubscriptionGuard() {
                    hub->unsubscribe(subscription_id);
                }
            } guard{sse_event_hub, *subscription};

            MCP_LOG_INFO(
                "SSE tool_calls client connected: subscription_id={}",
                *subscription
            );

            if (!send(std::nullopt, json({
                {"type", "connected"},
                {"message", "Tool calls monitoring"}
            }).dump())) {
                return;
            }

            while (g_running.load()) {
                // 每次只消费当前客户端自己的一条事件。
                const auto event = sse_event_hub->wait_and_pop(
                    *subscription,
                    std::chrono::seconds(1)
                );

                // 超时期间继续检查服务是否正在关闭。
                if (!event.has_value()) {
                    continue;
                }

                if (!send(event->id, event->payload)) {
                    break;
                }
            }

            MCP_LOG_INFO(
                "SSE tool_calls client disconnected: subscription_id={}",
                *subscription
            );
        }
    );
    // SSE 服务在独立线程中监听 sse_port，并使用自己的 HTTP worker pool。
    std::thread sse_thread([] {
        g_sse_server->run();
    });

    // 无论主 HTTP 服务正常退出还是抛异常，都要停止并回收 SSE 线程。
    const auto stop_and_join_sse = [
        &sse_thread,
        &status_publisher,
        &status_publisher_running
    ] {
        status_publisher_running.store(false);
        if (status_publisher.joinable()) {
            status_publisher.join();
        }

        if (g_sse_server) {
            g_sse_server->stop();
        }

        if (sse_thread.joinable()) {
            sse_thread.join();
        }
    };

    try {
        // 当前线程继续运行 JSON-RPC HTTP 服务。
        g_http_server->run();
    } catch (...) {
        // 监听端口失败等异常不能遗留 joinable SSE 线程。
        stop_and_join_sse();
        throw;
    }

    // JSON-RPC 服务正常停止时，也执行同一套收尾逻辑。
    stop_and_join_sse();

    MCP_LOG_INFO("HTTP and SSE servers stopped");
}

// stdio 模式
void run_stdio_mode(
    McpServer& mcp_server,
    const std::shared_ptr<JsonRpcTaskRuntime>& runtime
) {
    MCP_LOG_INFO("Starting stdio server");

    StdioJsonRpcServer stdio_server(runtime);

    stdio_server.run();

    MCP_LOG_INFO("stdio server stopped");
}

// 同时运行两种模式
void run_both_modes(
    McpServer& mcp_server,
    const std::string& host,
    int port,
    const std::shared_ptr<JsonRpcTaskRuntime>& runtime
) {
    MCP_LOG_INFO("Starting both HTTP and stdio servers");

    std::thread http_thread([&mcp_server, &host, port, runtime]() {
        run_http_mode(mcp_server, host, port, runtime);
    });

    run_stdio_mode(mcp_server, runtime);

    if (http_thread.joinable()) {
        http_thread.join();
    }
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file = "../../config/server.json";
    std::string mode = "http";  // 默认 HTTP 模式
    std::string host;
    int port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --mode MODE      Server mode: http, stdio, or both (default: http)\n"
                      << "  --config FILE    Configuration file path\n"
                      << "  --host HOST      Server host (for HTTP mode)\n"
                      << "  --port PORT      Server port (for HTTP mode)\n"
                      << "  --help, -h       Show this help\n"
                      << "\nExamples:\n"
                      << "  " << argv[0] << " --mode http --port 8080\n"
                      << "  " << argv[0] << " --mode stdio\n"
                      << "  " << argv[0] << " --mode both --port 8080\n";
            return 0;
        }
    }

    // 验证模式
    if (mode != "http" && mode != "stdio" && mode != "both") {
        std::cerr << "Invalid mode: " << mode << " (must be http, stdio, or both)" << std::endl;
        return 1;
    }

    // 加载配置
    if (!MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
    }

    if (host.empty()) {
        // MCP Streamable HTTP 的本地默认值仅监听回环地址，降低 DNS
        // rebinding 和意外暴露到局域网的风险；远程部署可显式传 --host。
        host = "127.0.0.1";
    }
    if (port == 0) {
        port = MCP_CONFIG.GetServerPort();
    }

    // 初始化日志
    MCP_LOG_INIT("mcp_server", MCP_CONFIG.GetLogFilePath(),
                 MCP_CONFIG.GetLogFileSize(), MCP_CONFIG.GetLogFileCount(),
                 MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(MCP_CONFIG.GetLogLevel()));


    if (mode == "http" || mode == "both") {
        MCP_LOG_INFO("HTTP: {}:{}", host, port);
    }

    try {
        // 创建 MCP 服务器实例
        McpServer mcp_server("mcp-server", "1.0.0");

        // 设置能力
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false}; // 工具能力
        capabilities.resources = ServerCapabilities::ResourcesCapability{false, false}; // 资源能力
        capabilities.prompts = ServerCapabilities::PromptsCapability{false}; // 提示能力
        mcp_server.set_capabilities(capabilities);

        // 注册 Tools, Resources, Prompts
        setup_mcp_server(mcp_server);

        auto runtime = create_task_runtime(mcp_server);
        register_metrics_resource(mcp_server, runtime);

        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 根据模式启动服务器
        if (mode == "http") {
            run_http_mode(mcp_server, host, port, runtime);
        } else if (mode == "stdio") {
            run_stdio_mode(mcp_server, runtime);
        } else {
            run_both_modes(mcp_server, host, port, runtime);
        }

        runtime->shutdown();

        MCP_LOG_INFO("Server shutdown complete");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}
