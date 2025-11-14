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

namespace mcp {

/**
 * @brief MCP 服务器核心类
 *
 * 提供 Tools、Resources、Prompts 的管理功能
 */
class McpServer {
public:
    // ===== Tool 相关类型 =====

    /**
     * @brief 工具处理器函数类型
     * @param arguments 工具参数（JSON 对象）
     * @return 工具执行结果
     */
    using ToolHandler = std::function<ToolResult(const json& arguments)>;

    // ===== Resource 相关类型 =====

    /**
     * @brief 资源内容提供器函数类型
     * @param uri 资源 URI
     * @return 资源内容
     */
    using ResourceProvider = std::function<ResourceContent(const std::string& uri)>;

    // ===== Prompt 相关类型 =====

    /**
     * @brief 提示词生成器函数类型
     * @param arguments 提示词参数
     * @return 提示词消息列表
     */
    using PromptGenerator = std::function<std::vector<PromptMessage>(const json& arguments)>;

    // ===== 构造函数 =====

    McpServer(const std::string& name, const std::string& version);

    // ===== 初始化相关 =====

    /**
     * @brief 获取初始化结果
     */
    InitializeResult get_initialize_result() const;

    /**
     * @brief 设置服务器能力
     */
    void set_capabilities(const ServerCapabilities& capabilities);

    // ===== Tools 管理 =====

    /**
     * @brief 注册工具
     * @param tool 工具定义
     * @param handler 工具处理器
     */
    void register_tool(const Tool& tool, ToolHandler handler);

    /**
     * @brief 列出所有工具
     * @return 工具列表
     */
    std::vector<Tool> list_tools() const;

    /**
     * @brief 调用工具
     * @param name 工具名称
     * @param arguments 工具参数
     * @return 工具执行结果
     */
    ToolResult call_tool(const std::string& name, const json& arguments);

    /**
     * @brief 检查工具是否存在
     */
    bool has_tool(const std::string& name) const;

    // ===== Resources 管理 =====

    /**
     * @brief 注册资源
     * @param resource 资源定义
     * @param provider 资源内容提供器
     */
    void register_resource(const Resource& resource, ResourceProvider provider);

    /**
     * @brief 列出所有资源
     * @return 资源列表
     */
    std::vector<Resource> list_resources() const;

    /**
     * @brief 读取资源
     * @param uri 资源 URI
     * @return 资源内容
     */
    ResourceContent read_resource(const std::string& uri);

    /**
     * @brief 检查资源是否存在
     */
    bool has_resource(const std::string& uri) const;

    // ===== Prompts 管理 =====

    /**
     * @brief 注册提示词
     * @param prompt 提示词定义
     * @param generator 提示词生成器
     */
    void register_prompt(const Prompt& prompt, PromptGenerator generator);

    /**
     * @brief 列出所有提示词
     * @return 提示词列表
     */
    std::vector<Prompt> list_prompts() const;

    /**
     * @brief 获取提示词
     * @param name 提示词名称
     * @param arguments 提示词参数
     * @return 提示词消息列表
     */
    std::vector<PromptMessage> get_prompt(const std::string& name, const json& arguments);

    /**
     * @brief 检查提示词是否存在
     */
    bool has_prompt(const std::string& name) const;

private:
    // 服务器信息
    ServerInfo server_info_;

    // 服务器能力
    ServerCapabilities capabilities_;

    // Tools 存储
    std::unordered_map<std::string, Tool> tools_;
    std::unordered_map<std::string, ToolHandler> tool_handlers_;
    mutable std::mutex tools_mutex_;

    // Resources 存储
    std::unordered_map<std::string, Resource> resources_;
    std::unordered_map<std::string, ResourceProvider> resource_providers_;
    mutable std::mutex resources_mutex_;

    // Prompts 存储
    std::unordered_map<std::string, Prompt> prompts_;
    std::unordered_map<std::string, PromptGenerator> prompt_generators_;
    mutable std::mutex prompts_mutex_;
};

} // namespace mcp
