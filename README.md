# 并发 MCP Server 框架

基于 C++17 构建的高性能并发 **Model Context Protocol（MCP）** 服务端框架。项目支持 MCP `2026-07-28` 的无状态 Streamable HTTP，同时保留 stdio 和旧版 HTTP JSON-RPC 兼容路径，可直接接入 Codex 等 MCP 客户端。

## 核心特性

### MCP 2026-07-28 协议支持

- **Streamable HTTP** — 提供统一的 `POST /mcp` 端点；普通请求返回 JSON，工具调用可通过当前请求专属的 SSE 流返回结果。
- **无状态请求** — 新版请求在 `params._meta` 中携带协议版本、客户端信息和客户端能力，不依赖 `initialize` 或协议级 Session。
- **能力发现** — 实现 `server/discover`，返回服务器身份、能力和支持的协议版本。
- **请求元数据校验** — 校验 `MCP-Protocol-Version`、`Mcp-Method` 和按需提供的 `Mcp-Name`，并返回新版协议错误码。
- **结果与缓存语义** — 新版结果包含 `resultType` 和服务器信息；列表及资源读取结果包含 `ttlMs`、`cacheScope`。
- **双时代兼容** — 新版客户端使用 `/mcp`；原有 `/jsonrpc`、stdio `initialize` 和独立 SSE 服务继续为旧客户端及现有集成提供兼容。

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

- **MCP Streamable HTTP** — `POST /mcp` 支持 JSON 与请求级 SSE 响应；SSE 断开会取消对应任务。
- **兼容 HTTP JSON-RPC** — `/jsonrpc` 保留旧版请求、批处理和 Session 行为。
- **独立 SSE 服务** — 项目原有状态与工具事件流继续使用独立端口、专用线程池和事件回放。
- **stdio 传输** — 可由 Codex、Claude Desktop 等本地 MCP 客户端直接拉起进程。

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

提供兼容旧版握手协议的 C++ 客户端库，支持 `initialize`、`tools/list`、`tools/call`、`resources/list`、`resources/read`、`prompts/list`、`prompts/get`。

## 架构概览

```
┌─────────────────────────────────────────────────────┐
│                    传输层                             │
│ Streamable HTTP  │ Legacy HTTP/SSE │ stdio Transport │
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
./build/src/mcp_server --mode http --host 127.0.0.1 --port 8089 \
  --config ./config/server.json

# stdio 模式（接入 Codex、Claude Desktop）
./build/src/mcp_server --mode stdio --config ./config/server.json

# 同时启动两种模式
./build/src/mcp_server --mode both --host 127.0.0.1 --port 8089 \
  --config ./config/server.json
```

HTTP 模式默认仅监听 `127.0.0.1`，避免本地 MCP 服务意外暴露到局域网。远程部署时需显式指定 `--host`，并在反向代理或服务层配置认证。

### 接入 Codex

Codex 支持 stdio 和 Streamable HTTP 两种 MCP 连接方式。本地开发推荐使用 stdio，由 Codex 自动启动和管理服务进程：

```bash
codex mcp add mcp-conductor -- \
  /absolute/path/to/mcp-conductor/build/src/mcp_server \
  --mode stdio \
  --config /absolute/path/to/mcp-conductor/config/server.json
```

也可以手动编辑 `~/.codex/config.toml` 或项目内的 `.codex/config.toml`：

```toml
[mcp_servers.mcp-conductor]
command = "/absolute/path/to/mcp-conductor/build/src/mcp_server"
args = [
  "--mode", "stdio",
  "--config", "/absolute/path/to/mcp-conductor/config/server.json"
]
cwd = "/absolute/path/to/mcp-conductor"
```

使用最新版 Streamable HTTP 时，先启动 HTTP 服务，再执行：

```bash
codex mcp add mcp-conductor-http \
  --url http://127.0.0.1:8089/mcp
```

通过 `codex mcp list` 查看配置；进入 Codex TUI 后可使用 `/mcp` 查看连接状态。Codex CLI、IDE 扩展和 ChatGPT 桌面端会共享同一份 Codex MCP 配置。配置方式参考 [OpenAI 官方 MCP 文档](https://learn.chatgpt.com/docs/extend/mcp)。

### 协议端点

| 端点/传输 | 协议形态 | 用途 |
|---|---|---|
| `POST /mcp` | MCP `2026-07-28` Streamable HTTP | 新版无状态客户端、请求级 SSE |
| `POST /jsonrpc` | 项目旧版 HTTP JSON-RPC 兼容接口 | 原有 HTTP 客户端、批处理与 Session |
| stdio | 新旧 MCP 请求 | Codex 等本地进程集成 |
| 独立 SSE 端口 | 项目扩展事件流 | 状态推送、工具调用事件和历史回放 |

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
    "request_timeout_ms": 30000,            // 请求总超时
    "tool_workers": 2,                      // Tool 通道线程数
    "tool_max_pending_tasks": 32,           // Tool 通道队列容量
    "tool_circuit_failure_threshold": 5,    // 连续失败 N 次触发熔断
    "tool_circuit_open_ms": 30000,          // 熔断持续时间（毫秒）
    "resource_workers": 2,                  // Resource 通道线程数
    "resource_max_pending_tasks": 32,       // Resource 通道队列容量
    "prompt_workers": 2,                    // Prompt 通道线程数
    "prompt_max_pending_tasks": 32,         // Prompt 通道队列容量
    "sse_max_clients": 16,                  // SSE 最大同时连接数
    "sse_max_pending_events_per_client": 128,
    "sse_replay_buffer_events": 256         // SSE 事件回放缓冲区大小
  },
  "logging": {
    "log_file_path": "../../logs/server.log",
    "log_level": "info",                    // 日志级别
    "log_file_size": 52428800,
    "log_file_count": 5,
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
ctest --test-dir build --output-on-failure
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
