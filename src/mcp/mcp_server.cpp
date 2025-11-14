/**
 * @file mcp_server.cpp
 * @brief MCP 服务器核心类实现
 */

#include "mcp_server.h"
#include <stdexcept>

namespace mcp {

// ===== 构造函数 =====

McpServer::McpServer(const std::string& name, const std::string& version) {
    server_info_.name = name;
    server_info_.version = version;

    // 设置默认能力
    capabilities_.tools = ServerCapabilities::ToolsCapability{false};
    capabilities_.resources = ServerCapabilities::ResourcesCapability{false, false};
    capabilities_.prompts = ServerCapabilities::PromptsCapability{false};
}

// ===== 初始化相关 =====

InitializeResult McpServer::get_initialize_result() const {
    InitializeResult result;
    result.protocol_version = LATEST_PROTOCOL_VERSION;
    result.capabilities = capabilities_;
    result.server_info = server_info_;
    return result;
}

void McpServer::set_capabilities(const ServerCapabilities& capabilities) {
    capabilities_ = capabilities;
}

// ===== Tools 管理 =====

void McpServer::register_tool(const Tool& tool, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(tools_mutex_);

    if (tools_.find(tool.name) != tools_.end()) {
        throw std::runtime_error("Tool already registered: " + tool.name);
    }

    tools_[tool.name] = tool;
    tool_handlers_[tool.name] = std::move(handler);
}

std::vector<Tool> McpServer::list_tools() const {
    std::lock_guard<std::mutex> lock(tools_mutex_);

    std::vector<Tool> result;
    result.reserve(tools_.size());

    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }

    return result;
}

ToolResult McpServer::call_tool(const std::string& name, const json& arguments) {
    std::lock_guard<std::mutex> lock(tools_mutex_);

    auto it = tool_handlers_.find(name);
    if (it == tool_handlers_.end()) {
        throw std::runtime_error("Tool not found: " + name);
    }

    try {
        return it->second(arguments);
    } catch (const std::exception& e) {
        // 将异常转换为错误结果
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
    std::lock_guard<std::mutex> lock(tools_mutex_);
    return tools_.find(name) != tools_.end();
}

// ===== Resources 管理 =====

void McpServer::register_resource(const Resource& resource, ResourceProvider provider) {
    std::lock_guard<std::mutex> lock(resources_mutex_);

    if (resources_.find(resource.uri) != resources_.end()) {
        throw std::runtime_error("Resource already registered: " + resource.uri);
    }

    resources_[resource.uri] = resource;
    resource_providers_[resource.uri] = std::move(provider);
}

std::vector<Resource> McpServer::list_resources() const {
    std::lock_guard<std::mutex> lock(resources_mutex_);

    std::vector<Resource> result;
    result.reserve(resources_.size());

    for (const auto& [uri, resource] : resources_) {
        result.push_back(resource);
    }

    return result;
}

ResourceContent McpServer::read_resource(const std::string& uri) {
    std::lock_guard<std::mutex> lock(resources_mutex_);

    auto it = resource_providers_.find(uri);
    if (it == resource_providers_.end()) {
        throw std::runtime_error("Resource not found: " + uri);
    }

    return it->second(uri);
}

bool McpServer::has_resource(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    return resources_.find(uri) != resources_.end();
}

// ===== Prompts 管理 =====

void McpServer::register_prompt(const Prompt& prompt, PromptGenerator generator) {
    std::lock_guard<std::mutex> lock(prompts_mutex_);

    if (prompts_.find(prompt.name) != prompts_.end()) {
        throw std::runtime_error("Prompt already registered: " + prompt.name);
    }

    prompts_[prompt.name] = prompt;
    prompt_generators_[prompt.name] = std::move(generator);
}

std::vector<Prompt> McpServer::list_prompts() const {
    std::lock_guard<std::mutex> lock(prompts_mutex_);

    std::vector<Prompt> result;
    result.reserve(prompts_.size());

    for (const auto& [name, prompt] : prompts_) {
        result.push_back(prompt);
    }

    return result;
}

std::vector<PromptMessage> McpServer::get_prompt(const std::string& name, const json& arguments) {
    std::lock_guard<std::mutex> lock(prompts_mutex_);

    auto it = prompt_generators_.find(name);
    if (it == prompt_generators_.end()) {
        throw std::runtime_error("Prompt not found: " + name);
    }

    return it->second(arguments);
}

bool McpServer::has_prompt(const std::string& name) const {
    std::lock_guard<std::mutex> lock(prompts_mutex_);
    return prompts_.find(name) != prompts_.end();
}

} // namespace mcp
