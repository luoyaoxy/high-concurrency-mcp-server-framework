#!/bin/bash

echo "========================================"
echo "🚀 启动 MCP Server + Ollama 演示环境"
echo "========================================"
echo ""

MCP_SERVER_DIR="/Users/chef/Documents/team_project/mcp-tutorial/build/src"

# 1. 启动 MCP Server
echo "📡 检查 MCP Server..."
if lsof -Pi :8089 -sTCP:LISTEN -t >/dev/null; then
    echo "✅ MCP Server 已在运行（端口 8089）"
else
    echo "🔄 正在启动 MCP Server..."
    cd "$MCP_SERVER_DIR"
    ./mcp_server --mode http --port 8089 > /tmp/mcp_server.log 2>&1 &
    MCP_PID=$!
    sleep 2

    if lsof -Pi :8089 -sTCP:LISTEN -t >/dev/null; then
        echo "✅ MCP Server 启动成功！（PID: $MCP_PID）"
        echo "   日志文件: /tmp/mcp_server.log"
    else
        echo "❌ MCP Server 启动失败"
        echo "   查看日志: cat /tmp/mcp_server.log"
        exit 1
    fi
fi

echo ""

# 2. 启动 Ollama 服务
echo "🤖 检查 Ollama 服务..."
if pgrep -x "ollama" > /dev/null; then
    echo "✅ Ollama 服务已在运行"
else
    echo "🔄 正在启动 Ollama 服务..."
    ollama serve > /tmp/ollama.log 2>&1 &
    OLLAMA_PID=$!
    sleep 3

    if pgrep -x "ollama" > /dev/null; then
        echo "✅ Ollama 服务启动成功！（PID: $OLLAMA_PID）"
        echo "   日志文件: /tmp/ollama.log"
    else
        echo "❌ Ollama 服务启动失败"
        echo "   请确保已安装 Ollama: brew install ollama"
        echo "   查看日志: cat /tmp/ollama.log"
        exit 1
    fi
fi

echo ""

# 3. 检查模型
echo "📦 检查 Ollama 模型..."
if ollama list | grep -q "qwen2.5:1.5b"; then
    echo "✅ qwen2.5:1.5b 模型已下载"
elif ollama list | grep -q "qwen2.5"; then
    echo "✅ 检测到其他 Qwen 模型"
    ollama list | grep "qwen2.5"
else
    echo "⚠️  未检测到 Qwen 模型"
    echo ""
    echo "正在下载 qwen2.5:1.5b (约 1GB)..."
    ollama pull qwen2.5:1.5b

    if [ $? -eq 0 ]; then
        echo "✅ 模型下载完成！"
    else
        echo "❌ 模型下载失败"
        exit 1
    fi
fi

echo ""
echo "========================================"
echo "✅ 所有服务已就绪！"
echo "========================================"
echo ""
echo "📊 服务状态:"
echo "  - MCP Server:  http://localhost:8089"
echo "  - Ollama:      http://localhost:11434"
echo ""
echo "🎯 现在可以运行测试了:"
echo "  cd /Users/chef/Documents/team_project/mcp-tutorial/ai_integration"
echo "  python ollama_mcp_demo.py"
echo ""
echo "🛑 停止所有服务:"
echo "  pkill -f mcp_server"
echo "  pkill -f ollama"
echo ""
