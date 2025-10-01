# MCP Library - 基础库（包含配置管理和日志系统）
add_library(mcp_lib STATIC
    lib/config.cpp
    logger/logger.cpp
)

target_include_directories(mcp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mcp_lib
    Threads::Threads
)

# Link JSON library (using vcpkg)
target_link_libraries(mcp_lib nlohmann_json::nlohmann_json)

# Link spdlog library (using vcpkg)
target_link_libraries(mcp_lib spdlog::spdlog)

# ===================================================================
# Main executable - MCP Server
# ===================================================================
add_executable(mcp_server
    main.cpp
)

target_link_libraries(mcp_server
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
