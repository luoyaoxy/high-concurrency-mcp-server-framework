/**
 * @file test_mcp_server.cpp
 * @brief MCP 服务器单元测试
 */

#include <gtest/gtest.h>
#include "mcp_server.h"
#include "types.h"

using namespace mcp;

class McpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<McpServer>("test-server", "1.0.0");
    }

    void TearDown() override {
        server.reset();
    }

    std::unique_ptr<McpServer> server;
};

// ===== 初始化测试 =====

TEST_F(McpServerTest, Initialize) {
    auto result = server->get_initialize_result();

    EXPECT_EQ(result.protocol_version, LATEST_PROTOCOL_VERSION);
    EXPECT_EQ(result.server_info.name, "test-server");
    EXPECT_EQ(result.server_info.version, "1.0.0");
}

// ===== Tools 测试 =====

TEST_F(McpServerTest, RegisterTool) {
    Tool tool;
    tool.name = "test_tool";
    tool.description = "Test tool";

    server->register_tool(tool, [](const json& args) -> ToolResult {
        ToolResult result;
        result.content.push_back(ContentItem{
            .type = "text",
            .text = "test"
        });
        return result;
    });

    EXPECT_TRUE(server->has_tool("test_tool"));
    EXPECT_FALSE(server->has_tool("nonexistent"));
}

TEST_F(McpServerTest, ListTools) {
    Tool tool1;
    tool1.name = "tool1";
    tool1.description = "Tool 1";

    Tool tool2;
    tool2.name = "tool2";
    tool2.description = "Tool 2";

    server->register_tool(tool1, [](const json&) -> ToolResult {
        return ToolResult();
    });

    server->register_tool(tool2, [](const json&) -> ToolResult {
        return ToolResult();
    });

    auto tools = server->list_tools();
    EXPECT_EQ(tools.size(), 2);
}

TEST_F(McpServerTest, CallTool) {
    Tool tool;
    tool.name = "echo";
    tool.description = "Echo tool";

    server->register_tool(tool, [](const json& args) -> ToolResult {
        std::string message = args.at("message").get<std::string>();
        ToolResult result;
        result.content.push_back(ContentItem{
            .type = "text",
            .text = message
        });
        return result;
    });

    json args = {{"message", "Hello"}};
    auto result = server->call_tool("echo", args);

    EXPECT_FALSE(result.is_error);
    EXPECT_EQ(result.content.size(), 1);
    EXPECT_EQ(result.content[0].text.value(), "Hello");
}

TEST_F(McpServerTest, CallNonexistentTool) {
    json args = {{"message", "Hello"}};

    EXPECT_THROW({
        server->call_tool("nonexistent", args);
    }, std::runtime_error);
}

// ===== Resources 测试 =====

TEST_F(McpServerTest, RegisterResource) {
    Resource resource;
    resource.uri = "test://resource";
    resource.name = "Test Resource";

    server->register_resource(resource, [](const std::string& uri) -> ResourceContent {
        ResourceContent content;
        content.uri = uri;
        content.text = "test content";
        return content;
    });

    EXPECT_TRUE(server->has_resource("test://resource"));
    EXPECT_FALSE(server->has_resource("nonexistent"));
}

TEST_F(McpServerTest, ListResources) {
    Resource res1;
    res1.uri = "test://res1";
    res1.name = "Resource 1";

    Resource res2;
    res2.uri = "test://res2";
    res2.name = "Resource 2";

    server->register_resource(res1, [](const std::string&) -> ResourceContent {
        return ResourceContent();
    });

    server->register_resource(res2, [](const std::string&) -> ResourceContent {
        return ResourceContent();
    });

    auto resources = server->list_resources();
    EXPECT_EQ(resources.size(), 2);
}

TEST_F(McpServerTest, ReadResource) {
    Resource resource;
    resource.uri = "test://data";
    resource.name = "Test Data";

    server->register_resource(resource, [](const std::string& uri) -> ResourceContent {
        ResourceContent content;
        content.uri = uri;
        content.text = "Hello, World!";
        content.mime_type = "text/plain";
        return content;
    });

    auto content = server->read_resource("test://data");

    EXPECT_EQ(content.uri, "test://data");
    EXPECT_EQ(content.text, "Hello, World!");
    EXPECT_EQ(content.mime_type.value(), "text/plain");
}

TEST_F(McpServerTest, ReadNonexistentResource) {
    EXPECT_THROW({
        server->read_resource("nonexistent://resource");
    }, std::runtime_error);
}

// ===== Prompts 测试 =====

TEST_F(McpServerTest, RegisterPrompt) {
    Prompt prompt;
    prompt.name = "test_prompt";
    prompt.description = "Test prompt";

    server->register_prompt(prompt, [](const json&) -> std::vector<PromptMessage> {
        return {};
    });

    EXPECT_TRUE(server->has_prompt("test_prompt"));
    EXPECT_FALSE(server->has_prompt("nonexistent"));
}

TEST_F(McpServerTest, ListPrompts) {
    Prompt prompt1;
    prompt1.name = "prompt1";

    Prompt prompt2;
    prompt2.name = "prompt2";

    server->register_prompt(prompt1, [](const json&) -> std::vector<PromptMessage> {
        return {};
    });

    server->register_prompt(prompt2, [](const json&) -> std::vector<PromptMessage> {
        return {};
    });

    auto prompts = server->list_prompts();
    EXPECT_EQ(prompts.size(), 2);
}

TEST_F(McpServerTest, GetPrompt) {
    Prompt prompt;
    prompt.name = "greeting";

    server->register_prompt(prompt, [](const json& args) -> std::vector<PromptMessage> {
        std::string name = args.at("name").get<std::string>();

        std::vector<PromptMessage> messages;
        PromptMessage msg;
        msg.role = Role::User;
        msg.content = {
            {"type", "text"},
            {"text", "Hello, " + name + "!"}
        };
        messages.push_back(msg);
        return messages;
    });

    json args = {{"name", "Alice"}};
    auto messages = server->get_prompt("greeting", args);

    EXPECT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].role, Role::User);
    EXPECT_EQ(messages[0].content["text"].get<std::string>(), "Hello, Alice!");
}

TEST_F(McpServerTest, GetNonexistentPrompt) {
    json args = {{"name", "Alice"}};

    EXPECT_THROW({
        server->get_prompt("nonexistent", args);
    }, std::runtime_error);
}

// ===== JSON 序列化测试 =====

TEST_F(McpServerTest, ToolSerialization) {
    Tool tool;
    tool.name = "test";
    tool.description = "Test tool";
    tool.input_schema.properties = {
        {"arg1", {{"type", "string"}}}
    };
    tool.input_schema.required = {"arg1"};

    json j = tool.to_json();

    EXPECT_EQ(j["name"].get<std::string>(), "test");
    EXPECT_EQ(j["description"].get<std::string>(), "Test tool");
    EXPECT_TRUE(j.contains("inputSchema"));

    Tool tool2 = Tool::from_json(j);
    EXPECT_EQ(tool2.name, tool.name);
    EXPECT_EQ(tool2.description, tool.description);
}

TEST_F(McpServerTest, ResourceSerialization) {
    Resource resource;
    resource.uri = "test://resource";
    resource.name = "Test";
    resource.description = "Test resource";
    resource.mime_type = "text/plain";

    json j = resource.to_json();

    EXPECT_EQ(j["uri"].get<std::string>(), "test://resource");
    EXPECT_EQ(j["name"].get<std::string>(), "Test");

    Resource resource2 = Resource::from_json(j);
    EXPECT_EQ(resource2.uri, resource.uri);
    EXPECT_EQ(resource2.name, resource.name);
}

TEST_F(McpServerTest, InitializeResultSerialization) {
    auto result = server->get_initialize_result();
    json j = result.to_json();

    EXPECT_TRUE(j.contains("protocolVersion"));
    EXPECT_TRUE(j.contains("capabilities"));
    EXPECT_TRUE(j.contains("serverInfo"));

    InitializeResult result2 = InitializeResult::from_json(j);
    EXPECT_EQ(result2.protocol_version, result.protocol_version);
    EXPECT_EQ(result2.server_info.name, result.server_info.name);
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
