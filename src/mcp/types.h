/**
 * @file types.h
 * @brief MCP 协议核心类型定义
 *
 * 包含 Tools、Resources、Prompts 等核心类型
 */

#pragma once

#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace mcp {

using json = nlohmann::json;

// MCP 协议版本。按从旧到新排列，以便保留原客户端兼容性。
inline constexpr std::array<std::string_view, 4> kSupportedProtocolVersions = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
    "2025-11-25",
};
inline constexpr char kLatestProtocolVersion[] = "2025-11-25";
inline constexpr char kDefaultNegotiatedVersion[] = "2024-11-05";

// 保留旧常量名，避免破坏项目已有使用方。
inline constexpr const char* LATEST_PROTOCOL_VERSION = kLatestProtocolVersion;
inline constexpr const char* DEFAULT_NEGOTIATED_VERSION =
    kDefaultNegotiatedVersion;

bool IsSupportedProtocolVersion(std::string_view protocol_version);
std::string NegotiateProtocolVersion(std::string_view requested_version);

// 进度令牌类型
using ProgressToken = std::variant<std::string, int64_t>;

// 光标类型（用于分页）
using Cursor = std::string;

// 角色类型
enum class Role {
    User,
    Assistant
};

// 工具输入 Schema (JSON Schema)
struct ToolInputSchema {
    std::string type = "object";
    json properties;
    std::vector<std::string> required;

    json to_json() const;
    static ToolInputSchema from_json(const json& j);
};

// 工具的内部执行策略，不暴露为 MCP tools/list 协议字段。
struct ToolExecutionPolicy {
    // 默认禁止自动重试，避免对有副作用的工具重复执行。
    bool idempotent = false;
    // 0 表示沿用 JSON-RPC 请求的总 deadline。
    int timeout_ms = 0;
    // 自动重试的额外次数；0 表示只执行一次。
    int max_retries = 0;
};

// 工具定义
struct Tool {
    std::string name;                      // 工具名称
    std::string description;               // 工具描述
    ToolInputSchema input_schema;          // 输入 Schema
    ToolExecutionPolicy execution_policy;  // 内部执行与重试策略

    json to_json() const;
    static Tool from_json(const json& j);
};

// 工具调用结果内容（文本或图片等）
struct ContentItem {
    std::string type;      // "text", "image", "resource"
    std::optional<std::string> text;
    std::optional<std::string> data;       // base64 for image
    std::optional<std::string> mime_type;
    std::optional<std::string> uri;        // for resource

    json to_json() const;
    static ContentItem from_json(const json& j);
};

// 工具调用结果
struct ToolResult {
    std::vector<ContentItem> content;
    bool is_error = false;

    json to_json() const;
    static ToolResult from_json(const json& j);
};

// 资源定义
struct Resource {
    std::string uri;                           // 资源 URI (如 file:///path/to/file)
    std::string name;                          // 资源名称
    std::optional<std::string> description;    // 资源描述
    std::optional<std::string> mime_type;      // MIME 类型

    json to_json() const;
    static Resource from_json(const json& j);
};

// 资源内容
struct ResourceContent {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string text;           // 文本内容
    std::optional<std::string> blob;  // base64 编码的二进制内容

    json to_json() const;
    static ResourceContent from_json(const json& j);
};


// 提示参数
struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;

    json to_json() const;
    static PromptArgument from_json(const json& j);
};

// 提示定义
struct Prompt {
    std::string name;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;

    json to_json() const;
    static Prompt from_json(const json& j);
};

// 提示消息
struct PromptMessage {
    Role role;
    json content;

    json to_json() const;
    static PromptMessage from_json(const json& j);
};

// 服务器能力
struct ServerCapabilities {
    // 工具能力
    struct ToolsCapability {
        bool list_changed = false; // 工具列表是否发生变化

        json to_json() const; // 转换为 JSON
        static ToolsCapability from_json(const json& j); // 从 JSON 转换为 ToolsCapability
    };

    // 资源能力
    struct ResourcesCapability {
        bool subscribe = false; // 是否订阅资源
        bool list_changed = false; // 资源列表是否发生变化

        json to_json() const; // 转换为 JSON
        static ResourcesCapability from_json(const json& j); // 从 JSON 转换为 ResourcesCapability
    };

    // 提示能力
    struct PromptsCapability {
        bool list_changed = false; // 提示列表是否发生变化  

        json to_json() const; // 转换为 JSON
        static PromptsCapability from_json(const json& j); // 从 JSON 转换为 PromptsCapability
    };

    std::optional<ToolsCapability> tools; // 工具能力
    std::optional<ResourcesCapability> resources; // 资源能力
    std::optional<PromptsCapability> prompts; // 提示能力
    std::optional<json> logging; // 日志配置

    json to_json() const; // 转换为 JSON
    static ServerCapabilities from_json(const json& j);
};


// MCP 协议服务器信息
struct ServerInfo {
    std::string name; // 服务器名称
    std::string version; // 服务器版本

    json to_json() const;
    static ServerInfo from_json(const json& j);
};

// MCP 协议初始化结果
struct InitializeResult {
    std::string protocol_version; // MCP 协议版本
    ServerCapabilities capabilities; // 服务器能力
    ServerInfo server_info; // 服务器信息
    std::optional<std::string> instructions;

    json to_json() const;
    static InitializeResult from_json(const json& j);
};

} // namespace mcp
