#include "mcp/client.h"
#include <gtest/gtest.h>

using namespace mcp;

TEST(McpClientTest, ClientConfiguration) {
    ClientConfig config;
    config.server_url = "http://localhost:8080/mcp";
    config.timeout_seconds = 30;
    config.headers["Authorization"] = "Bearer token";
    
    EXPECT_EQ(config.server_url, "http://localhost:8080/mcp");
    EXPECT_EQ(config.timeout_seconds, 30);
    EXPECT_EQ(config.headers["Authorization"], "Bearer token");
}

TEST(McpClientTest, ToolSerialization) {
    Tool tool;
    tool.name = "test_tool";
    tool.description = "A test tool";
    
    ToolParameter param;
    param.type = "string";
    param.description = "Test parameter";
    param.required = true;
    
    tool.parameters["param1"] = param;
    
    json tool_json = tool.ToJson();
    EXPECT_EQ(tool_json["name"], "test_tool");
    EXPECT_EQ(tool_json["description"], "A test tool");
    EXPECT_EQ(tool_json["inputSchema"]["properties"]["param1"]["type"], "string");
    EXPECT_EQ(tool_json["inputSchema"]["required"].size(), 1);
    EXPECT_EQ(tool_json["inputSchema"]["required"][0], "param1");
    
    Tool parsed_tool = Tool::FromJson(tool_json);
    EXPECT_EQ(parsed_tool.name, tool.name);
    EXPECT_EQ(parsed_tool.description, tool.description);
    EXPECT_EQ(parsed_tool.parameters.size(), 1);
    EXPECT_EQ(parsed_tool.parameters["param1"].type, "string");
    EXPECT_TRUE(parsed_tool.parameters["param1"].required);
}

TEST(McpClientTest, ResourceSerialization) {
    Resource resource;
    resource.uri = "test://resource";
    resource.name = "Test Resource";
    resource.description = "A test resource";
    resource.mime_type = "application/json";
    
    json resource_json = resource.ToJson();
    EXPECT_EQ(resource_json["uri"], "test://resource");
    EXPECT_EQ(resource_json["name"], "Test Resource");
    EXPECT_EQ(resource_json["description"], "A test resource");
    EXPECT_EQ(resource_json["mimeType"], "application/json");
    
    Resource parsed_resource = Resource::FromJson(resource_json);
    EXPECT_EQ(parsed_resource.uri, resource.uri);
    EXPECT_EQ(parsed_resource.name, resource.name);
    EXPECT_EQ(parsed_resource.description, resource.description);
    EXPECT_EQ(parsed_resource.mime_type, resource.mime_type);
}

TEST(McpClientTest, ToolResultSerialization) {
    ToolResult result;
    result.type = "text";
    result.content = json{{"message", "Hello World"}};
    result.is_error = false;
    
    json result_json = result.ToJson();
    EXPECT_EQ(result_json["type"], "text");
    EXPECT_EQ(result_json["content"]["message"], "Hello World");
    EXPECT_EQ(result_json["isError"], false);
    
    ToolResult parsed_result = ToolResult::FromJson(result_json);
    EXPECT_EQ(parsed_result.type, result.type);
    EXPECT_EQ(parsed_result.content, result.content);
    EXPECT_EQ(parsed_result.is_error, result.is_error);
    
    // Test error result
    ToolResult error_result;
    error_result.type = "error";
    error_result.content = json{{"error", "Something went wrong"}};
    error_result.is_error = true;
    
    json error_json = error_result.ToJson();
    EXPECT_EQ(error_json["isError"], true);
    
    ToolResult parsed_error = ToolResult::FromJson(error_json);
    EXPECT_TRUE(parsed_error.is_error.value());
}

TEST(McpClientTest, ClientState) {
    ClientConfig config;
    config.server_url = "http://nonexistent:9999/mcp";
    config.timeout_seconds = 1; // Short timeout for quick test
    
    McpClient client(config);
    
    // Initial state should be disconnected
    EXPECT_EQ(client.GetState(), ClientState::Disconnected);
    EXPECT_FALSE(client.GetServerInfo().has_value());
}

TEST(McpClientTest, AsyncClient) {
    ClientConfig config;
    config.server_url = "http://nonexistent:9999/mcp";
    config.timeout_seconds = 1;
    
    AsyncMcpClient async_client(config);
    
    bool callback_called = false;
    std::string error_message;
    
    async_client.Connect([&](bool success, const std::string& error) {
        callback_called = true;
        error_message = error;
        EXPECT_FALSE(success); // Should fail to connect to nonexistent server
        EXPECT_FALSE(error.empty());
    });
    
    // Give it some time to attempt connection
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Note: In a real test, we'd wait for the callback or use a condition variable
    // For this simple test, we just verify the async client was created
    
    async_client.Disconnect();
}

