#!/bin/bash

echo "========================================"
echo "🚀 MCP Server 一键测试脚本"
echo "========================================"
echo ""

MCP_SERVER_DIR="/Users/chef/Documents/team_project/mcp-tutorial/build/src"
AI_DIR="/Users/chef/Documents/team_project/mcp-tutorial/ai_integration"

# 检查 MCP Server 是否已运行
if lsof -Pi :8089 -sTCP:LISTEN -t >/dev/null; then
    echo "✅ MCP Server 已经在运行（端口 8089）"
else
    echo "🔄 正在启动 MCP Server..."
    cd "$MCP_SERVER_DIR"
    ./mcp_server --mode http --port 8089 > /dev/null 2>&1 &
    sleep 2

    if lsof -Pi :8089 -sTCP:LISTEN -t >/dev/null; then
        echo "✅ MCP Server 启动成功！"
    else
        echo "❌ MCP Server 启动失败"
        echo "请手动启动: cd $MCP_SERVER_DIR && ./mcp_server --mode http --port 8089"
        exit 1
    fi
fi

echo ""
echo "请选择测试方案:"
echo ""
echo "  1. 💻 Ollama 本地模型（推荐）"
echo "     - 完全免费，无需 API Key"
echo "     - 需要先安装: brew install ollama"
echo ""
echo "  2. 🌐 硅基流动 API"
echo "     - 注册送免费额度"
echo "     - 需要注册: https://cloud.siliconflow.cn/"
echo ""
echo "  3. 🔧 简单测试（直接调用工具，不用 AI）"
echo "     - 最快速，直接验证 MCP Server"
echo ""

read -p "请输入选择 (1/2/3，默认 3): " choice
choice=${choice:-3}

cd "$AI_DIR"

case $choice in
    1)
        echo ""
        echo "=== Ollama 本地模型测试 ==="
        echo ""

        # 检查 Ollama 是否安装
        if ! command -v ollama &> /dev/null; then
            echo "❌ Ollama 未安装"
            echo ""
            echo "请先安装 Ollama:"
            echo "  brew install ollama"
            echo ""
            exit 1
        fi

        # 检查 Ollama 服务是否运行
        if ! pgrep -x "ollama" > /dev/null; then
            echo "🔄 正在启动 Ollama 服务..."
            ollama serve > /dev/null 2>&1 &
            sleep 3
        fi

        # 检查模型是否下载
        if ! ollama list | grep -q "qwen2.5"; then
            echo "⚠️  未检测到 Qwen 模型"
            echo ""
            echo "请选择要下载的模型:"
            echo "  1. qwen2.5:1.5b (需要 ~2GB 内存，最快，推荐)"
            echo "  2. qwen2.5:3b   (需要 ~4GB 内存，较快)"
            echo "  3. qwen2.5:7b   (需要 ~8GB 内存，效果最好)"
            echo ""
            read -p "请输入选择 (1/2/3，默认 1): " model_choice
            model_choice=${model_choice:-1}

            case $model_choice in
                1) MODEL="qwen2.5:1.5b" ;;
                2) MODEL="qwen2.5:3b" ;;
                3) MODEL="qwen2.5:7b" ;;
                *) MODEL="qwen2.5:1.5b" ;;
            esac

            echo ""
            echo "📥 正在下载模型: $MODEL"
            echo "（这可能需要几分钟，请耐心等待...）"
            ollama pull "$MODEL"
        fi

        echo ""
        echo "🚀 启动 Ollama 测试..."
        python ollama_mcp_demo.py
        ;;

    2)
        echo ""
        echo "=== 硅基流动 API 测试 ==="
        echo ""
        echo "📝 如果还没有 API Key，请访问:"
        echo "   https://cloud.siliconflow.cn/"
        echo ""
        python siliconflow_mcp_demo.py
        ;;

    3)
        echo ""
        echo "=== 简单测试（直接调用工具）==="
        echo ""
        python ollama_mcp_demo.py <<< "2"
        ;;

    *)
        echo "❌ 无效的选择"
        exit 1
        ;;
esac

echo ""
echo "========================================"
echo "✅ 测试完成！"
echo "========================================"
