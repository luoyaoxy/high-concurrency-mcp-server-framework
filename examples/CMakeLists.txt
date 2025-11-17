# ===================================================================
# MCP 客户端 SDK 使用示例
# ===================================================================

# Client Demo - 完整的 MCP 客户端 SDK 示例
# 演示如何使用 McpClient 调用 MCP 服务器的所有功能
add_executable(client_demo
    client_demo.cpp
)

target_link_libraries(client_demo
    mcp_client_sdk
)

# ===================================================================
# 🔧 扩展位置：在这里添加更多示例程序
# ===================================================================
# 示例：
# add_executable(another_example
#     another_example.cpp
# )
# target_link_libraries(another_example mcp_client_sdk)
# ===================================================================
