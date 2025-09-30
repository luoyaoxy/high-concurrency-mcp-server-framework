# Weather Client Example
add_executable(weather_client
    weather_client.cpp
)

target_link_libraries(weather_client
    mcp_client
    mcp_lib
)

# Simple MCP Client Example
add_executable(simple_client
    simple_client.cpp
)

target_link_libraries(simple_client
    mcp_client
    mcp_lib
)

# Logger example
add_executable(logger_example logger_example.cpp)
target_link_libraries(logger_example mcp_lib)