# Examples

# 查找 httplib (用于 HTTP 客户端)
find_package(httplib CONFIG REQUIRED)

# Configuration and Logging Demo
add_executable(config_demo
    config_demo.cpp
)

target_link_libraries(config_demo
    mcp_lib
)

# Simple JSON-RPC Client (stdio)
add_executable(simple_client
    simple_client.cpp
)

target_link_libraries(simple_client
    PRIVATE
    nlohmann_json::nlohmann_json
)

# HTTP JSON-RPC Client
add_executable(http_client
    http_client.cpp
)

target_link_libraries(http_client
    PRIVATE
    nlohmann_json::nlohmann_json
    httplib::httplib
)

# MCP Demo Server - 展示 Tools/Resources/Prompts
add_executable(mcp_demo_server
    mcp_demo_server.cpp
)

target_link_libraries(mcp_demo_server
    mcp_lib
)

# ===================================================================
# 🔧 扩展位置：在这里添加更多示例程序
# ===================================================================
# 示例：
# add_executable(another_example
#     another_example.cpp
# )
# target_link_libraries(another_example mcp_lib)
# ===================================================================
