#!/usr/bin/env python3

import requests
import sys

def test_sse(endpoint="/sse/events", port=8089):
    url = f"http://localhost:{port}{endpoint}"

    print(f"连接到 {url}...")
    print("等待 SSE 消息 (Ctrl+C 退出)\n")

    try:
        response = requests.get(url, stream=True, timeout=None)
        response.raise_for_status()

        for line in response.iter_lines():
            if line:
                decoded = line.decode('utf-8')
                if decoded.startswith('data: '):
                    print(decoded[6:])

    except KeyboardInterrupt:
        print("\n\n中断连接")
    except Exception as e:
        print(f"错误: {e}")

if __name__ == "__main__":
    endpoint = sys.argv[1] if len(sys.argv) > 1 else "/sse/events"

    print("可用端点:")
    print("  /sse/events      - 服务器状态流")
    print("  /sse/tool_calls  - 工具调用监控流\n")

    test_sse(endpoint)
