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
# ===================================================================
