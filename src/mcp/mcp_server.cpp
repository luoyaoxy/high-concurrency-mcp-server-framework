// MCP 服务器核心类实现

#include "mcp_server.h"
#include "jsonrpc_request_context.h"
#include "tool_execution_error.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include "logger.h"

namespace mcp {
namespace {

bool current_request_should_stop() {
    const JsonRpcRequestContext* context =
        current_jsonrpc_request_context();
    return context != nullptr && context->should_stop();
}

std::string current_trace_id() {
    const JsonRpcRequestContext* context =
        current_jsonrpc_request_context();
    return context != nullptr ? context->trace_id() : "none";
}

ToolResult cancelled_tool_result() {
    ToolResult result;
    result.is_error = true;
    result.content.push_back(ContentItem{
        .type = "text",
        .text = "Request cancelled"
    });
    return result;
}

std::chrono::milliseconds retry_backoff(int retry_number) {
    constexpr std::chrono::milliseconds base_delay{50};
    constexpr std::chrono::milliseconds max_delay{1000};
    const int shift = std::min(retry_number - 1, 4);
    return std::min(base_delay * (1 << shift), max_delay);
}

bool wait_for_retry_backoff(std::chrono::milliseconds delay) {
    constexpr std::chrono::milliseconds check_interval{10};
    const auto deadline = std::chrono::steady_clock::now() + delay;

    while (std::chrono::steady_clock::now() < deadline) {
        if (current_request_should_stop()) {
            return false;
        }

        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds
        >(deadline - std::chrono::steady_clock::now());
        std::this_thread::sleep_for(std::min(check_interval, remaining));
    }

    return !current_request_should_stop();
}

} // namespace

// 构造函数
McpServer::McpServer(const std::string& name, const std::string& version) {
    server_info_.name = name;
    server_info_.version = version;

    // 设置默认能力
    capabilities_.tools = ServerCapabilities::ToolsCapability{false};
    capabilities_.resources = ServerCapabilities::ResourcesCapability{false, false};
    capabilities_.prompts = ServerCapabilities::PromptsCapability{false};
}

InitializeResult McpServer::get_initialize_result(
    std::string_view requested_protocol_version
) const {
    InitializeResult result;
    result.protocol_version = NegotiateProtocolVersion(
        requested_protocol_version
    );
    result.capabilities = capabilities_;
    result.server_info = server_info_;
    return result;
}

void McpServer::set_capabilities(const ServerCapabilities& capabilities) {
    capabilities_ = capabilities;
}

void McpServer::register_tool(const Tool& tool, ToolHandler handler) {
    std::unique_lock<std::shared_mutex> lock(tools_mutex_);

    if (tools_.find(tool.name) != tools_.end()) {
        throw std::runtime_error("Tool already registered: " + tool.name);
    }
    // MCP_LOG_INFO("Use tool name {}", tool.name);

    tools_[tool.name] = tool;
    tool_handlers_[tool.name] = std::move(handler);
}

std::vector<Tool> McpServer::list_tools() const {
    std::shared_lock<std::shared_mutex> lock(tools_mutex_);

    std::vector<Tool> result;
    result.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }

    return result;
}

ToolResult McpServer::call_tool(
    const std::string& name,
    const json& arguments
) {
    ToolHandler handler;
    Tool tool;

    {
        // 锁只保护注册表查询，不包住实际工具执行。
        std::shared_lock<std::shared_mutex> lock(tools_mutex_);

        auto it = tool_handlers_.find(name);
        if (it == tool_handlers_.end()) {
            throw std::invalid_argument("Tool not found: " + name);
        }

        handler = it->second;
        tool = tools_.at(name);
    }

    MCP_LOG_INFO("Use Tool: {}", name);

    // 在进入工具前检查，避免已取消请求产生新的副作用。
    if (current_request_should_stop()) {
        return cancelled_tool_result();
    }

    // tool timeout 是整个调用及其重试共享的总时间预算。
    std::unique_ptr<JsonRpcRequestContext> tool_context;
    std::unique_ptr<JsonRpcRequestContextScope> tool_context_scope;
    const JsonRpcRequestContext* request_context =
        current_jsonrpc_request_context();

    if (
        request_context != nullptr &&
        tool.execution_policy.timeout_ms > 0
    ) {
        auto effective_deadline = JsonRpcRequestContext::Clock::now() +
            std::chrono::milliseconds(tool.execution_policy.timeout_ms);

        if (
            request_context->deadline().has_value() &&
            *request_context->deadline() < effective_deadline
        ) {
            effective_deadline = *request_context->deadline();
        }

        tool_context = std::make_unique<JsonRpcRequestContext>(
            request_context->trace_id(),
            effective_deadline,
            request_context->cancellation_token()
        );
        tool_context_scope = std::make_unique<JsonRpcRequestContextScope>(
            *tool_context
        );
    }

    SseEventCallback callback;
    {
        // 复制回调后立即释放锁，避免在锁内调用外部代码。
        std::lock_guard<std::mutex> lock(sse_mutex_);
        callback = sse_callback_;
    }

    if (callback) {
        callback(json({
            {"type", "tool_call_start"},
            {"tool", name},
            {"arguments", arguments},
            {"timestamp", std::time(nullptr)}
        }));
    }

    try {
        const int max_retries =
            tool.execution_policy.idempotent
                ? std::max(0, tool.execution_policy.max_retries)
                : 0;
        const int total_attempts = max_retries + 1;

        for (int attempt = 0; attempt <= max_retries; ++attempt) {
            // 不在已取消或超时后启动新的工具尝试。
            if (current_request_should_stop()) {
                return cancelled_tool_result();
            }

            try {
                MCP_LOG_INFO(
                    "[trace_id={}] tool={} attempt={}/{}",
                    current_trace_id(),
                    name,
                    attempt + 1,
                    total_attempts
                );

                // handler 在 worker 线程中执行，多个无状态工具可真正并发。
                ToolResult result = handler(arguments);

                // 工具主动结束后，再次确认请求没有在执行期间被取消。
                if (current_request_should_stop()) {
                    result = cancelled_tool_result();
                }

                if (result.is_error) {
                    MCP_LOG_WARN(
                        "[trace_id={}] tool={} returned business error on attempt={}/{}",
                        current_trace_id(),
                        name,
                        attempt + 1,
                        total_attempts
                    );
                } else {
                    MCP_LOG_INFO(
                        "[trace_id={}] tool={} succeeded on attempt={}/{}",
                        current_trace_id(),
                        name,
                        attempt + 1,
                        total_attempts
                    );
                }

                if (callback) {
                    callback(json({
                        {"type", "tool_call_end"},
                        {"tool", name},
                        {"success", !result.is_error},
                        {"timestamp", std::time(nullptr)}
                    }));
                }

                // ToolResult 的业务错误默认不重试。
                return result;

            } catch (const RetryableToolError& e) {
                const bool can_retry =
                    attempt < max_retries && !current_request_should_stop();

                if (can_retry) {
                    const auto delay = retry_backoff(attempt + 1);
                    MCP_LOG_WARN(
                        "[trace_id={}] tool={} retrying after retryable error: "
                        "attempt={}/{}, delay_ms={}, error={}",
                        current_trace_id(),
                        name,
                        attempt + 1,
                        total_attempts,
                        delay.count(),
                        e.what()
                    );
                    if (!wait_for_retry_backoff(delay)) {
                        return cancelled_tool_result();
                    }
                    continue;
                }

                MCP_LOG_ERROR(
                    "[trace_id={}] tool={} failed after attempt={}/{}: {}",
                    current_trace_id(),
                    name,
                    attempt + 1,
                    total_attempts,
                    e.what()
                );

                if (callback) {
                    callback(json({
                        {"type", "tool_call_error"},
                        {"tool", name},
                        {"error", e.what()},
                        {"timestamp", std::time(nullptr)}
                    }));
                }

                ToolResult error_result;
                error_result.is_error = true;
                error_result.content.push_back(ContentItem{
                    .type = "text",
                    .text = std::string("Error calling tool: ") + e.what()
                });
                return error_result;
            }
        }

        throw std::logic_error("Tool retry loop exited unexpectedly");

    } catch (const std::exception& e) {
        MCP_LOG_ERROR(
            "[trace_id={}] tool={} failed with non-retryable error: {}",
            current_trace_id(),
            name,
            e.what()
        );
        if (callback) {
            callback(json({
                {"type", "tool_call_error"},
                {"tool", name},
                {"error", e.what()},
                {"timestamp", std::time(nullptr)}
            }));
        }

        ToolResult error_result;
        error_result.is_error = true;
        error_result.content.push_back(ContentItem{
            .type = "text",
            .text = std::string("Error calling tool: ") + e.what()
        });
        return error_result;
    }
}

bool McpServer::has_tool(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(tools_mutex_);
    return tools_.find(name) != tools_.end();
}

void McpServer::register_resource(const Resource& resource, ResourceProvider provider) {
    std::unique_lock<std::shared_mutex> lock(resources_mutex_);

    if (resources_.find(resource.uri) != resources_.end()) {
        throw std::runtime_error("Resource already registered: " + resource.uri);
    }
    // MCP_LOG_INFO("Use resources_ {}", resource.uri);

    resources_[resource.uri] = resource;
    resource_providers_[resource.uri] = std::move(provider);
}

std::vector<Resource> McpServer::list_resources() const {
    std::shared_lock<std::shared_mutex> lock(resources_mutex_);

    std::vector<Resource> result;
    result.reserve(resources_.size());

    for (const auto& [uri, resource] : resources_) {
        result.push_back(resource);
    }

    return result;
}

ResourceContent McpServer::read_resource(
    const std::string& uri
) {
    ResourceProvider provider;

    {
        // 锁只保护 provider 查询。
        std::shared_lock<std::shared_mutex> lock(resources_mutex_);

        auto it = resource_providers_.find(uri);
        if (it == resource_providers_.end()) {
            throw std::runtime_error("Resource not found: " + uri);
        }

        provider = it->second;
    }

    MCP_LOG_INFO("Use Resource: {}", uri);

    // 资源 provider 在锁外执行，多个只读资源可并发读取。
    return provider(uri);
}

bool McpServer::has_resource(const std::string& uri) const {
    std::shared_lock<std::shared_mutex> lock(resources_mutex_);
    return resources_.find(uri) != resources_.end();
}

void McpServer::register_prompt(const Prompt& prompt, PromptGenerator generator) {
    std::unique_lock<std::shared_mutex> lock(prompts_mutex_);

    if (prompts_.find(prompt.name) != prompts_.end()) {
        throw std::runtime_error("Prompt already registered: " + prompt.name);
    }

    prompts_[prompt.name] = prompt;
    prompt_generators_[prompt.name] = std::move(generator);
}

std::vector<Prompt> McpServer::list_prompts() const {
    std::shared_lock<std::shared_mutex> lock(prompts_mutex_);

    std::vector<Prompt> result;
    result.reserve(prompts_.size());

    for (const auto& [name, prompt] : prompts_) {
        result.push_back(prompt);
    }

    return result;
}

std::vector<PromptMessage> McpServer::get_prompt(
    const std::string& name,
    const json& arguments
) {
    PromptGenerator generator;

    {
        // 锁只保护 generator 查询。
        std::shared_lock<std::shared_mutex> lock(prompts_mutex_);

        auto it = prompt_generators_.find(name);
        if (it == prompt_generators_.end()) {
            throw std::runtime_error("Prompt not found: " + name);
        }

        generator = it->second;
    }

    MCP_LOG_INFO("Use Prompt: {}", name);

    // 无状态 prompt generator 可由多个 worker 并发执行。
    return generator(arguments);
}

bool McpServer::has_prompt(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(prompts_mutex_);
    return prompts_.find(name) != prompts_.end();
}

void McpServer::set_sse_callback(SseEventCallback callback) {
    std::lock_guard<std::mutex> lock(sse_mutex_);
    sse_callback_ = std::move(callback);
}

} // namespace mcp
