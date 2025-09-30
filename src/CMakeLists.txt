# MCP Library
add_library(mcp_lib STATIC
    lib/json_rpc.cpp
    lib/http_transport.cpp
    lib/mcp_protocol.cpp
    lib/config.cpp
    logger/logger.cpp
)

target_include_directories(mcp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mcp_lib
    ${CURL_LIBRARIES}
    Threads::Threads
)

# Link JSON library (using vcpkg)
target_link_libraries(mcp_lib nlohmann_json::nlohmann_json)

# Link spdlog library (using vcpkg)
target_link_libraries(mcp_lib spdlog::spdlog)

# MCP Client SDK
add_library(mcp_client STATIC
    mcp_client/client.cpp
)

target_link_libraries(mcp_client mcp_lib)

# MCP Server
add_executable(mcp_weather_server
    mcp_server/main.cpp
    mcp_server/weather_server.cpp
    mcp_server/weather_api.cpp
)

target_include_directories(mcp_weather_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mcp_weather_server mcp_lib)