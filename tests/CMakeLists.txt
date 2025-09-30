# Google Test framework
add_executable(test_json_rpc
    test_json_rpc.cpp
)

target_link_libraries(test_json_rpc 
    mcp_lib
    GTest::gtest 
    GTest::gtest_main
)

add_executable(test_mcp_client
    test_mcp_client.cpp
)

target_link_libraries(test_mcp_client 
    mcp_client 
    mcp_lib
    GTest::gtest 
    GTest::gtest_main
)

# Add tests to CTest
add_test(NAME JsonRpcTest COMMAND test_json_rpc)
add_test(NAME McpClientTest COMMAND test_mcp_client)

# Example test
add_executable(test_example
    test_example.cpp
)

target_link_libraries(test_example 
    GTest::gtest 
    GTest::gtest_main
)

# Enable Google Test discovery
include(GoogleTest)
gtest_discover_tests(test_json_rpc)
gtest_discover_tests(test_mcp_client)
gtest_discover_tests(test_example)