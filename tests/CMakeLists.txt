# Google Test framework

# Logger test
add_executable(test_logger
    test_logger.cpp
    ../src/logger/logger.cpp
)

target_include_directories(test_logger PRIVATE
    ../src/logger
)

target_link_libraries(test_logger
    spdlog::spdlog
    GTest::gtest
)

# Config test
add_executable(test_config
    test_config.cpp
    ../src/config/config.cpp
)

target_include_directories(test_config PRIVATE
    ../src/config
)

target_link_libraries(test_config
    nlohmann_json::nlohmann_json
    GTest::gtest
)

# Add tests to CTest
add_test(NAME LoggerTest COMMAND test_logger)
add_test(NAME ConfigTest COMMAND test_config)

# Enable Google Test discovery
include(GoogleTest)
gtest_discover_tests(test_logger)
gtest_discover_tests(test_config)

# ===================================================================
# 🔧 扩展位置：在这里添加新的测试用例
# ===================================================================
# JSON RPC test
add_executable(test_json_rpc
    stdio_jsonrpc_test.cpp
)
target_link_libraries(test_json_rpc
    mcp_lib
    GTest::gtest_main
)
add_test(NAME JsonRpcTest COMMAND test_json_rpc)
gtest_discover_tests(test_json_rpc)

# MCP Server test
add_executable(test_mcp_server
    test_mcp_server.cpp
)
target_link_libraries(test_mcp_server
    mcp_lib
    GTest::gtest_main
)
add_test(NAME McpServerTest COMMAND test_mcp_server)
gtest_discover_tests(test_mcp_server)

add_executable(test_jsonrpc_task_queue
    jsonrpc_task_queue_test.cpp
)

target_link_libraries(test_jsonrpc_task_queue
    mcp_lib
    GTest::gtest_main
)

add_test(NAME JsonRpcTaskQueueTest COMMAND test_jsonrpc_task_queue)
gtest_discover_tests(test_jsonrpc_task_queue)

add_executable(test_jsonrpc_task_runtime
    jsonrpc_task_runtime_test.cpp
)

target_link_libraries(test_jsonrpc_task_runtime
    mcp_lib
    GTest::gtest_main
)


gtest_discover_tests(test_jsonrpc_task_runtime)
# ===================================================================

# JSON-RPC 调度指标快照与并发采集测试。
add_executable(test_jsonrpc_metrics
    jsonrpc_metrics_test.cpp
)

target_link_libraries(test_jsonrpc_metrics
    mcp_lib
    GTest::gtest_main
)

gtest_discover_tests(test_jsonrpc_metrics)


# SSE 客户端隔离与背压测试。
add_executable(test_sse_event_hub
    sse_event_hub_test.cpp
)

target_link_libraries(test_sse_event_hub
    mcp_lib
    GTest::gtest_main
)

# 将 TEST(...) 自动注册为独立 CTest 项。
gtest_discover_tests(test_sse_event_hub)

# 按工具隔离故障的熔断器测试。
add_executable(test_tool_circuit_breaker
    tool_circuit_breaker_test.cpp
)

target_link_libraries(test_tool_circuit_breaker
    mcp_lib
    GTest::gtest_main
)

gtest_discover_tests(test_tool_circuit_breaker)

# HTTP batch 的并发 Map/Gather 行为测试。
add_executable(test_http_jsonrpc
    http_jsonrpc_test.cpp
)

target_link_libraries(test_http_jsonrpc
    mcp_lib
    GTest::gtest_main
)

gtest_discover_tests(test_http_jsonrpc)

# 幂等工具的自动重试、取消与 deadline 测试。
add_executable(test_tool_retry
    tool_retry_test.cpp
)

target_link_libraries(test_tool_retry
    mcp_lib
    GTest::gtest_main
)

gtest_discover_tests(test_tool_retry)

# 真实天气客户端使用 mock HTTP 响应，不依赖外部天气服务。
add_executable(test_weather_client
    weather_client_test.cpp
)

target_link_libraries(test_weather_client
    mcp_lib
    GTest::gtest_main
)

gtest_discover_tests(test_weather_client)
