#!/usr/bin/env python3
"""
使用 Ollama（本地免费大模型）+ MCP Server 完整示例
无需 API Key，完全本地运行！
"""

import json
import requests

# ==========================================
# MCP Client
# ==========================================
class McpClient:
    def __init__(self, host="localhost", port=8089):
        self.base_url = f"http://{host}:{port}/jsonrpc"
        self.request_id = 0

    def _send_request(self, method, params=None):
        self.request_id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id
        }
        response = requests.post(self.base_url, json=payload)
        response.raise_for_status()
        result = response.json()
        if "error" in result:
            raise Exception(f"MCP Error: {result['error']}")
        return result.get("result")

    def list_tools(self):
        result = self._send_request("tools/list")
        return result.get("tools", [])

    def call_tool(self, name, arguments):
        return self._send_request("tools/call", {
            "name": name,
            "arguments": arguments
        })

    def get_prompt(self, name, arguments):
        result = self._send_request("prompts/get", {
            "name": name,
            "arguments": arguments
        })
        return result.get("messages", [])


# ==========================================
# Ollama Client（本地大模型）
# ==========================================
class OllamaClient:
    def __init__(self, model="qwen2.5:1.5b", base_url="http://localhost:11434"):
        self.model = model
        self.base_url = base_url

    def chat(self, messages, tools=None):
        """调用 Ollama 聊天接口"""
        payload = {
            "model": self.model,
            "messages": messages,
            "stream": False
        }

        # Ollama 支持工具调用（Function Calling）
        if tools:
            payload["tools"] = tools

        response = requests.post(
            f"{self.base_url}/api/chat",
            json=payload
        )
        response.raise_for_status()
        return response.json()


# ==========================================
# 主要演示函数
# ==========================================
def main():
    print("=" * 70)
    print("🎉 Ollama (本地大模型) + MCP Server 集成示例")
    print("=" * 70)
    print()

    # 初始化客户端
    mcp = McpClient(host="localhost", port=8089)
    ollama = OllamaClient(model="qwen2.5:1.5b")  # 使用 qwen2.5:1.5b 小模型

    # 获取 MCP 工具列表
    print("📋 正在获取 MCP Server 的工具列表...")
    tools = mcp.list_tools()
    print(f"✅ 找到 {len(tools)} 个工具:")
    for tool in tools:
        print(f"   - {tool['name']}: {tool['description']}")
    print()

    # 转换为 Ollama 的工具格式
    ollama_tools = []
    for tool in tools:
        ollama_tool = {
            "type": "function",
            "function": {
                "name": tool["name"],
                "description": tool["description"],
                "parameters": {
                    "type": "object",
                    "properties": tool["inputSchema"]["properties"],
                    "required": tool["inputSchema"].get("required", [])
                }
            }
        }
        ollama_tools.append(ollama_tool)

    # 用户输入
    user_message = "请帮我查一下北京的天气"

    print(f"👤 用户: {user_message}\n")
    print("🤖 Ollama 正在思考...\n")

    messages = [
        {"role": "user", "content": user_message}
    ]

    try:
        # 第一轮：让 AI 决定调用哪个工具
        response = ollama.chat(messages, tools=ollama_tools)

        message = response.get("message", {})

        # 检查是否调用工具
        if "tool_calls" in message and message["tool_calls"]:
            tool_call = message["tool_calls"][0]
            function_name = tool_call["function"]["name"]
            function_args = tool_call["function"]["arguments"]

            print(f"🔧 AI 决定调用工具: {function_name}")
            print(f"   参数: {json.dumps(function_args, ensure_ascii=False)}\n")

            # 调用 MCP Server 的工具
            print("📡 正在调用 MCP Server...")
            tool_result = mcp.call_tool(function_name, function_args)
            tool_output = tool_result["content"][0].get("text", "")

            print(f"✅ 工具返回:\n{tool_output}\n")

            # 将工具结果返回给 AI
            messages.append(message)
            messages.append({
                "role": "tool",
                "content": tool_output
            })

            # 第二轮：生成最终回复
            print("🤖 Ollama 正在生成回复...\n")
            final_response = ollama.chat(messages)
            final_content = final_response.get("message", {}).get("content", "")

            print(f"💬 AI 回复:\n{final_content}\n")
        else:
            # AI 直接回复，没有调用工具
            content = message.get("content", "")
            print(f"💬 AI 回复:\n{content}\n")

    except Exception as e:
        print(f"❌ 错误: {e}")
        print("\n请检查:")
        print("  1. Ollama 是否正在运行: ollama serve")
        print("  2. 模型是否已下载: ollama pull qwen2.5:7b")
        print("  3. MCP Server 是否正在运行")
        return

    print("=" * 70)
    print("✅ 测试完成！")
    print("=" * 70)


# ==========================================
# 简化版示例：直接调用工具（不用 AI）
# ==========================================
def simple_test():
    """简单测试：直接调用 MCP 工具，不使用 AI"""
    print("=" * 70)
    print("🔧 简单测试：直接调用 MCP 工具")
    print("=" * 70)
    print()

    mcp = McpClient(host="localhost", port=8089)

    print("1️⃣ 测试 get_weather 工具")
    result = mcp.call_tool("get_weather", {"city": "Beijing"})
    print(result["content"][0]["text"])
    print()

    print("2️⃣ 测试 calculate 工具")
    result = mcp.call_tool("calculate", {"operation": "add", "a": 123, "b": 456})
    print(f"123 + 456 = {result['content'][0]['text']}")
    print()

    print("3️⃣ 测试 echo 工具")
    result = mcp.call_tool("echo", {"message": "Hello from Python!"})
    print(result["content"][0]["text"])
    print()


if __name__ == "__main__":
    print("\n请选择运行模式:")
    print("  1. 完整演示（使用 Ollama AI + MCP Server）")
    print("  2. 简单测试（直接调用 MCP 工具，不使用 AI）\n")

    choice = input("请输入选择 (1 或 2，默认 2): ").strip() or "2"

    if choice == "1":
        print("\n⚠️  确保 Ollama 已启动并下载模型:")
        print("   ollama serve")
        print("   ollama pull qwen2.5:1.5b")
        print()
        input("按回车继续...")
        main()
    else:
        simple_test()
