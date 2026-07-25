# 高并发 MCP Server 框架

基于 C++17 构建的高性能、高并发 **Model Context Protocol (MCP)** 服务端框架。通过多通道任务隔离、熔断保护、SSE 实时推送以及灵活传输层支持，从容应对高负载工具调用场景。

## 核心特性

### 并发调度

- **多通道任务隔离** — 请求按类型路由到独立的执行通道（`default`、`tool`、`resource`、`prompt`），每个通道拥有独立的有界队列与工作线程池，慢工具不会拖垮资源读取或提示词生成。
- **独立工作线程池** — 每个通道可独立配置线程数与队列容量，彻底消除资源争抢。
- **取消与超时** — 请求级别的取消令牌 + 截止时间传播，确保已取消的任务及时停止，不浪费计算资源。
- **非阻塞提交** — `submit()` 调用立即返回 `future`，支持 fire-and-forget 或异步等待结果。

### 可靠性

- **工具级熔断器** — 某个工具连续失败达到阈值后自动熔断，保护工作线程池。冷却结束后通过半开探测自动恢复。
- **可配置重试** — 幂等工具可声明重试策略，框架透明处理重试逻辑。
- **全链路有界限流** — 每个队列、线程池、SSE 连接数均有硬上限，防止高负载下内存无限增长。

### 传输与流式推送

- **HTTP JSON-RPC** — 标准 JSON-RPC 2.0 over HTTP，支持请求/响应模式。
- **SSE（Server-Sent Events）** — 使用独立端口和专用工作线程池驱动实时事件流，支持断线重连后的事件回放。
- **stdio 传输** — 完整的 stdio 传输支持，可直接接入 Claude Desktop 等本地 MCP 集成场景。

### 可观测性

- **内置指标** — 调度器指标（队列深度、活跃线程数、任务延迟）以 MCP 资源形式暴露（`mcp://server/metrics`）。
- **结构化日志** — 基于 `spdlog`，支持日志轮转、可配置级别、控制台与文件双输出。

### 内置工具

框架自带示例工具，演示完整的工具注册与执行流程：

| 工具 | 说明 |
|---|---|
| `echo` | 回显输入消息 |
| `calculate` | 基本四则运算（加、减、乘、除） |
| `get_time` | 获取当前系统时间 |
| `get_weather` | 通过 Open-Meteo 公开 API 获取真实天气数据 |
| `write_file` | 文件写入（带路径级互斥锁） |

### 客户端 SDK

开箱即用的 C++ 客户端库，完整支持 MCP 协议：`initialize`、`tools/list`、`tools/call`、`resources/list`、`resources/read`、`prompts/list`、`prompts/get`。

## 架构概览

```
┌─────────────────────────────────────────────────────┐
│                    传输层                             │
│   HTTP JSON-RPC  │  SSE Server  │  stdio Transport  │
├─────────────────────────────────────────────────────┤
│                   任务运行时                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ Default  │  │   Tool   │  │ Resource / Prompt │  │
│  │   通道   │  │   通道   │  │      通道         │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
│  ┌──────────────────────────────────────────────┐   │
│  │          取消注册中心 + 指标采集              │   │
│  └──────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────┤
│                    MCP Server                        │
│   Tools  │  Resources  │  Prompts  │  Capabilities  │
├─────────────────────────────────────────────────────┤
│                   可靠机制                            │
│     熔断器  │  重试策略  │  超时控制                   │
└─────────────────────────────────────────────────────┘
```

## 快速开始

### 环境要求

- C++17 编译器（GCC 9+ / Clang 10+ / MSVC 2019+）
- CMake 3.16+
- [vcpkg](https://github.com/microsoft/vcpkg) 依赖管理

### 依赖库

| 库 | 用途 |
|---|---|
| `nlohmann-json` | JSON 解析与序列化 |
| `cpp-httplib` | HTTP 服务端/客户端 |
| `spdlog` | 结构化日志 |
| `libcurl` | 外部 API 调用（天气工具） |
| `Google Test` | 单元测试 |

### 编译

```bash
# 通过 vcpkg 安装依赖
vcpkg install curl nlohmann-json spdlog gtest cpp-httplib

# 配置并编译
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

启用 AddressSanitizer（调试构建）：

```bash
cmake -B build -DENABLE_ASAN=ON \
  -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 启动服务

```bash
# HTTP 模式（默认）
./build/src/mcp_server --mode http --port 8089

# stdio 模式（接入 Claude Desktop）
./build/src/mcp_server --mode stdio

# 同时启动两种模式
./build/src/mcp_server --mode both --port 8089
```

### 配置说明

编辑 [config/server.json](config/server.json)：

```jsonc
{
  "server": {
    "port": 8089,                           // JSON-RPC HTTP 端口
    "sse_port": 8090,                       // SSE 流式推送端口（独立）
    "sse_workers": 2,                       // SSE 工作线程数
    "worker_threads": 4,                    // Default 通道线程数
    "max_pending_tasks": 128,               // Default 通道队列容量
    "tool_workers": 2,                      // Tool 通道线程数
    "tool_max_pending_tasks": 32,           // Tool 通道队列容量
    "tool_circuit_failure_threshold": 5,    // 连续失败 N 次触发熔断
    "tool_circuit_open_ms": 30000,          // 熔断持续时间（毫秒）
    "resource_workers": 2,                  // Resource 通道线程数
    "resource_max_pending_tasks": 32,       // Resource 通道队列容量
    "prompt_workers": 2,                    // Prompt 通道线程数
    "prompt_max_pending_tasks": 32,         // Prompt 通道队列容量
    "sse_max_clients": 16,                  // SSE 最大同时连接数
    "sse_replay_buffer_events": 256         // SSE 事件回放缓冲区大小
  },
  "logging": {
    "log_level": "info",                    // 日志级别
    "log_console_output": true              // 是否输出到控制台
  }
}
```

### 客户端示例

```cpp
#include "mcp_client.h"

using namespace mcp;

int main() {
    // 连接 MCP Server
    McpClient client("localhost", 8089);

    // 初始化
    auto info = client.initialize();
    std::cout << "服务器: " << info.server_info.name << "\n";

    // 列出所有工具
    auto tools = client.list_tools();

    // 调用工具
    auto result = client.call_tool("calculate", {
        {"operation", "add"},
        {"a", 123},
        {"b", 456}
    });
    // result.content[0].text => "579"

    // 读取资源
    auto sys_info = client.read_resource("system://info");

    // SSE 事件流
    // 服务器状态流: http://localhost:8090/sse/events
    // 工具调用流:   http://localhost:8090/sse/tool_calls
}
```

完整示例见：[examples/client_demo.cpp](examples/client_demo.cpp)

### 运行测试

```bash
cd build && ctest --output-on-failure
```

## 项目结构

```
├── config/                  # JSON 配置文件
├── examples/                # 客户端 SDK 使用示例
├── scripts/                 # 辅助脚本
├── src/
│   ├── config/              # 配置加载与校验
│   ├── json_rpc/            # JSON-RPC 运行时、任务调度、SSE、传输层
│   ├── logger/              # 日志抽象层（spdlog）
│   ├── mcp/                 # MCP 协议：工具、资源、提示词、熔断器
│   ├── mcp_client/          # 客户端 SDK
│   └── mcp_server_main.cpp  # 服务端入口
├── tests/                   # 单元测试（Google Test）
├── CMakeLists.txt
└── vcpkg.json
```

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。
