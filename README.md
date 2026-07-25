# High-Concurrency MCP Server Framework

A high-performance, concurrent **Model Context Protocol (MCP)** server framework built with C++17. Designed to handle heavy tool-calling workloads with lane-based task isolation, circuit breaking, SSE streaming, and flexible transport support.

## Features

### Concurrency & Scheduling

- **Multi-Lane Task Isolation** — Requests are routed into dedicated execution lanes (`default`, `tool`, `resource`, `prompt`), each with its own bounded queue and worker pool. A slow tool never starves resource reads or prompt generation.
- **Independent Worker Pools** — Configurable thread count and queue depth per lane, preventing noisy-neighbor problems.
- **Cancellation & Timeout** — Request-level cancellation tokens with deadline propagation, ensuring cancelled work stops promptly.
- **Non-blocking Submission** — `submit()` returns a future immediately, enabling fire-and-forget or async result collection.

### Reliability

- **Circuit Breaker per Tool** — A failing tool is automatically opened after a configurable number of consecutive failures, protecting the worker pool. Half-open probes allow automatic recovery.
- **Configurable Retry** — Idempotent tools can declare retry policies; the framework handles retries transparently.
- **Bounded Resource Limits** — Every queue, pool, and client connection is bounded to prevent unbounded memory growth under load.

### Transport & Streaming

- **HTTP JSON-RPC** — Standard JSON-RPC 2.0 over HTTP for request/response communication.
- **SSE (Server-Sent Events)** — Real-time event streaming on a dedicated port with its own worker pool. Supports per-client replay buffers for missed events.
- **stdio Transport** — Full stdio-based transport for Claude Desktop and other local MCP integrations.

### Observability

- **Built-in Metrics** — Scheduler metrics (queue depths, active workers, task latencies) exposed as an MCP resource (`mcp://server/metrics`).
- **Structured Logging** — `spdlog`-based logging with rotation, configurable levels, and console/file dual output.

### Tool Suite

Built-in example tools demonstrate the framework:

| Tool | Description |
|---|---|
| `echo` | Echoes back the input message |
| `calculate` | Performs basic arithmetic (`add`, `subtract`, `multiply`, `divide`) |
| `get_time` | Returns current system time |
| `get_weather` | Fetches real weather data via Open-Meteo API |
| `write_file` | Writes content to disk with path-level locking |

### Client SDK

A ready-to-use C++ client library with full MCP protocol support: `initialize`, `tools/list`, `tools/call`, `resources/list`, `resources/read`, `prompts/list`, `prompts/get`.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Transport Layer                    │
│   HTTP JSON-RPC  │  SSE Server  │  stdio Transport  │
├─────────────────────────────────────────────────────┤
│                 Task Runtime                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ Default  │  │   Tool   │  │ Resource / Prompt │  │
│  │  Lane    │  │   Lane   │  │      Lanes        │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
│  ┌──────────────────────────────────────────────┐   │
│  │        Cancellation Registry + Metrics       │   │
│  └──────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────┤
│                   MCP Server                        │
│   Tools  │  Resources  │  Prompts  │  Capabilities  │
├─────────────────────────────────────────────────────┤
│              Reliability Layer                       │
│   Circuit Breaker  │  Retry Policy  │  Timeouts     │
└─────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.16+
- [vcpkg](https://github.com/microsoft/vcpkg) for dependency management

### Dependencies

| Library | Purpose |
|---|---|
| `nlohmann-json` | JSON parsing and serialization |
| `cpp-httplib` | HTTP server/client |
| `spdlog` | Structured logging |
| `libcurl` | External API calls (weather tool) |
| `Google Test` | Unit testing |

### Build

```bash
# Install dependencies via vcpkg
vcpkg install curl nlohmann-json spdlog gtest cpp-httplib

# Configure and build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

With AddressSanitizer (debug builds):

```bash
cmake -B build -DENABLE_ASAN=ON \
  -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Run

```bash
# HTTP mode (default)
./build/src/mcp_server --mode http --port 8089

# stdio mode (for Claude Desktop integration)
./build/src/mcp_server --mode stdio

# Both modes simultaneously
./build/src/mcp_server --mode both --port 8089
```

### Configuration

Edit [config/server.json](config/server.json):

```jsonc
{
  "server": {
    "port": 8089,                // JSON-RPC HTTP port
    "sse_port": 8090,            // SSE streaming port (dedicated)
    "sse_workers": 2,            // SSE worker threads
    "worker_threads": 4,         // Default lane workers
    "max_pending_tasks": 128,    // Default lane queue capacity
    "tool_workers": 2,           // Tool lane workers
    "tool_max_pending_tasks": 32,
    "tool_circuit_failure_threshold": 5,  // Consecutive failures to open circuit
    "tool_circuit_open_ms": 30000,       // Circuit open duration (ms)
    "resource_workers": 2,
    "resource_max_pending_tasks": 32,
    "prompt_workers": 2,
    "prompt_max_pending_tasks": 32,
    "sse_max_clients": 16,
    "sse_replay_buffer_events": 256
  },
  "logging": {
    "log_level": "info",
    "log_console_output": true
  }
}
```

### Client Example

```cpp
#include "mcp_client.h"

using namespace mcp;

int main() {
    // Connect to MCP server
    McpClient client("localhost", 8089);

    // Initialize
    auto info = client.initialize();
    std::cout << "Server: " << info.server_info.name << "\n";

    // List tools
    auto tools = client.list_tools();

    // Call a tool
    auto result = client.call_tool("calculate", {
        {"operation", "add"},
        {"a", 123},
        {"b", 456}
    });
    // result.content[0].text => "579"

    // Read a resource
    auto sys_info = client.read_resource("system://info");

    // Stream SSE events
    // Connect to: http://localhost:8090/sse/events        (server status)
    // Connect to: http://localhost:8090/sse/tool_calls     (tool call events)
}
```

Full example: [examples/client_demo.cpp](examples/client_demo.cpp)

### Run Tests

```bash
cd build && ctest --output-on-failure
```

## Project Structure

```
├── config/              # JSON configuration files
├── examples/            # Client SDK usage examples
├── scripts/             # Utility scripts
├── src/
│   ├── config/          # Config loader with validation
│   ├── json_rpc/        # JSON-RPC runtime, task scheduling, SSE, transport
│   ├── logger/          # Logging abstraction (spdlog)
│   ├── mcp/             # MCP protocol: tools, resources, prompts, circuit breaker
│   ├── mcp_client/      # Client SDK
│   └── mcp_server_main.cpp  # Server entry point
├── tests/               # Unit tests (Google Test)
├── CMakeLists.txt
└── vcpkg.json
```

## License

This project is available under the MIT License. See [LICENSE](LICENSE) for details.
