# MCP 核心功能实现

本文档说明如何使用 MCP (Model Context Protocol) 的核心功能：**Tools**、**Resources** 和 **Prompts**。

---

## 目录

1. [架构概览](#架构概览)
2. [Tools（工具）](#tools工具)
3. [Resources（资源）](#resources资源)
4. [Prompts（提示词）](#prompts提示词)
5. [HTTP 服务器使用](#http-服务器使用)
6. [示例程序](#示例程序)
7. [测试](#测试)

---

## 架构概览

### 核心组件

```
src/mcp/
├── types.h              # MCP 协议类型定义
├── types.cpp            # 类型序列化实现
├── mcp_server.h         # MCP 服务器核心类
└── mcp_server.cpp       # 服务器实现
```

### MCP 服务器类

`McpServer` 是核心类，提供：
- **Tools 管理**：注册、列表、调用工具
- **Resources 管理**：注册、列表、读取资源
- **Prompts 管理**：注册、列表、获取提示词

---

## Tools（工具）

### 什么是 Tool？

Tool 是可以被客户端调用的函数，类似于 Function Calling。

### 定义 Tool

```cpp
#include "mcp_server.h"

McpServer server("my-server", "1.0.0");

// 1. 创建工具定义
Tool echo_tool;
echo_tool.name = "echo";
echo_tool.description = "Echo back the input message";

// 2. 定义输入 Schema (JSON Schema)
echo_tool.input_schema.properties = {
    {"message", {
        {"type", "string"},
        {"description", "The message to echo"}
    }}
};
echo_tool.input_schema.required = {"message"};

// 3. 注册工具处理器
server.register_tool(echo_tool, [](const json& args) -> ToolResult {
    std::string message = args.at("message").get<std::string>();

    ToolResult result;
    result.content.push_back(ContentItem{
        .type = "text",
        .text = "Echo: " + message
    });
    return result;
});
```

### 使用 Tool

```cpp
// 列出所有工具
std::vector<Tool> tools = server.list_tools();
for (const auto& tool : tools) {
    std::cout << "Tool: " << tool.name << std::endl;
}

// 调用工具
json args = {{"message", "Hello, World!"}};
ToolResult result = server.call_tool("echo", args);

// 检查结果
if (!result.is_error) {
    std::cout << result.content[0].text.value() << std::endl;
}
```

### 完整示例

```cpp
// Calculator 工具
Tool calc_tool;
calc_tool.name = "add";
calc_tool.description = "Add two numbers";
calc_tool.input_schema.properties = {
    {"a", {{"type", "number"}}},
    {"b", {{"type", "number"}}}
};
calc_tool.input_schema.required = {"a", "b"};

server.register_tool(calc_tool, [](const json& args) -> ToolResult {
    double a = args.at("a").get<double>();
    double b = args.at("b").get<double>();

    ToolResult result;
    result.content.push_back(ContentItem{
        .type = "text",
        .text = std::to_string(a + b)
    });
    return result;
});
```

---

## Resources（资源）

### 什么是 Resource？

Resource 是可读的数据源，如文件、数据库、API 等。

### 定义 Resource

```cpp
// 1. 创建资源定义
Resource file_resource;
file_resource.uri = "file:///path/to/data.txt";
file_resource.name = "Data File";
file_resource.description = "Sample data file";
file_resource.mime_type = "text/plain";

// 2. 注册资源提供器
server.register_resource(file_resource, [](const std::string& uri) -> ResourceContent {
    ResourceContent content;
    content.uri = uri;
    content.mime_type = "text/plain";

    // 读取文件内容
    std::ifstream file("/path/to/data.txt");
    std::stringstream buffer;
    buffer << file.rdbuf();
    content.text = buffer.str();

    return content;
});
```

### 使用 Resource

```cpp
// 列出所有资源
std::vector<Resource> resources = server.list_resources();
for (const auto& res : resources) {
    std::cout << "Resource: " << res.uri << std::endl;
}

// 读取资源
ResourceContent content = server.read_resource("file:///path/to/data.txt");
std::cout << "Content: " << content.text << std::endl;
```

### 动态资源示例

```cpp
// 系统信息资源（动态生成）
Resource sys_info;
sys_info.uri = "system://info";
sys_info.name = "System Information";
sys_info.mime_type = "application/json";

server.register_resource(sys_info, [](const std::string& uri) -> ResourceContent {
    ResourceContent content;
    content.uri = uri;
    content.mime_type = "application/json";

    // 动态生成系统信息
    json info = {
        {"timestamp", std::time(nullptr)},
        {"hostname", "localhost"},
        {"uptime", 12345}
    };
    content.text = info.dump(2);

    return content;
});
```

---

## Prompts（提示词）

### 什么是 Prompt？

Prompt 是参数化的提示词模板，用于生成标准化的 AI 对话提示。

### 定义 Prompt

```cpp
// 1. 创建提示词定义
Prompt code_review;
code_review.name = "code_review";
code_review.description = "Generate code review prompt";

// 2. 定义参数
PromptArgument lang_arg;
lang_arg.name = "language";
lang_arg.description = "Programming language";
lang_arg.required = true;
code_review.arguments.push_back(lang_arg);

PromptArgument code_arg;
code_arg.name = "code";
code_arg.description = "Code to review";
code_arg.required = true;
code_review.arguments.push_back(code_arg);

// 3. 注册提示词生成器
server.register_prompt(code_review, [](const json& args) -> std::vector<PromptMessage> {
    std::string language = args.at("language").get<std::string>();
    std::string code = args.at("code").get<std::string>();

    std::vector<PromptMessage> messages;

    PromptMessage msg;
    msg.role = Role::User;
    msg.content = {
        {"type", "text"},
        {"text", "Please review this " + language + " code:\n\n" + code}
    };
    messages.push_back(msg);

    return messages;
});
```

### 使用 Prompt

```cpp
// 列出所有提示词
std::vector<Prompt> prompts = server.list_prompts();
for (const auto& prompt : prompts) {
    std::cout << "Prompt: " << prompt.name << std::endl;
}

// 获取提示词消息
json args = {
    {"language", "C++"},
    {"code", "int main() { return 0; }"}
};
std::vector<PromptMessage> messages = server.get_prompt("code_review", args);

// 使用消息
for (const auto& msg : messages) {
    std::cout << msg.content["text"] << std::endl;
}
```

---

## HTTP 服务器使用

### 启动完整的 MCP HTTP 服务器

```bash
cd build/src
./mcp_full_http_server --port 8080
```

### 支持的 JSON-RPC 方法

#### 1. initialize

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {}
}
```

#### 2. tools/list

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list",
  "params": {}
}
```

#### 3. tools/call

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "echo",
    "arguments": {
      "message": "Hello"
    }
  }
}
```

#### 4. resources/list

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "resources/list",
  "params": {}
}
```

#### 5. resources/read

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "resources/read",
  "params": {
    "uri": "system://info"
  }
}
```

#### 6. prompts/list

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "prompts/list",
  "params": {}
}
```

#### 7. prompts/get

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "prompts/get",
  "params": {
    "name": "code_review",
    "arguments": {
      "language": "C++",
      "code": "int main() { return 0; }"
    }
  }
}
```

### 使用 curl 测试

```bash
# 初始化
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {}
  }'

# 列出工具
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list",
    "params": {}
  }'

# 调用工具
curl -X POST http://localhost:8080/jsonrpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "echo",
      "arguments": {"message": "Hello, MCP!"}
    }
  }'
```

---

## 示例程序

### 1. MCP Demo Server（命令行演示）

```bash
cd build/examples
./mcp_demo_server
```

这个程序会：
- 注册示例 Tools（echo, add, get_time）
- 注册示例 Resources（system info, config）
- 注册示例 Prompts（code_review, translate, summarize）
- 演示如何调用这些功能

### 2. MCP Full HTTP Server（HTTP 服务器）

```bash
cd build/src
./mcp_full_http_server --port 8080
```

这是一个完整的 HTTP 服务器，支持所有 MCP 协议方法。

---

## 测试

### 运行单元测试

```bash
cd build
ctest

# 或者直接运行测试
./tests/test_mcp_server
```

### 测试覆盖

- Tools 注册、列表、调用
- Resources 注册、列表、读取
- Prompts 注册、列表、获取
- JSON 序列化/反序列化
- 错误处理

---

## 下一步

现在你已经实现了 MCP 的核心功能！可以考虑：

1. **添加更多工具**：文件操作、数据库查询、API 调用等
2. **实现 SSE**：支持服务器推送通知
3. **添加 WebSocket**：支持双向实时通信
4. **实现客户端**：创建 MCP 客户端 SDK
5. **添加认证**：API Key 或 OAuth 认证
6. **性能优化**：缓存、连接池等

---

**Happy Coding! 🚀**
