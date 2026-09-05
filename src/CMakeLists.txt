# MCP Library - 基础库（包含配置管理和日志系统）
add_library(mcp_lib STATIC
    config/config.cpp
    logger/logger.cpp
    json_rpc/stdio_jsonrpc.cpp
    json_rpc/jsonrpc_serialization.cpp
    json_rpc/http_jsonrpc.cpp
    # 独立 SSE HTTP 服务，使用专属 httplib worker pool。
    json_rpc/http_sse_server.cpp

    json_rpc/jsonrpc_task.cpp
    # 请求执行上下文：提供取消与 deadline 查询接口。
    json_rpc/jsonrpc_request_context.cpp
    # 按 JSON-RPC request id 管理取消令牌。
    json_rpc/jsonrpc_cancellation_registry.cpp
    # 原子调度指标与结构化快照。
    json_rpc/jsonrpc_metrics.cpp
    json_rpc/jsonrpc_task_queue.cpp
    json_rpc/jsonrpc_worker_pool.cpp
    json_rpc/jsonrpc_task_runtime.cpp

    # SSE 订阅者的独立、有界事件队列。
    json_rpc/sse_event_hub.cpp

    mcp/types.cpp
    mcp/mcp_server.cpp
    # Open-Meteo 城市解析与真实天气查询。
    mcp/weather_client.cpp
    mcp/tool_circuit_breaker.cpp

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
    CURL::libcurl
    OpenSSL::Crypto
)

# Link JSON library (using vcpkg)
target_link_libraries(mcp_lib nlohmann_json::nlohmann_json)

# Link spdlog library (using vcpkg)
target_link_libraries(mcp_lib spdlog::spdlog)

# Link cpp-httplib library (using vcpkg)
find_package(httplib CONFIG REQUIRED)
target_link_libraries(mcp_lib httplib::httplib)

# ===================================================================
# MCP Server - 统一服务器（支持 HTTP/stdio/both 三种模式）
# ===================================================================
add_executable(mcp_server
    mcp_server_main.cpp
)

target_link_libraries(mcp_server
    mcp_lib
)

# ===================================================================
# MCP Client SDK - 客户端库
# ===================================================================
add_library(mcp_client_sdk STATIC
    mcp_client/mcp_client.cpp
)

target_include_directories(mcp_client_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp_client
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp
)

target_link_libraries(mcp_client_sdk
    mcp_lib
    httplib::httplib
)

# ===================================================================
# 🔧 扩展位置：在这里添加更多库或可执行文件
# ===================================================================
