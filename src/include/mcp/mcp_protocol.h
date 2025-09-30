#pragma once

#include "json_rpc.h"
#include <vector>
#include <unordered_map>

namespace mcp {

// 工具参数定义结构体，描述工具的输入参数
struct ToolParameter {
    std::string type;                      // 参数类型（string, number, boolean 等）
    std::string description;               // 参数描述
    std::optional<json> default_value;     // 默认值（可选）
    bool required = false;                 // 是否为必需参数
    std::optional<json> enum_values;       // 枚举值（可选）
};

// MCP 工具定义结构体，描述服务器提供的功能
struct Tool {
    std::string name;                                          // 工具名称
    std::string description;                                   // 工具描述
    std::unordered_map<std::string, ToolParameter> parameters; // 工具参数映射

    json ToJson() const;                                       // 转换为 JSON 格式
    static Tool FromJson(const json& j);                       // 从 JSON 格式解析
};

// MCP 资源定义结构体，描述可访问的数据资源
struct Resource {
    std::string uri;                        // 资源 URI 标识符
    std::string name;                       // 资源名称
    std::optional<std::string> description; // 资源描述（可选）
    std::optional<std::string> mime_type;   // MIME 类型（可选）

    json ToJson() const;                    // 转换为 JSON 格式
    static Resource FromJson(const json& j); // 从 JSON 格式解析
};

// 工具调用请求结构体，包含调用信息和参数
struct ToolCall {
    std::string name;       // 工具名称
    json arguments;         // 调用参数

    json ToJson() const;                    // 转换为 JSON 格式
    static ToolCall FromJson(const json& j); // 从 JSON 格式解析
};

// 工具执行结果结构体，包含执行结果和状态
struct ToolResult {
    std::string type;               // 结果类型（text, image 等）
    json content;                   // 结果内容
    std::optional<bool> is_error;   // 是否为错误结果（可选）

    json ToJson() const;                       // 转换为 JSON 格式
    static ToolResult FromJson(const json& j); // 从 JSON 格式解析
};

// MCP 服务器基类，实现标准的 MCP 协议处理
class McpServer : public JsonRpcHandler {
public:
    virtual ~McpServer() = default;

    // 添加工具到服务器
    void AddTool(const Tool& tool);
    // 添加资源到服务器
    void AddResource(const Resource& resource);
    
    // 处理 JSON-RPC 请求的主入口
    JsonRpcResponse HandleRequest(const JsonRpcRequest& request) override;
    
protected:
    // 处理初始化请求
    virtual JsonRpcResponse HandleInitialize(const json& params);
    // 处理工具列表请求
    virtual JsonRpcResponse HandleToolsList(const json& params);
    // 处理工具调用请求
    virtual JsonRpcResponse HandleToolsCall(const json& params);
    // 处理资源列表请求
    virtual JsonRpcResponse HandleResourcesList(const json& params);
    // 处理资源读取请求
    virtual JsonRpcResponse HandleResourcesRead(const json& params);
    
    // 纯虚函数：执行具体的工具逻辑（子类必须实现）
    virtual ToolResult ExecuteTool(const std::string& name, const json& arguments) = 0;
    // 纯虚函数：读取具体的资源内容（子类必须实现）
    virtual json ReadResource(const std::string& uri) = 0;

private:
    std::vector<Tool> tools_;           // 已注册的工具列表
    std::vector<Resource> resources_;   // 已注册的资源列表
    bool initialized_ = false;          // 初始化状态标志
};

// MCP 标准方法名称常量定义
namespace mcp_methods {
    constexpr const char* INITIALIZE = "initialize";         // 初始化方法
    constexpr const char* TOOLS_LIST = "tools/list";         // 获取工具列表方法
    constexpr const char* TOOLS_CALL = "tools/call";         // 调用工具方法
    constexpr const char* RESOURCES_LIST = "resources/list"; // 获取资源列表方法
    constexpr const char* RESOURCES_READ = "resources/read"; // 读取资源方法
}

} // namespace mcp