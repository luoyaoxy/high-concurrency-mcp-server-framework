/**
 * @file test_mcp_server.cpp
 * @brief MCP 服务器单元测试
 */

#include <gtest/gtest.h>
#include "mcp_server.h"
#include "types.h"

#include "logger.h"

#include <atomic>
#include <chrono>
#include <future>

using namespace mcp;

class McpServerTest : public ::testing::Test {
protected:

    static void SetUpTestSuite() {
        MCP_LOG_INIT("mcp_server_test", "", 0, 0, false);
    }

    static void TearDownTestSuite() {
        MCP_LOG_SHUTDOWN();
    }

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

TEST_F(McpServerTest, NegotiatesEverySupportedProtocolVersion) {
    for (std::string_view version : kSupportedProtocolVersions) {
        const auto result = server->get_initialize_result(version);
        EXPECT_EQ(result.protocol_version, version);
    }
}

TEST_F(McpServerTest, UnsupportedProtocolVersionUsesLatestSupportedVersion) {
    const auto result = server->get_initialize_result("2099-01-01");
    EXPECT_EQ(result.protocol_version, kLatestProtocolVersion);
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
    }, std::invalid_argument);
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

TEST_F(McpServerTest, ToolHandlersCanRunConcurrently) {
    std::atomic<int> active_count{0};
    std::atomic<int> max_active_count{0};

    std::promise<void> both_started;
    std::future<void> both_started_result =
        both_started.get_future();

    std::promise<void> release;
    std::shared_future<void> release_signal =
        release.get_future().share();

    Tool tool;
    tool.name = "parallel";
    tool.description = "Parallel test tool";

    server->register_tool(
        tool,
        [
            &active_count,
            &max_active_count,
            &both_started,
            release_signal
        ](const json&) -> ToolResult {
            const int current =
                active_count.fetch_add(1) + 1;

            int previous = max_active_count.load();
            while (
                previous < current &&
                !max_active_count.compare_exchange_weak(
                    previous,
                    current
                )
            ) {}

            // 两个 handler 同时进入时，通知测试线程。
            if (current == 2) {
                both_started.set_value();
            }

            release_signal.wait();
            active_count.fetch_sub(1);

            return ToolResult{};
        }
    );

    auto first = std::async(std::launch::async, [this] {
        return server->call_tool("parallel", json::object());
    });

    auto second = std::async(std::launch::async, [this] {
        return server->call_tool("parallel", json::object());
    });

    const bool ran_concurrently =
        both_started_result.wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready;

    // 无论断言结果如何，都要解除 handler 阻塞。
    release.set_value();

    first.get();
    second.get();

    EXPECT_TRUE(ran_concurrently);
    EXPECT_EQ(max_active_count.load(), 2);
}

TEST_F(McpServerTest, RegisterToolDoesNotWaitForRunningHandler) {
    std::promise<void> handler_started;
    std::future<void> handler_started_result =
        handler_started.get_future();

    std::promise<void> release_handler;
    std::shared_future<void> release_signal =
        release_handler.get_future().share();

    Tool slow_tool;
    slow_tool.name = "slow_tool";

    server->register_tool(
        slow_tool,
        [&handler_started, release_signal](const json&) -> ToolResult {
            handler_started.set_value();
            release_signal.wait();
            return ToolResult{};
        }
    );

    auto slow_call = std::async(std::launch::async, [this] {
        return server->call_tool("slow_tool", json::object());
    });

    const bool handler_is_running =
        handler_started_result.wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready;

    Tool new_tool;
    new_tool.name = "registered_while_slow";

    // 注册需要独占锁；若 handler 持锁，这里会被慢调用阻塞。
    auto registration = std::async(std::launch::async, [this, new_tool] {
        server->register_tool(new_tool, [](const json&) -> ToolResult {
            return ToolResult{};
        });
    });

    const bool registration_finished =
        registration.wait_for(std::chrono::milliseconds(200)) ==
        std::future_status::ready;

    // 断言前先解除阻塞，避免测试失败时遗留工作线程。
    release_handler.set_value();
    slow_call.get();
    registration.get();

    EXPECT_TRUE(handler_is_running);
    EXPECT_TRUE(registration_finished);
    EXPECT_TRUE(server->has_tool("registered_while_slow"));
}

TEST_F(McpServerTest, ReadOtherResourceDoesNotWaitForRunningProvider) {
    std::promise<void> provider_started;
    std::future<void> provider_started_result =
        provider_started.get_future();

    std::promise<void> release_provider;
    std::shared_future<void> release_signal =
        release_provider.get_future().share();

    Resource slow_resource;
    slow_resource.uri = "test://slow-resource";
    server->register_resource(
        slow_resource,
        [&provider_started, release_signal](const std::string& uri) {
            provider_started.set_value();
            release_signal.wait();
            ResourceContent content;
            content.uri = uri;
            content.text = "slow";
            return content;
        }
    );

    Resource fast_resource;
    fast_resource.uri = "test://fast-resource";
    server->register_resource(
        fast_resource,
        [](const std::string& uri) {
            ResourceContent content;
            content.uri = uri;
            content.text = "fast";
            return content;
        }
    );

    auto slow_read = std::async(std::launch::async, [this] {
        return server->read_resource("test://slow-resource");
    });

    const bool provider_is_running =
        provider_started_result.wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready;

    // 两次读只应短暂共享 registry 锁，不应相互等待 provider 执行。
    auto fast_read = std::async(std::launch::async, [this] {
        return server->read_resource("test://fast-resource");
    });

    const bool fast_read_finished =
        fast_read.wait_for(std::chrono::milliseconds(200)) ==
        std::future_status::ready;

    release_provider.set_value();
    slow_read.get();
    ResourceContent fast_content = fast_read.get();

    EXPECT_TRUE(provider_is_running);
    EXPECT_TRUE(fast_read_finished);
    EXPECT_EQ(fast_content.text, "fast");
}

TEST_F(McpServerTest, GetOtherPromptDoesNotWaitForRunningGenerator) {
    std::promise<void> generator_started;
    std::future<void> generator_started_result =
        generator_started.get_future();

    std::promise<void> release_generator;
    std::shared_future<void> release_signal =
        release_generator.get_future().share();

    Prompt slow_prompt;
    slow_prompt.name = "slow_prompt";
    server->register_prompt(
        slow_prompt,
        [&generator_started, release_signal](const json&) {
            generator_started.set_value();
            release_signal.wait();
            return std::vector<PromptMessage>{};
        }
    );

    Prompt fast_prompt;
    fast_prompt.name = "fast_prompt";
    server->register_prompt(
        fast_prompt,
        [](const json&) {
            PromptMessage message;
            message.role = Role::Assistant;
            message.content = {{"type", "text"}, {"text", "fast"}};
            return std::vector<PromptMessage>{message};
        }
    );

    auto slow_get = std::async(std::launch::async, [this] {
        return server->get_prompt("slow_prompt", json::object());
    });

    const bool generator_is_running =
        generator_started_result.wait_for(std::chrono::seconds(1)) ==
        std::future_status::ready;

    // generator 在锁外运行时，其他 prompt 仍可立即查询并生成。
    auto fast_get = std::async(std::launch::async, [this] {
        return server->get_prompt("fast_prompt", json::object());
    });

    const bool fast_get_finished =
        fast_get.wait_for(std::chrono::milliseconds(200)) ==
        std::future_status::ready;

    release_generator.set_value();
    slow_get.get();
    std::vector<PromptMessage> fast_messages = fast_get.get();

    EXPECT_TRUE(generator_is_running);
    EXPECT_TRUE(fast_get_finished);
    ASSERT_EQ(fast_messages.size(), 1);
    EXPECT_EQ(fast_messages.front().content["text"], "fast");
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
