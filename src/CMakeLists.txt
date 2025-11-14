# MCP Library - 基础库（包含配置管理和日志系统）
add_library(mcp_lib STATIC
    config/config.cpp
    logger/logger.cpp
    json_rpc/stdio_jsonrpc.cpp
    json_rpc/jsonrpc_serialization.cpp
    json_rpc/http_jsonrpc.cpp
    mcp/types.cpp
    mcp/mcp_server.cpp
)

# 设置 include 路径，这样就可以直接 #include "config.h" 而不需要写相对路径
target_include_directories(mcp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/logger
    ${CMAKE_CURRENT_SOURCE_DIR}/json_rpc
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp
)

target_link_libraries(mcp_lib
    Threads::Threads
)

# Link JSON library (using vcpkg)
target_link_libraries(mcp_lib nlohmann_json::nlohmann_json)

# Link spdlog library (using vcpkg)
target_link_libraries(mcp_lib spdlog::spdlog)

# Link cpp-httplib library (using vcpkg)
find_package(httplib CONFIG REQUIRED)
target_link_libraries(mcp_lib httplib::httplib)

# ===================================================================
# Main executable - MCP Server (stdio)
# ===================================================================
add_executable(mcp_server
    main.cpp
)

target_link_libraries(mcp_server
    mcp_lib
)

# ===================================================================
# HTTP Server - MCP HTTP JSON-RPC Server
# ===================================================================
add_executable(mcp_http_server
    http_server_main.cpp
)

target_link_libraries(mcp_http_server
    mcp_lib
)

# ===================================================================
# Full MCP HTTP Server - 完整的 MCP 服务器（支持 Tools/Resources/Prompts）
# ===================================================================
add_executable(mcp_full_http_server
    mcp_full_http_server_main.cpp
)

target_link_libraries(mcp_full_http_server
    mcp_lib
)

# ===================================================================
# 🔧 扩展位置：在这里添加新的库或可执行文件
# ===================================================================
# 示例：
# # 添加 MCP Client SDK
# add_library(mcp_client STATIC
#     mcp_client/client.cpp
# )
# target_link_libraries(mcp_client mcp_lib)
#
# # 添加其他服务器
# add_executable(mcp_weather_server
#     mcp_server/main.cpp
#     mcp_server/weather_server.cpp
# )
# target_link_libraries(mcp_weather_server mcp_lib)
# ===================================================================
