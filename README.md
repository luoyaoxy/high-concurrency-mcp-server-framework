# MCP Tutorial - 学习 Model Context Protocol 的完整项目

本项目是一个完整的 MCP (Model Context Protocol) 学习教程，包含了 C++ 实现的 MCP Client SDK、MCP Server 和实际的天气查询应用示例。

## 🎯 项目概述

### MCP 是什么？

MCP（Model Context Protocol，模型上下文协议）是一套开放标准，用来让大语言模型（LLM）或 AI 应用与外部工具/数据源进行"标准化通信"。

- 可以把它理解为"AI 与外部世界的 USB-C 接口"：只要双方都支持 MCP，就能即插即用，不再为每个平台单独适配接口
- MCP 使用 JSON-RPC 语义，支持多种传输方式：本地 STDIO 或网络 HTTP + SSE

### 解决的问题

**在 MCP 之前：**
- 每个 AI 平台（如 ChatGPT、Claude）都有自己的插件/工具机制与配置格式，开发者需要为不同平台写多份适配代码和 Schema
- 接口一旦变更，需要在各平台重复修改，维护成本高且容易出错

**有了 MCP 之后：**
- 开发者只需实现一次标准化的 MCP Server（包装你的工具/数据），AI 应用只需一次实现 MCP Client（在应用内部），二者通过统一协议沟通
- 能力发现、工具参数、资源读取等都走统一的 API（如 tools/list、tools/call、resources/read），无需各平台重复适配

### 核心概念

- **AI 应用（Host）**：用户实际使用的应用，如 Claude、Cursor、ChatGPT；内部集成 MCP Client
- **MCP Client**：在 Host 内部，负责按 MCP 协议与 MCP Server 通信
- **MCP Server**：对外提供标准化的工具（tools）与资源（resources）接口，里面封装了真实的 API 或数据源
- **工具（tool）**：MCP Server 暴露的一个可调用能力单元（带 JSON Schema 的参数说明）
- **资源（resource）**：可供读取的静态/半静态数据（例如文件、配置、数据库记录）
- **传输层**：STDIO（本地进程通信）或 HTTP + SSE（网络通信，支持流式推送）

## 🚀 快速开始

### 1. 系统要求

- **macOS** 或 **Ubuntu**
- CMake 和 Make（或 Ninja）
- C++17 编译器
- libcurl 开发库
- nlohmann/json 库

### 2. 安装依赖（一次性）

**macOS:**
```bash
brew install cmake curl nlohmann-json
```

**Ubuntu:**
```bash
sudo apt update
sudo apt install build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

### 3. 构建项目

```bash
# 构建项目
./build.sh
```

### 4. 运行示例

```bash
# 启动天气服务器（使用模拟数据）
cd build
./src/mcp_weather_server --api-key mock

# 在新终端运行客户端示例
./examples/weather_client --city Beijing

# 或运行交互式客户端
./examples/simple_client
```

## 📁 项目结构

```
mcp-tutorial/
├── src/
│   ├── include/mcp/           # MCP SDK 头文件
│   ├── lib/                   # 核心库实现
│   ├── mcp_client/           # MCP 客户端实现
│   └── mcp_server/           # MCP 服务器实现
├── examples/                 # 示例程序
├── tests/                   # 测试代码
├── build.sh                 # 简单构建脚本
└── QUICK_START.md           # 快速入门指南
```

## 🔧 功能特性

### MCP Client SDK
- 现代 C++ 实现（C++17）
- 同步和异步调用接口
- HTTP + SSE 传输支持
- 完善的错误处理和重连机制
- 线程安全的设计

### MCP Weather Server
- 完整的天气查询服务
- 支持 OpenWeatherMap API 和模拟数据
- 三个主要工具：
  - `get_current_weather`: 获取当前天气
  - `get_forecast`: 获取天气预报
  - `get_weather_by_coordinates`: 通过坐标获取天气
- HTTP API 和 MCP 协议双重支持

### 示例程序
- **weather_client**: 完整的天气查询客户端
- **simple_client**: 交互式 MCP 客户端
- 完善的测试套件

## 🛠️ 技术栈

- **语言**: C++17
- **构建系统**: CMake + Ninja/Make
- **网络库**: libcurl
- **JSON 库**: nlohmann/json
- **协议**: JSON-RPC 2.0 over HTTP
- **平台支持**: macOS, Ubuntu

## 📚 文档

- [QUICK_START.md](QUICK_START.md) - 快速入门指南

## 🎓 学习目标

通过本项目，你将学会：

1. **MCP 协议基础**: 理解 MCP 的工作原理和协议规范
2. **C++ 网络编程**: HTTP 客户端/服务器开发
3. **异步编程**: futures, promises, 和线程管理
4. **JSON-RPC**: 远程过程调用协议的实现
5. **现代 C++**: C++17 特性的实际应用
6. **跨平台开发**: macOS 和 Linux 兼容性
7. **API 集成**: 第三方 API（天气服务）集成

## 🤝 工作流程示例

基于你提供的时序图，本项目实现了完整的天气查询流程：

```
用户 -> AI工具(MCP客户端) -> MCP服务器 -> 天气API提供方
                ↓
    "今天北京天气如何？"
                ↓
         解析请求，调用天气工具
                ↓
         发送MCP标准化请求
                ↓
              调用天气API
                ↓
         返回处理后的天气数据
                ↓
    "北京今天晴，气温25℃，适合户外活动"
```

## 🎯 本仓库的价值

- **学习 MCP**: 通过完整实现理解 MCP 协议
- **实践 C++**: 现代 C++ 在网络编程中的应用
- **可扩展架构**: 易于添加新工具和 API 提供商
- **生产就绪**: 包含错误处理、测试和文档

让 C++ 程序通过标准 MCP 协议访问任意外部服务，轻松集成到 AI 工作流中！