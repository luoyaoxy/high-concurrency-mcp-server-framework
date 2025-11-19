#!/usr/bin/env python3
"""
使用 Ollama（本地免费大模型）+ MCP Server 完整示例
无需 API Key，完全本地运行！
"""

import json
import requests
import sys
import threading

# ==========================================
# MCP Client
# ==========================================
class McpClient:
    def __init__(self, host="localhost", port=8089):
        self.base_url = f"http://{host}:{port}"
        self.request_id = 0
        self.sse_thread = None
        self.sse_running = False

    def start_sse_monitor(self):
        """启动 SSE 监控线程"""
        def monitor():
            url = f"{self.base_url}/sse/tool_calls"
            try:
                response = requests.get(url, stream=True, timeout=None)
                for line in response.iter_lines(decode_unicode=True):
                    if not self.sse_running:
                        break
                    if line and line.startswith('data: '):
                        try:
                            data = json.loads(line[6:])
                            event_type = data.get("type", "unknown")
                            if event_type == "tool_call_start":
                                print(f"\n🔧 [SSE] 调用工具: {data['tool']}", flush=True)
                            elif event_type == "tool_call_end":
                                status = "✅ 成功" if data['success'] else "❌ 失败"
                                print(f"🔧 [SSE] 工具完成: {data['tool']} - {status}", flush=True)
                            elif event_type == "tool_call_error":
                                print(f"🔧 [SSE] 工具错误: {data['tool']} - {data['error']}", flush=True)
                        except Exception as e:
                            print(f"[SSE Parse Error] {e}", flush=True)
            except Exception as e:
                if self.sse_running:
                    print(f"[SSE Connection Error] {e}", flush=True)

        self.sse_running = True
        self.sse_thread = threading.Thread(target=monitor, daemon=True)
        self.sse_thread.start()

    def stop_sse_monitor(self):
        """停止 SSE 监控"""
        self.sse_running = False
        if self.sse_thread:
            self.sse_thread.join(timeout=1)

    def _send_request(self, method, params=None):
        self.request_id += 1
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id
        }
        response = requests.post(f"{self.base_url}/jsonrpc", json=payload)
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
def interactive_mode():

    # 初始化客户端
    mcp = McpClient(host="localhost", port=8089)
    ollama = OllamaClient(model="qwen2.5:1.5b")

    # 启动 SSE 监控
    mcp.start_sse_monitor()

    # 获取 MCP 工具列表
    try:
        tools = mcp.list_tools()
    except Exception as e:
        mcp.stop_sse_monitor()
        print("请确保 MCP Server 正在运行\n")
        return

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

    # 对话历史
    messages = []

    while True:
        try:
            # 获取用户输入
            user_input = input("👤 你: ").strip()

            if not user_input:
                continue

            # 处理特殊命令
            if user_input.lower() in ['quit', 'exit', 'q']:
                mcp.stop_sse_monitor()
                print("\n👋 再见！")
                break

            if user_input.lower() == 'tools':
                print("\n📋 可用工具列表:")
                for tool in tools:
                    print(f"   - {tool['name']}: {tool['description']}")
                    if 'inputSchema' in tool and 'properties' in tool['inputSchema']:
                        print(f"     参数: {list(tool['inputSchema']['properties'].keys())}")
                print()
                continue

            # 添加用户消息
            messages.append({"role": "user", "content": user_input})

            # 调用 AI
            response = ollama.chat(messages, tools=ollama_tools)
            message = response.get("message", {})

            # 检查是否调用工具
            if "tool_calls" in message and message["tool_calls"]:
                tool_call = message["tool_calls"][0]
                function_name = tool_call["function"]["name"]
                function_args = tool_call["function"]["arguments"]
                # 调用 MCP Server 的工具
                try:
                    tool_result = mcp.call_tool(function_name, function_args)
                    tool_output = tool_result["content"][0].get("text", "")

                    # 将工具结果返回给 AI
                    messages.append(message)
                    messages.append({
                        "role": "tool",
                        "content": tool_output
                    })

                    # 生成最终回复
                    final_response = ollama.chat(messages)
                    final_content = final_response.get("message", {}).get("content", "")

                    # 添加 AI 回复到历史
                    messages.append({"role": "assistant", "content": final_content})

                    print(f"💬 回复: {final_content}\n")

                except Exception as e:
                    print(f"❌ 工具调用失败: {e}\n")
                    # 移除最后的用户消息，因为这轮对话失败了
                    messages.pop()
            else:
                # AI 直接回复，没有调用工具
                content = message.get("content", "")
                messages.append({"role": "assistant", "content": content})
                print(f"\n💬 回复: {content}\n")

        except KeyboardInterrupt:
            print("\n\n👋 收到中断信号，退出...")
            break
        except Exception as e:
            print(f"❌ 错误: {e}\n")
            # 移除最后的用户消息
            if messages and messages[-1]["role"] == "user":
                messages.pop()


# ==========================================
# 简化版示例：直接调用工具（不用 AI）
# ==========================================
def simple_test():
   

    mcp = McpClient(host="localhost", port=8089)

    result = mcp.call_tool("get_weather", {"city": "Beijing"})
  
    result = mcp.call_tool("calculate", {"operation": "add", "a": 123, "b": 456})

    result = mcp.call_tool("echo", {"message": "Hello from Python!"})
    

if __name__ == "__main__":
    print("\n请选择运行模式:")
    print("  1. 交互模式（AI 对话，可调用多个工具）")
    print("  2. 简单测试（直接测试 MCP 工具，不使用 AI）")
    print("  3. 命令行模式（直接从参数运行）\n")

    # 支持命令行参数
    if len(sys.argv) > 1:
        choice = "3"
        user_query = " ".join(sys.argv[1:])
    else:
        choice = input("请输入选择 (1/2/3，默认 1): ").strip() or "1"

    if choice == "1":
        print("\n⚠️  确保服务已启动:")
        print("   1. Ollama: ollama serve")
        print("   2. 模型: ollama pull qwen2.5:1.5b")
        print("   3. MCP Server 运行中")
        print()
        input("按回车继续...")
        interactive_mode()
    elif choice == "2":
        simple_test()
    elif choice == "3":
        # 命令行模式：直接处理一个查询
        mcp = McpClient(host="localhost", port=8089)
        ollama = OllamaClient(model="qwen2.5:1.5b")

        try:
            tools = mcp.list_tools()

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

            messages = [{"role": "user", "content": user_query}]
            response = ollama.chat(messages, tools=ollama_tools)
            message = response.get("message", {})

            if "tool_calls" in message and message["tool_calls"]:
                tool_call = message["tool_calls"][0]
                function_name = tool_call["function"]["name"]
                function_args = tool_call["function"]["arguments"]

                print(f"\n🔧 调用工具: {function_name}")
                print(f"   参数: {json.dumps(function_args, ensure_ascii=False)}")

                tool_result = mcp.call_tool(function_name, function_args)
                tool_output = tool_result["content"][0].get("text", "")

                messages.append(message)
                messages.append({"role": "tool", "content": tool_output})

                final_response = ollama.chat(messages)
                final_content = final_response.get("message", {}).get("content", "")
                print(f"\n💬 回复: {final_content}\n")
            else:
                content = message.get("content", "")
                print(f"\n💬 回复: {content}\n")

        except Exception as e:
            print(f"❌ 错误: {e}")
            sys.exit(1)
    else:
        print("❌ 无效选择")
        sys.exit(1)
