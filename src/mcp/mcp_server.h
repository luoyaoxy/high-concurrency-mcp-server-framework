/**
 * @file mcp_server.h
 * @brief MCP 服务器核心类
 *
 * 管理 Tools、Resources、Prompts 的注册和调用
 */

#pragma once

#include "types.h"
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace mcp {


// MCP 服务器核心类
// 提供 Tools、Resources、Prompts 的管理功能
class McpServer {
public:
    // ===== Tool 相关类型 =====

    // 工具处理器函数类型    
    using ToolHandler = std::function<ToolResult(const json& arguments)>;

    // 资源内容提供器函数类型
    using ResourceProvider = std::function<ResourceContent(const std::string& uri)>;

    // 提示词生成器函数类型
    using PromptGenerator = std::function<std::vector<PromptMessage>(const json& arguments)>;

    // 构造函数
    McpServer(const std::string& name, const std::string& version);

    // 获取初始化结果
    InitializeResult get_initialize_result(
        std::string_view requested_protocol_version =
            kLatestProtocolVersion
    ) const;

    // 设置服务器能力
    void set_capabilities(const ServerCapabilities& capabilities);

    // 注册工具
    void register_tool(const Tool& tool, ToolHandler handler);

    // 列出所有工具
    std::vector<Tool> list_tools() const;

    // 调用工具
    ToolResult call_tool(const std::string& name, const json& arguments);

    // 检查工具是否存在
    bool has_tool(const std::string& name) const;

    // 注册资源
    void register_resource(const Resource& resource, ResourceProvider provider);

    // 列出所有资源
    std::vector<Resource> list_resources() const;

    // 读取资源
    ResourceContent read_resource(const std::string& uri);

    // 检查资源是否存在
    bool has_resource(const std::string& uri) const;

    // 注册提示词
    void register_prompt(const Prompt& prompt, PromptGenerator generator);

    // 列出所有提示词
    std::vector<Prompt> list_prompts() const;

    // 获取提示词
    std::vector<PromptMessage> get_prompt(const std::string& name, const json& arguments);

    // 检查提示词是否存在
    bool has_prompt(const std::string& name) const;

    // SSE 事件回调
    using SseEventCallback = std::function<void(const json&)>;
    void set_sse_callback(SseEventCallback callback);

private:
    // 服务器信息
    ServerInfo server_info_;

    // 服务器能力
    ServerCapabilities capabilities_;

    // Tools 存储，用来存储各种工具
    std::unordered_map<std::string, Tool> tools_;
    std::unordered_map<std::string, ToolHandler> tool_handlers_;
    mutable std::shared_mutex tools_mutex_;

    // Resources 存储
    std::unordered_map<std::string, Resource> resources_;
    std::unordered_map<std::string, ResourceProvider> resource_providers_;
    mutable std::shared_mutex resources_mutex_;

    // Prompts 存储
    std::unordered_map<std::string, Prompt> prompts_;
    std::unordered_map<std::string, PromptGenerator> prompt_generators_;
    mutable std::shared_mutex prompts_mutex_;

    // SSE 事件回调
    SseEventCallback sse_callback_;
    mutable std::mutex sse_mutex_;
};

} // namespace mcp
