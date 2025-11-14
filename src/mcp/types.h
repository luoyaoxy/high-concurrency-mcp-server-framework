/**
 * @file types.h
 * @brief MCP 协议核心类型定义
 *
 * 包含 Tools、Resources、Prompts 等核心类型
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace mcp {

using json = nlohmann::json;

// MCP 协议版本
constexpr const char* LATEST_PROTOCOL_VERSION = "2024-11-05";
constexpr const char* DEFAULT_NEGOTIATED_VERSION = "2024-11-05";

// 进度令牌类型
using ProgressToken = std::variant<std::string, int64_t>;

// 光标类型（用于分页）
using Cursor = std::string;

// 角色类型
enum class Role {
    User,
    Assistant
};

// ===== Tool (工具) 相关类型 =====

/**
 * @brief 工具输入 Schema (JSON Schema)
 */
struct ToolInputSchema {
    std::string type = "object";
    json properties;
    std::vector<std::string> required;

    json to_json() const;
    static ToolInputSchema from_json(const json& j);
};

/**
 * @brief 工具定义
 */
struct Tool {
    std::string name;                      // 工具名称
    std::string description;               // 工具描述
    ToolInputSchema input_schema;          // 输入 Schema

    json to_json() const;
    static Tool from_json(const json& j);
};

/**
 * @brief 工具调用结果内容（文本或图片等）
 */
struct ContentItem {
    std::string type;      // "text", "image", "resource"
    std::optional<std::string> text;
    std::optional<std::string> data;       // base64 for image
    std::optional<std::string> mime_type;
    std::optional<std::string> uri;        // for resource

    json to_json() const;
    static ContentItem from_json(const json& j);
};

/**
 * @brief 工具调用结果
 */
struct ToolResult {
    std::vector<ContentItem> content;
    bool is_error = false;

    json to_json() const;
    static ToolResult from_json(const json& j);
};

// ===== Resource (资源) 相关类型 =====

/**
 * @brief 资源定义
 */
struct Resource {
    std::string uri;                           // 资源 URI (如 file:///path/to/file)
    std::string name;                          // 资源名称
    std::optional<std::string> description;    // 资源描述
    std::optional<std::string> mime_type;      // MIME 类型

    json to_json() const;
    static Resource from_json(const json& j);
};

/**
 * @brief 资源内容
 */
struct ResourceContent {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string text;           // 文本内容
    std::optional<std::string> blob;  // base64 编码的二进制内容

    json to_json() const;
    static ResourceContent from_json(const json& j);
};

// ===== Prompt (提示) 相关类型 =====

/**
 * @brief 提示参数
 */
struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;

    json to_json() const;
    static PromptArgument from_json(const json& j);
};

/**
 * @brief 提示定义
 */
struct Prompt {
    std::string name;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;

    json to_json() const;
    static Prompt from_json(const json& j);
};

/**
 * @brief 提示消息
 */
struct PromptMessage {
    Role role;
    json content;

    json to_json() const;
    static PromptMessage from_json(const json& j);
};

// ===== Initialization (初始化) 相关类型 =====

/**
 * @brief 服务器能力
 */
struct ServerCapabilities {
    struct ToolsCapability {
        bool list_changed = false;

        json to_json() const;
        static ToolsCapability from_json(const json& j);
    };

    struct ResourcesCapability {
        bool subscribe = false;
        bool list_changed = false;

        json to_json() const;
        static ResourcesCapability from_json(const json& j);
    };

    struct PromptsCapability {
        bool list_changed = false;

        json to_json() const;
        static PromptsCapability from_json(const json& j);
    };

    std::optional<ToolsCapability> tools;
    std::optional<ResourcesCapability> resources;
    std::optional<PromptsCapability> prompts;
    std::optional<json> logging;

    json to_json() const;
    static ServerCapabilities from_json(const json& j);
};

/**
 * @brief 服务器信息
 */
struct ServerInfo {
    std::string name;
    std::string version;

    json to_json() const;
    static ServerInfo from_json(const json& j);
};

/**
 * @brief 初始化结果
 */
struct InitializeResult {
    std::string protocol_version;
    ServerCapabilities capabilities;
    ServerInfo server_info;

    json to_json() const;
    static InitializeResult from_json(const json& j);
};

} // namespace mcp
