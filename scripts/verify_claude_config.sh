#!/bin/bash

echo "=================================="
echo "Claude Desktop MCP 配置验证脚本"
echo "=================================="
echo ""

CONFIG_FILE="$HOME/Library/Application Support/Claude/claude_desktop_config.json"
MCP_SERVER="/Users/chef/Documents/team_project/mcp-tutorial/build/src/mcp_server"

# 1. 检查配置文件是否存在
echo "1. 检查配置文件..."
if [ -f "$CONFIG_FILE" ]; then
    echo "   ✅ 配置文件存在: $CONFIG_FILE"
else
    echo "   ❌ 配置文件不存在: $CONFIG_FILE"
    exit 1
fi

# 2. 验证 JSON 格式
echo ""
echo "2. 验证 JSON 格式..."
if python3 -m json.tool "$CONFIG_FILE" > /dev/null 2>&1; then
    echo "   ✅ JSON 格式正确"
else
    echo "   ❌ JSON 格式错误"
    python3 -m json.tool "$CONFIG_FILE"
    exit 1
fi

# 3. 检查 mcp_server 是否存在
echo ""
echo "3. 检查 mcp_server 可执行文件..."
if [ -f "$MCP_SERVER" ]; then
    echo "   ✅ mcp_server 存在: $MCP_SERVER"
else
    echo "   ❌ mcp_server 不存在: $MCP_SERVER"
    exit 1
fi

# 4. 检查执行权限
echo ""
echo "4. 检查执行权限..."
if [ -x "$MCP_SERVER" ]; then
    echo "   ✅ mcp_server 有执行权限"
else
    echo "   ⚠️  mcp_server 没有执行权限，尝试添加..."
    chmod +x "$MCP_SERVER"
    if [ -x "$MCP_SERVER" ]; then
        echo "   ✅ 执行权限已添加"
    else
        echo "   ❌ 无法添加执行权限"
        exit 1
    fi
fi

# 5. 测试 mcp_server 是否能启动
echo ""
echo "5. 测试 mcp_server 启动（stdio 模式）..."
timeout 2 "$MCP_SERVER" --mode stdio < /dev/null > /dev/null 2>&1
EXIT_CODE=$?
if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 0 ]; then
    echo "   ✅ mcp_server 可以正常启动"
else
    echo "   ❌ mcp_server 启动失败，退出码: $EXIT_CODE"
    echo ""
    echo "   尝试手动运行查看错误："
    echo "   $MCP_SERVER --mode stdio"
    exit 1
fi

# 6. 检查 Claude Desktop 是否正在运行
echo ""
echo "6. 检查 Claude Desktop 进程..."
if pgrep -x "Claude" > /dev/null; then
    echo "   ⚠️  Claude Desktop 正在运行"
    echo "   请重启 Claude Desktop 以加载新配置："
    echo ""
    echo "   方法 1 (推荐): 使用 Command+Q 退出，然后重新打开"
    echo "   方法 2 (命令): killall Claude && sleep 2 && open -a Claude"
else
    echo "   ✅ Claude Desktop 未运行，可以直接启动"
    echo ""
    echo "   启动命令: open -a Claude"
fi

echo ""
echo "=================================="
echo "✅ 所有检查通过！"
echo "=================================="
echo ""
echo "配置内容："
cat "$CONFIG_FILE"
echo ""
echo "下一步："
echo "1. 如果 Claude Desktop 正在运行，请重启它"
echo "2. 在 Claude Desktop 中测试: '查一下北京的天气'"
