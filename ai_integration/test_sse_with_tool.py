#!/usr/bin/env python3

import requests
import json
import threading
import time

def sse_monitor():
    """SSE 监控线程"""
    url = "http://localhost:8089/sse/tool_calls"
    print("SSE: 连接中...")

    try:
        response = requests.get(url, stream=True, timeout=None)
        for line in response.iter_lines(decode_unicode=True):
            if line and line.startswith('data: '):
                data = json.loads(line[6:])
                print(f"[SSE] {data}")
    except Exception as e:
        print(f"[SSE Error] {e}")

def call_tool():
    """调用工具"""
    time.sleep(2)  # 等待 SSE 连接

    payload = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {
            "name": "get_weather",
            "arguments": {"city": "Beijing"}
        },
        "id": 1
    }

    print("\n调用工具: get_weather")
    response = requests.post("http://localhost:8089/jsonrpc", json=payload)
    result = response.json()
    print(f"工具结果: {result['result']['content'][0]['text']}\n")

if __name__ == "__main__":
    # 启动 SSE 监控线程
    sse_thread = threading.Thread(target=sse_monitor, daemon=True)
    sse_thread.start()

    # 调用工具
    call_tool()

    # 等待
    time.sleep(2)
