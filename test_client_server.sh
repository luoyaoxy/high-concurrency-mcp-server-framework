#!/bin/bash
# 测试 MCP 服务端和客户端通信

cd "$(dirname "$0")/build"

echo "=== 启动 MCP 服务端和客户端通信测试 ==="
echo ""
echo "客户端将发送 3 个请求："
echo "  1. initialize 请求"
echo "  2. echo 请求"
echo "  3. 不存在的方法（测试错误处理）"
echo ""
echo "=========================================="
echo ""

# 通过管道连接客户端和服务端
./examples/simple_client | ./src/mcp_server

echo ""
echo "=========================================="
echo "=== 测试完成 ==="
