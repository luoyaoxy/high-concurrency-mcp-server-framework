# C++ MCP (Model Context Protocol) 完整开发指南

## 项目概述

本指南将帮助你从零开始使用 C++ 实现一个完整的 MCP (Model Context Protocol) 服务器和客户端。MCP 是一个用于大语言模型与外部工具、数据源进行标准化交互的协议。

## 目录

1. [架构设计](#架构设计)
2. [技术栈选型](#技术栈选型)
3. [开发路线图](#开发路线图)
4. [详细实现步骤](#详细实现步骤)

---

## 架构设计

### 核心模块划分

```
mcp-cpp/
├── include/
│   ├── mcp/
│   │   ├── types.hpp           # 协议类型定义
│   │   ├── json_rpc.hpp        # JSON-RPC 2.0 实现
│   │   ├── transport.hpp       # 传输层抽象
│   │   ├── server.hpp          # MCP 服务器
│   │   ├── client.hpp          # MCP 客户端
│   │   └── utils.hpp           # 工具函数
├── src/
│   ├── types.cpp
│   ├── json_rpc.cpp
│   ├── transport/
│   │   ├── stdio_transport.cpp
│   │   ├── http_transport.cpp
│   │   └── websocket_transport.cpp
│   ├── server.cpp
│   ├── client.cpp
│   └── utils.cpp
├── examples/
│   ├── simple_echo_server.cpp
│   ├── weather_server.cpp
│   ├── file_resource_server.cpp
│   └── simple_client.cpp
├── tests/
│   └── unit_tests/
├── CMakeLists.txt
└── README.md
```

### MCP 协议层次

```
┌─────────────────────────────────┐
│   Application Layer             │  ← Tools, Resources, Prompts
│   (业务逻辑层)                  │
├─────────────────────────────────┤
│   MCP Protocol Layer            │  ← Request/Response/Notification
│   (协议层)                      │
├─────────────────────────────────┤
│   JSON-RPC 2.0 Layer           │  ← method, params, id
│   (RPC 层)                      │
├─────────────────────────────────┤
│   Transport Layer               │  ← stdio, HTTP, WebSocket
│   (传输层)                      │
└─────────────────────────────────┘
```

---

## 技术栈选型

### 必需依赖

1. **JSON 处理**: [nlohmann/json](https://github.com/nlohmann/json)
   - 现代化 C++ JSON 库
   - 安装: `brew install nlohmann-json` 或通过 CMake FetchContent

2. **HTTP 服务器**: [cpp-httplib](https://github.com/yhirose/cpp-httplib)
   - 单头文件 HTTP/HTTPS 库
   - 支持 SSE (Server-Sent Events)

3. **WebSocket**: [uWebSockets](https://github.com/uNetworking/uWebSockets) 或 [websocketpp](https://github.com/zaphoyd/websocketpp)
   - 高性能 WebSocket 实现

4. **命令行参数**: [CLI11](https://github.com/CLIUtils/CLI11)
   - 现代化命令行解析库

5. **日志**: [spdlog](https://github.com/gabime/spdlog)
   - 快速的 C++ 日志库

6. **测试框架**: [Google Test](https://github.com/google/googletest)
   - 单元测试和集成测试

### 可选依赖

- **异步 I/O**: [asio](https://github.com/chriskohlhoff/asio) - 用于高性能网络编程
- **协程支持**: C++20 协程 (需要 GCC 11+ 或 Clang 14+)

---

## 开发路线图

### 阶段 0: 环境准备 (1 天)
- ✅ 配置 C++ 开发环境
- ✅ 安装依赖库
- ✅ 创建项目结构

### 阶段 1: 基础类型系统 (2-3 天)
- ✅ 实现 JSON-RPC 2.0 基础类型
- ✅ 实现 MCP 协议类型定义
- ✅ 单元测试

### 阶段 2: 传输层实现 (3-4 天)
- ✅ stdio 传输 (标准输入/输出)
- ✅ HTTP/SSE 传输
- ✅ WebSocket 传输 (可选)

### 阶段 3: 服务器核心 (4-5 天)
- ✅ 服务器基础框架
- ✅ 工具 (Tools) 注册和调用
- ✅ 资源 (Resources) 提供
- ✅ 提示 (Prompts) 管理

### 阶段 4: 客户端实现 (3-4 天)
- ✅ 客户端基础框架
- ✅ 连接管理
- ✅ 请求/响应处理

### 阶段 5: 示例和文档 (2-3 天)
- ✅ 编写示例程序
- ✅ API 文档
- ✅ 使用教程

**总预计时间: 15-20 天**

---

## 详细实现步骤

---

## 第一步: 环境准备和项目初始化

### 1.1 创建项目目录结构

```bash
mkdir mcp-cpp && cd mcp-cpp
mkdir -p include/mcp src examples tests cmake
```

### 1.2 创建 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(mcp-cpp VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译选项
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 依赖管理
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# cpp-httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# CLI11
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
)
FetchContent_MakeAvailable(cli11)

# Google Test (for testing)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# 项目源文件
set(MCP_SOURCES
    src/types.cpp
    src/json_rpc.cpp
    src/server.cpp
    src/client.cpp
    src/utils.cpp
    src/transport/stdio_transport.cpp
    src/transport/http_transport.cpp
)

# 创建库
add_library(mcp ${MCP_SOURCES})
target_include_directories(mcp PUBLIC include)
target_link_libraries(mcp PUBLIC
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
)

# 示例程序
add_executable(simple_echo_server examples/simple_echo_server.cpp)
target_link_libraries(simple_echo_server PRIVATE mcp CLI11::CLI11)

add_executable(simple_client examples/simple_client.cpp)
target_link_libraries(simple_client PRIVATE mcp CLI11::CLI11)

# 测试
enable_testing()
add_subdirectory(tests)
```

### 1.3 创建 README.md

```markdown
# MCP C++ Implementation

A modern C++20 implementation of the Model Context Protocol (MCP).

## Build

\`\`\`bash
mkdir build && cd build
cmake ..
cmake --build .
\`\`\`

## Run Examples

\`\`\`bash
# Echo Server
./simple_echo_server --stdio

# Client
./simple_client --connect stdio --command ./simple_echo_server
\`\`\`
```

---

## 第二步: 实现基础类型系统

### 2.1 JSON-RPC 2.0 类型定义

创建 `include/mcp/json_rpc.hpp`:

```cpp
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace mcp {

using json = nlohmann::json;

// JSON-RPC 2.0 请求 ID 类型
using RequestId = std::variant<int64_t, std::string>;

// JSON-RPC 2.0 错误码
enum class JsonRpcErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
};

// JSON-RPC 错误对象
struct JsonRpcError {
    int code;
    std::string message;
    std::optional<json> data;

    json to_json() const;
    static JsonRpcError from_json(const json& j);
};

// JSON-RPC 请求
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::string method;
    std::optional<json> params;
    std::optional<RequestId> id;

    json to_json() const;
    static JsonRpcRequest from_json(const json& j);

    // 判断是否为通知（无 id）
    bool is_notification() const { return !id.has_value(); }
};

// JSON-RPC 响应
struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    RequestId id;
    std::optional<json> result;
    std::optional<JsonRpcError> error;

    json to_json() const;
    static JsonRpcResponse from_json(const json& j);

    bool is_error() const { return error.has_value(); }
};

// JSON-RPC 通知
struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    std::string method;
    std::optional<json> params;

    json to_json() const;
    static JsonRpcNotification from_json(const json& j);
};

} // namespace mcp
```

### 2.2 实现 JSON-RPC 序列化

创建 `src/json_rpc.cpp`:

```cpp
#include "mcp/json_rpc.hpp"
#include <stdexcept>

namespace mcp {

// JsonRpcError 实现
json JsonRpcError::to_json() const {
    json j = {
        {"code", code},
        {"message", message}
    };
    if (data) {
        j["data"] = *data;
    }
    return j;
}

JsonRpcError JsonRpcError::from_json(const json& j) {
    JsonRpcError err;
    err.code = j.at("code").get<int>();
    err.message = j.at("message").get<std::string>();
    if (j.contains("data")) {
        err.data = j["data"];
    }
    return err;
}

// JsonRpcRequest 实现
json JsonRpcRequest::to_json() const {
    json j = {
        {"jsonrpc", jsonrpc},
        {"method", method}
    };

    if (params) {
        j["params"] = *params;
    }

    if (id) {
        std::visit([&j](auto&& arg) {
            j["id"] = arg;
        }, *id);
    }

    return j;
}

JsonRpcRequest JsonRpcRequest::from_json(const json& j) {
    JsonRpcRequest req;
    req.jsonrpc = j.value("jsonrpc", "2.0");
    req.method = j.at("method").get<std::string>();

    if (j.contains("params")) {
        req.params = j["params"];
    }

    if (j.contains("id")) {
        if (j["id"].is_number_integer()) {
            req.id = j["id"].get<int64_t>();
        } else if (j["id"].is_string()) {
            req.id = j["id"].get<std::string>();
        }
    }

    return req;
}

// JsonRpcResponse 实现
json JsonRpcResponse::to_json() const {
    json j = {{"jsonrpc", jsonrpc}};

    std::visit([&j](auto&& arg) {
        j["id"] = arg;
    }, id);

    if (result) {
        j["result"] = *result;
    }

    if (error) {
        j["error"] = error->to_json();
    }

    return j;
}

JsonRpcResponse JsonRpcResponse::from_json(const json& j) {
    JsonRpcResponse resp;
    resp.jsonrpc = j.value("jsonrpc", "2.0");

    if (j["id"].is_number_integer()) {
        resp.id = j["id"].get<int64_t>();
    } else if (j["id"].is_string()) {
        resp.id = j["id"].get<std::string>();
    }

    if (j.contains("result")) {
        resp.result = j["result"];
    }

    if (j.contains("error")) {
        resp.error = JsonRpcError::from_json(j["error"]);
    }

    return resp;
}

// JsonRpcNotification 实现
json JsonRpcNotification::to_json() const {
    json j = {
        {"jsonrpc", jsonrpc},
        {"method", method}
    };

    if (params) {
        j["params"] = *params;
    }

    return j;
}

JsonRpcNotification JsonRpcNotification::from_json(const json& j) {
    JsonRpcNotification notif;
    notif.jsonrpc = j.value("jsonrpc", "2.0");
    notif.method = j.at("method").get<std::string>();

    if (j.contains("params")) {
        notif.params = j["params"];
    }

    return notif;
}

} // namespace mcp
```

---

## 第三步: MCP 协议类型定义

### 3.1 MCP 核心类型

创建 `include/mcp/types.hpp`:

```cpp
#pragma once

#include "mcp/json_rpc.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mcp {

// MCP 协议版本
constexpr const char* LATEST_PROTOCOL_VERSION = "2025-06-18";
constexpr const char* DEFAULT_NEGOTIATED_VERSION = "2025-03-26";

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

// 工具输入 Schema
struct ToolInputSchema {
    std::string type = "object";
    json properties;
    std::vector<std::string> required;

    json to_json() const;
    static ToolInputSchema from_json(const json& j);
};

// 工具定义
struct Tool {
    std::string name;
    std::string description;
    ToolInputSchema input_schema;

    json to_json() const;
    static Tool from_json(const json& j);
};

// 工具调用结果
struct ToolResult {
    std::vector<json> content;  // TextContent, ImageContent, etc.
    bool is_error = false;

    json to_json() const;
    static ToolResult from_json(const json& j);
};

// ===== Resource (资源) 相关类型 =====

// 资源定义
struct Resource {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;

    json to_json() const;
    static Resource from_json(const json& j);
};

// 资源内容
struct ResourceContent {
    std::string uri;
    std::optional<std::string> mime_type;
    std::variant<std::string, std::vector<uint8_t>> content;  // text or blob

    json to_json() const;
    static ResourceContent from_json(const json& j);
};

// ===== Prompt (提示) 相关类型 =====

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

// ===== Initialization (初始化) 相关类型 =====

// 服务器能力
struct ServerCapabilities {
    struct Tools {
        bool list_changed = false;
    };

    struct Resources {
        bool subscribe = false;
        bool list_changed = false;
    };

    struct Prompts {
        bool list_changed = false;
    };

    std::optional<Tools> tools;
    std::optional<Resources> resources;
    std::optional<Prompts> prompts;
    std::optional<json> logging;

    json to_json() const;
    static ServerCapabilities from_json(const json& j);
};

// 服务器信息
struct ServerInfo {
    std::string name;
    std::string version;

    json to_json() const;
    static ServerInfo from_json(const json& j);
};

// 初始化结果
struct InitializeResult {
    std::string protocol_version;
    ServerCapabilities capabilities;
    ServerInfo server_info;

    json to_json() const;
    static InitializeResult from_json(const json& j);
};

} // namespace mcp
```

---

## 第四步: 传输层实现

### 4.1 传输层抽象接口

创建 `include/mcp/transport.hpp`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace mcp {

// 消息回调类型
using MessageCallback = std::function<void(const std::string& message)>;
using ErrorCallback = std::function<void(const std::string& error)>;
using CloseCallback = std::function<void()>;

// 传输层抽象基类
class Transport {
public:
    virtual ~Transport() = default;

    // 启动传输
    virtual void start() = 0;

    // 停止传输
    virtual void stop() = 0;

    // 发送消息
    virtual void send(const std::string& message) = 0;

    // 设置回调
    virtual void on_message(MessageCallback callback) = 0;
    virtual void on_error(ErrorCallback callback) = 0;
    virtual void on_close(CloseCallback callback) = 0;

    // 判断是否已连接
    virtual bool is_connected() const = 0;
};

// 工厂函数
std::unique_ptr<Transport> create_stdio_transport();
std::unique_ptr<Transport> create_http_transport(const std::string& host, int port);

} // namespace mcp
```

### 4.2 Stdio 传输实现

创建 `src/transport/stdio_transport.cpp`:

```cpp
#include "mcp/transport.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <atomic>

namespace mcp {

class StdioTransport : public Transport {
private:
    std::atomic<bool> running_{false};
    std::thread read_thread_;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    CloseCallback close_callback_;

public:
    StdioTransport() = default;

    ~StdioTransport() override {
        stop();
    }

    void start() override {
        if (running_.exchange(true)) {
            return;  // Already running
        }

        spdlog::info("Starting stdio transport");

        // 启动读取线程
        read_thread_ = std::thread([this]() {
            read_loop();
        });
    }

    void stop() override {
        if (!running_.exchange(false)) {
            return;  // Already stopped
        }

        spdlog::info("Stopping stdio transport");

        if (read_thread_.joinable()) {
            read_thread_.join();
        }

        if (close_callback_) {
            close_callback_();
        }
    }

    void send(const std::string& message) override {
        // 写入到 stdout
        std::cout << message << std::endl;
        std::cout.flush();

        spdlog::debug("Sent message: {}", message);
    }

    void on_message(MessageCallback callback) override {
        message_callback_ = std::move(callback);
    }

    void on_error(ErrorCallback callback) override {
        error_callback_ = std::move(callback);
    }

    void on_close(CloseCallback callback) override {
        close_callback_ = std::move(callback);
    }

    bool is_connected() const override {
        return running_.load();
    }

private:
    void read_loop() {
        std::string line;
        while (running_.load()) {
            if (std::getline(std::cin, line)) {
                if (!line.empty()) {
                    spdlog::debug("Received message: {}", line);

                    if (message_callback_) {
                        message_callback_(line);
                    }
                }
            } else {
                // EOF or error
                if (std::cin.eof()) {
                    spdlog::info("EOF received, closing transport");
                    running_ = false;
                    break;
                }

                if (std::cin.fail()) {
                    std::string error = "Read error from stdin";
                    spdlog::error(error);

                    if (error_callback_) {
                        error_callback_(error);
                    }

                    running_ = false;
                    break;
                }
            }
        }
    }
};

std::unique_ptr<Transport> create_stdio_transport() {
    return std::make_unique<StdioTransport>();
}

} // namespace mcp
```

---

## 第五步: MCP 服务器核心实现

### 5.1 服务器接口定义

创建 `include/mcp/server.hpp`:

```cpp
#pragma once

#include "mcp/json_rpc.hpp"
#include "mcp/transport.hpp"
#include "mcp/types.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace mcp {

// 工具处理函数类型
using ToolHandler = std::function<ToolResult(const json& params)>;

// 资源读取函数类型
using ResourceReader = std::function<ResourceContent(const std::string& uri)>;

// 提示获取函数类型
using PromptGetter = std::function<std::vector<PromptMessage>(
    const std::string& name,
    const std::map<std::string, std::string>& arguments
)>;

// MCP 服务器
class Server {
public:
    explicit Server(const std::string& name, const std::string& version = "1.0.0");
    ~Server();

    // 设置传输层
    void set_transport(std::unique_ptr<Transport> transport);

    // 注册工具
    void register_tool(const Tool& tool, ToolHandler handler);

    // 注册资源
    void register_resource(const Resource& resource, ResourceReader reader);

    // 注册提示
    void register_prompt(const Prompt& prompt, PromptGetter getter);

    // 启动服务器
    void run();

    // 停止服务器
    void stop();

    // 服务器信息
    const ServerInfo& info() const { return server_info_; }

private:
    // 处理消息
    void handle_message(const std::string& message);

    // 处理请求
    void handle_request(const JsonRpcRequest& request);

    // 处理各种 MCP 方法
    void handle_initialize(const JsonRpcRequest& request);
    void handle_tools_list(const JsonRpcRequest& request);
    void handle_tools_call(const JsonRpcRequest& request);
    void handle_resources_list(const JsonRpcRequest& request);
    void handle_resources_read(const JsonRpcRequest& request);
    void handle_prompts_list(const JsonRpcRequest& request);
    void handle_prompts_get(const JsonRpcRequest& request);

    // 发送响应
    void send_response(const JsonRpcResponse& response);
    void send_error(const RequestId& id, JsonRpcErrorCode code, const std::string& message);

private:
    ServerInfo server_info_;
    ServerCapabilities capabilities_;
    std::unique_ptr<Transport> transport_;

    // 注册的工具、资源、提示
    std::map<std::string, std::pair<Tool, ToolHandler>> tools_;
    std::map<std::string, std::pair<Resource, ResourceReader>> resources_;
    std::map<std::string, std::pair<Prompt, PromptGetter>> prompts_;

    bool initialized_ = false;
    std::atomic<bool> running_{false};
};

} // namespace mcp
```

### 5.2 服务器实现

创建 `src/server.cpp`:

```cpp
#include "mcp/server.hpp"
#include <spdlog/spdlog.h>

namespace mcp {

Server::Server(const std::string& name, const std::string& version)
    : server_info_{name, version} {

    // 初始化能力
    capabilities_.tools = ServerCapabilities::Tools{false};
    capabilities_.resources = ServerCapabilities::Resources{false, false};
    capabilities_.prompts = ServerCapabilities::Prompts{false};

    spdlog::info("Created MCP server: {} v{}", name, version);
}

Server::~Server() {
    stop();
}

void Server::set_transport(std::unique_ptr<Transport> transport) {
    transport_ = std::move(transport);

    // 设置回调
    transport_->on_message([this](const std::string& msg) {
        handle_message(msg);
    });

    transport_->on_error([](const std::string& error) {
        spdlog::error("Transport error: {}", error);
    });

    transport_->on_close([this]() {
        spdlog::info("Transport closed");
        running_ = false;
    });
}

void Server::register_tool(const Tool& tool, ToolHandler handler) {
    tools_[tool.name] = {tool, std::move(handler)};
    spdlog::info("Registered tool: {}", tool.name);
}

void Server::register_resource(const Resource& resource, ResourceReader reader) {
    resources_[resource.uri] = {resource, std::move(reader)};
    spdlog::info("Registered resource: {}", resource.uri);
}

void Server::register_prompt(const Prompt& prompt, PromptGetter getter) {
    prompts_[prompt.name] = {prompt, std::move(getter)};
    spdlog::info("Registered prompt: {}", prompt.name);
}

void Server::run() {
    if (!transport_) {
        throw std::runtime_error("No transport set");
    }

    running_ = true;
    transport_->start();

    spdlog::info("MCP server started");

    // 阻塞等待
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("MCP server stopped");
}

void Server::stop() {
    if (running_.exchange(false)) {
        if (transport_) {
            transport_->stop();
        }
    }
}

void Server::handle_message(const std::string& message) {
    try {
        json j = json::parse(message);

        // 解析为请求
        auto request = JsonRpcRequest::from_json(j);
        handle_request(request);

    } catch (const json::exception& e) {
        spdlog::error("JSON parse error: {}", e.what());
        send_error({}, JsonRpcErrorCode::ParseError, e.what());
    } catch (const std::exception& e) {
        spdlog::error("Error handling message: {}", e.what());
        send_error({}, JsonRpcErrorCode::InternalError, e.what());
    }
}

void Server::handle_request(const JsonRpcRequest& request) {
    spdlog::debug("Handling request: {}", request.method);

    if (request.method == "initialize") {
        handle_initialize(request);
    } else if (request.method == "tools/list") {
        handle_tools_list(request);
    } else if (request.method == "tools/call") {
        handle_tools_call(request);
    } else if (request.method == "resources/list") {
        handle_resources_list(request);
    } else if (request.method == "resources/read") {
        handle_resources_read(request);
    } else if (request.method == "prompts/list") {
        handle_prompts_list(request);
    } else if (request.method == "prompts/get") {
        handle_prompts_get(request);
    } else {
        if (request.id) {
            send_error(*request.id, JsonRpcErrorCode::MethodNotFound,
                      "Method not found: " + request.method);
        }
    }
}

void Server::handle_initialize(const JsonRpcRequest& request) {
    if (!request.id) {
        return;
    }

    InitializeResult result;
    result.protocol_version = LATEST_PROTOCOL_VERSION;
    result.capabilities = capabilities_;
    result.server_info = server_info_;

    JsonRpcResponse response;
    response.id = *request.id;
    response.result = result.to_json();

    send_response(response);

    initialized_ = true;
    spdlog::info("Server initialized");
}

void Server::handle_tools_list(const JsonRpcRequest& request) {
    if (!request.id) {
        return;
    }

    json tools_array = json::array();
    for (const auto& [name, tool_pair] : tools_) {
        tools_array.push_back(tool_pair.first.to_json());
    }

    JsonRpcResponse response;
    response.id = *request.id;
    response.result = {{"tools", tools_array}};

    send_response(response);
}

void Server::handle_tools_call(const JsonRpcRequest& request) {
    if (!request.id || !request.params) {
        return;
    }

    try {
        std::string tool_name = request.params->at("name").get<std::string>();
        json arguments = request.params->value("arguments", json::object());

        auto it = tools_.find(tool_name);
        if (it == tools_.end()) {
            send_error(*request.id, JsonRpcErrorCode::MethodNotFound,
                      "Tool not found: " + tool_name);
            return;
        }

        // 调用工具处理函数
        ToolResult result = it->second.second(arguments);

        JsonRpcResponse response;
        response.id = *request.id;
        response.result = result.to_json();

        send_response(response);

    } catch (const std::exception& e) {
        send_error(*request.id, JsonRpcErrorCode::InternalError, e.what());
    }
}

void Server::send_response(const JsonRpcResponse& response) {
    if (!transport_) {
        return;
    }

    std::string message = response.to_json().dump();
    transport_->send(message);
}

void Server::send_error(const RequestId& id, JsonRpcErrorCode code, const std::string& message) {
    JsonRpcResponse response;
    response.id = id;
    response.error = JsonRpcError{
        static_cast<int>(code),
        message,
        std::nullopt
    };

    send_response(response);
}

} // namespace mcp
```

---

## 第六步: 编写第一个示例

### 6.1 简单的 Echo 服务器

创建 `examples/simple_echo_server.cpp`:

```cpp
#include <mcp/server.hpp>
#include <mcp/transport.hpp>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <csignal>

static std::atomic<bool> should_exit{false};

void signal_handler(int signal) {
    spdlog::info("Received signal {}, exiting...", signal);
    should_exit = true;
}

int main(int argc, char** argv) {
    CLI::App app{"MCP Echo Server Example"};

    bool use_stdio = false;
    std::string host = "localhost";
    int port = 8080;

    app.add_flag("--stdio", use_stdio, "Use stdio transport");
    app.add_option("--host", host, "HTTP host")->default_val("localhost");
    app.add_option("--port", port, "HTTP port")->default_val(8080);

    CLI11_PARSE(app, argc, argv);

    // 设置日志级别
    spdlog::set_level(spdlog::level::debug);

    // 创建服务器
    mcp::Server server("EchoServer", "1.0.0");

    // 注册 echo 工具
    mcp::Tool echo_tool;
    echo_tool.name = "echo";
    echo_tool.description = "Echo the input text back";
    echo_tool.input_schema.properties = {
        {"text", {{"type", "string"}, {"description", "Text to echo"}}}
    };
    echo_tool.input_schema.required = {"text"};

    server.register_tool(echo_tool, [](const nlohmann::json& params) -> mcp::ToolResult {
        std::string text = params.at("text").get<std::string>();

        mcp::ToolResult result;
        result.content.push_back({
            {"type", "text"},
            {"text", text}
        });

        return result;
    });

    // 设置传输
    if (use_stdio) {
        server.set_transport(mcp::create_stdio_transport());
    } else {
        server.set_transport(mcp::create_http_transport(host, port));
    }

    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 运行服务器
    spdlog::info("Starting Echo Server...");
    server.run();

    return 0;
}
```

---

## 第七步: 测试和使用

### 7.1 构建项目

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 7.2 运行服务器

```bash
# 使用 stdio 传输
./simple_echo_server --stdio
```

### 7.3 测试（手动）

在另一个终端，发送 JSON-RPC 请求：

```bash
# 初始化
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}}' | ./simple_echo_server --stdio

# 列出工具
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./simple_echo_server --stdio

# 调用 echo 工具
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"text":"Hello, MCP!"}}}' | ./simple_echo_server --stdio
```

---

## 下一步开发计划

### 第八步: 实现客户端 (3-4 天)
- 连接管理
- 请求/响应处理
- 工具调用接口

### 第九步: HTTP/SSE 传输 (2-3 天)
- 实现 HTTP 服务器
- 实现 SSE (Server-Sent Events)
- 支持状态会话

### 第十步: 完善功能 (5-7 天)
- 资源 (Resources) 完整实现
- 提示 (Prompts) 完整实现
- 进度通知
- 日志记录
- 补全建议

### 第十一步: 测试和文档 (3-4 天)
- 单元测试
- 集成测试
- API 文档
- 使用示例
- 性能优化

---

## 推荐开发顺序

1. **Week 1**: 步骤 1-4 (环境、类型、JSON-RPC、传输层)
2. **Week 2**: 步骤 5-7 (服务器核心、示例、测试)
3. **Week 3**: 步骤 8-9 (客户端、HTTP 传输)
4. **Week 4**: 步骤 10-11 (完善功能、文档)

---

## 学习资源

- [MCP 官方规范](https://modelcontextprotocol.io/specification)
- [JSON-RPC 2.0 规范](https://www.jsonrpc.org/specification)
- [nlohmann/json 文档](https://json.nlohmann.me/)
- [现代 C++ 最佳实践](https://github.com/cpp-best-practices)

---

## 常见问题

### Q: 为什么选择 C++20?
A: C++20 提供了协程、概念、ranges 等现代特性，可以编写更简洁、高效的代码。

### Q: 如何处理异步操作?
A: 可以使用 C++20 协程或 asio 库实现异步 I/O。

### Q: 如何测试 MCP 服务器?
A: 可以使用 Python SDK 的客户端或 MCP Inspector 进行测试。

---

**祝你开发顺利！如有问题，随时交流。**


❌ 待开发的主要内容
根据开发指南，你还需要完成：
1. HTTP/SSE 传输层 (2-3天)
实现 HTTP 服务器（使用 cpp-httplib）
实现 SSE (Server-Sent Events) 支持
支持多客户端连接
2. WebSocket 传输层 (可选, 1-2天)
使用 websocketpp 或 uWebSockets
支持双向实时通信
3. MCP 协议完整类型定义 (1-2天)
Tools (工具) 类型
Resources (资源) 类型
Prompts (提示) 类型
完整的 MCP 能力声明
4. 服务器核心功能 (3-4天)
✅ 基础服务器框架（部分完成）
❌ Tools 注册和调用
❌ Resources 提供
❌ Prompts 管理
❌ 进度通知
❌ 补全建议
5. MCP 客户端实现 (3-4天)
客户端基础框架
连接管理
请求/响应处理
工具调用接口
6. 示例程序 (2-3天)
✅ simple_client (部分完成)
❌ weather_server（天气服务示例）
❌ file_resource_server（文件资源服务器）
❌ 更多实用示例
7. 测试和文档 (2-3天)
✅ 基础单元测试（JSON-RPC, Logger, Config）
❌ 完整的单元测试覆盖
❌ 集成测试
❌ API 文档
❌ 使用教程
📊 当前进度总结
总体完成度：约 25-30%
✅ 已完成: 基础架构、stdio 传输、JSON-RPC 基础、日志和配置系统
🔄 进行中: stdio 通信测试和调试
❌ 待开发: HTTP/WebSocket 传输、MCP 核心功能（Tools/Resources/Prompts）、客户端、完整示例
🎯 建议的下一步优先级
高优先级（核心功能）:
实现 HTTP/SSE 传输层（这样你就能有真正的 socket 服务器了！）
实现 Tools 注册和调用功能
完善 MCP 协议类型定义
中优先级（扩展功能）:
实现 Resources 和 Prompts
编写 MCP 客户端
添加更多示例
低优先级（优化和完善）:
WebSocket 传输
完善测试覆盖
编写文档
你想先从哪个功能开始？我建议先实现 HTTP/SSE 传输层，这样你就可以有一个真正的 TCP socket 服务器了！