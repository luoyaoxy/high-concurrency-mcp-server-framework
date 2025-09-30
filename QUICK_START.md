# MCP Tutorial 快速入门

## 🎯 学习目标

通过这个项目学习 MCP (Model Context Protocol) 协议和 C++ 网络编程。

## 📋 准备工作

确保你的系统已安装：
- CMake
- C++ 编译器
- libcurl 开发库
- nlohmann/json 库

## 🚀 三步走

### 1. 构建项目
```bash
./build.sh
```

### 2. 启动服务器
```bash
cd build
./src/mcp_weather_server --api-key mock
```

### 3. 测试客户端
```bash
# 新开终端，运行客户端
cd build
./examples/weather_client --city Shanghai

# 或者运行交互式客户端
./examples/simple_client
```

## 🎮 交互式客户端命令

在 `simple_client` 中可以使用：
- `help` - 显示帮助
- `weather Beijing` - 获取北京天气
- `forecast Shanghai 3` - 获取上海3天预报
- `coords 39.9 116.4` - 通过坐标获取天气
- `quit` - 退出

## 🔧 如果构建失败

**macOS:**
```bash
brew install cmake curl nlohmann-json
```

**Ubuntu:**
```bash
sudo apt install build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

然后重新运行 `./build.sh`

## 🧪 运行测试

```bash
cd build
./tests/test_json_rpc
./tests/test_mcp_client
```

## 📚 学习建议

1. 先运行示例，看看效果
2. 阅读 `src/include/mcp/` 下的头文件了解接口
3. 查看 `examples/` 下的客户端代码学习用法
4. 研究 `src/mcp_server/` 下的服务器实现

就这么简单！开始你的 MCP 学习之旅吧！ 🎉