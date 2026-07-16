#include <gtest/gtest.h>
#include "config.h"
#include <fstream>
#include <filesystem>

using namespace mcp;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试程序在 build/tests/ 目录运行，所以需要相对路径
        config_file_ = "../../config/server.json";
        temp_dir_ = "../../tests/";
    }

    void TearDown() override {
        // 清理测试期间创建的临时文件
        std::vector<std::string> temp_files = {
            temp_dir_ + "invalid_config.json",
            temp_dir_ + "invalid_port_config.json",
            temp_dir_ + "negative_port_config.json",
            temp_dir_ + "missing_section_config.json",
            temp_dir_ + "min_port_config.json",
            temp_dir_ + "max_port_config.json",
            temp_dir_ + "common_port_config.json",

            temp_dir_ + "runtime_defaults_config.json",

            temp_dir_ + "invalid_worker_threads_config.json",
            temp_dir_ + "invalid_pending_tasks_config.json",
            temp_dir_ + "invalid_request_timeout_config.json",

            temp_dir_ + "invalid_tool_workers_config.json",
            temp_dir_ + "invalid_tool_pending_tasks_config.json",

            temp_dir_ + "invalid_resource_workers_config.json",
            temp_dir_ + "invalid_resource_pending_tasks_config.json",

            temp_dir_ + "invalid_prompt_workers_config.json",
            temp_dir_ + "invalid_prompt_pending_tasks_config.json",

            // SSE 配置非法值测试生成的临时文件。
            temp_dir_ + "invalid_sse_max_clients_config.json",
            temp_dir_ + "invalid_sse_pending_events_config.json",

            // 独立 SSE 服务配置非法值测试生成的临时文件。
            temp_dir_ + "invalid_sse_port_config.json",
            temp_dir_ + "conflicting_sse_port_config.json",
            temp_dir_ + "invalid_sse_workers_config.json"
        };

        for (const auto& file : temp_files) {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
            }
        }
    }

    std::string config_file_;
    std::string temp_dir_;
};

// 测试加载 config/server.json 配置文件
TEST_F(ConfigTest, LoadConfigFileTest) {
    Config& config = Config::GetInstance();

    bool result = config.LoadFromFile(config_file_);
    EXPECT_TRUE(result);
    EXPECT_TRUE(config.IsLoaded());

    // 验证端口号
    EXPECT_EQ(config.GetServerPort(), 8089);
}

// 测试加载不存在的配置文件
TEST_F(ConfigTest, LoadMissingConfigFileTest) {
    Config& config = Config::GetInstance();

    bool result = config.LoadFromFile("nonexistent.json");
    EXPECT_FALSE(result);
}

// 测试加载无效的JSON文件
TEST_F(ConfigTest, LoadInvalidJsonTest) {
    std::string invalid_file = temp_dir_ + "invalid_config.json";
    std::ofstream file(invalid_file);
    file << "{ invalid json content }}";
    file.close();

    Config& config = Config::GetInstance();
    bool result = config.LoadFromFile(invalid_file);
    EXPECT_FALSE(result);
}

// 测试服务器端口配置读取
TEST_F(ConfigTest, GetServerPortTest) {
    Config& config = Config::GetInstance();
    config.LoadFromFile(config_file_);

    EXPECT_EQ(config.GetServerPort(), 8089);
}

// 测试配置宏
TEST_F(ConfigTest, ConfigMacroTest) {
    MCP_CONFIG.LoadFromFile(config_file_);

    EXPECT_EQ(MCP_CONFIG.GetServerPort(), 8089);
    EXPECT_TRUE(MCP_CONFIG.IsLoaded());
}

// 测试配置验证 - 无效端口号（超出范围）
TEST_F(ConfigTest, InvalidPortRangeTest) {
    std::string invalid_port_file = temp_dir_ + "invalid_port_config.json";
    std::ofstream file(invalid_port_file);
    file << R"({
        "server": {
            "port": 70000
        }
    })";
    file.close();

    Config& config = Config::GetInstance();
    bool result = config.LoadFromFile(invalid_port_file);
    EXPECT_FALSE(result);
}

// 测试配置验证 - 负数端口号
TEST_F(ConfigTest, NegativePortTest) {
    std::string negative_port_file = temp_dir_ + "negative_port_config.json";
    std::ofstream file(negative_port_file);
    file << R"({
        "server": {
            "port": -1
        }
    })";
    file.close();

    Config& config = Config::GetInstance();
    bool result = config.LoadFromFile(negative_port_file);
    EXPECT_FALSE(result);
}

// 测试缺少必要配置节
TEST_F(ConfigTest, MissingServerSectionTest) {
    std::string missing_section_file = temp_dir_ + "missing_section_config.json";
    std::ofstream file(missing_section_file);
    file << R"({
        "other": {
            "value": 123
        }
    })";
    file.close();

    Config& config = Config::GetInstance();
    bool result = config.LoadFromFile(missing_section_file);
    EXPECT_FALSE(result);
}

// 测试有效端口范围
TEST_F(ConfigTest, ValidPortRangesTest) {
    Config& config = Config::GetInstance();

    // 测试最小有效端口
    std::string min_port_file = temp_dir_ + "min_port_config.json";
    std::ofstream file1(min_port_file);
    file1 << R"({"server": {"port": 1}})";
    file1.close();

    EXPECT_TRUE(config.LoadFromFile(min_port_file));
    EXPECT_EQ(config.GetServerPort(), 1);

    // 测试最大有效端口
    std::string max_port_file = temp_dir_ + "max_port_config.json";
    std::ofstream file2(max_port_file);
    file2 << R"({"server": {"port": 65535}})";
    file2.close();

    EXPECT_TRUE(config.LoadFromFile(max_port_file));
    EXPECT_EQ(config.GetServerPort(), 65535);

    // 测试常用端口
    std::string common_port_file = temp_dir_ + "common_port_config.json";
    std::ofstream file3(common_port_file);
    file3 << R"({"server": {"port": 3000}})";
    file3.close();

    EXPECT_TRUE(config.LoadFromFile(common_port_file));
    EXPECT_EQ(config.GetServerPort(), 3000);
}

TEST_F(ConfigTest, ReadsTaskRuntimeConfiguration) {
    Config& config = Config::GetInstance();

    ASSERT_TRUE(config.LoadFromFile(config_file_));

    EXPECT_EQ(config.GetWorkerThreads(), 4u);
    EXPECT_EQ(config.GetMaxPendingTasks(), 128u);
    EXPECT_EQ(config.GetRequestTimeoutMs(), 30000);

    EXPECT_EQ(config.GetToolWorkers(), 2u);
    EXPECT_EQ(config.GetToolMaxPendingTasks(), 32u);
    EXPECT_EQ(config.GetToolCircuitFailureThreshold(), 5u);
    EXPECT_EQ(config.GetToolCircuitOpenMs(), 30000);

    EXPECT_EQ(config.GetResourceWorkers(), 2u);
    EXPECT_EQ(config.GetResourceMaxPendingTasks(), 32u);

    // 验证配置文件中的 prompt 专用执行池参数。
    EXPECT_EQ(config.GetPromptWorkers(), 2u);
    EXPECT_EQ(config.GetPromptMaxPendingTasks(), 32u);

    // 验证配置文件中的 SSE 资源上限。
    EXPECT_EQ(config.GetSseMaxClients(), 16u);
    EXPECT_EQ(config.GetSseMaxPendingEventsPerClient(), 128u);
    EXPECT_EQ(config.GetSseReplayBufferEvents(), 256u);

    // 验证独立 SSE 服务的端口与专用 worker 配置。
    EXPECT_EQ(config.GetSsePort(), 8090);
    EXPECT_EQ(config.GetSseWorkers(), 2u);
}

TEST_F(ConfigTest, UsesTaskRuntimeDefaults) {
    std::string defaults_file =
        temp_dir_ + "runtime_defaults_config.json";

    std::ofstream file(defaults_file);
    file << R"({
        "server": {
            "port": 8080
        }
    })";
    file.close();

    Config& config = Config::GetInstance();

    ASSERT_TRUE(config.LoadFromFile(defaults_file));

    // 缺省并发参数应回退到安全默认值。
    EXPECT_EQ(config.GetWorkerThreads(), 4u);
    EXPECT_EQ(config.GetMaxPendingTasks(), 128u);
    EXPECT_EQ(config.GetRequestTimeoutMs(), 30000);

    EXPECT_EQ(config.GetToolWorkers(), 2u);
    EXPECT_EQ(config.GetToolMaxPendingTasks(), 32u);

    EXPECT_EQ(config.GetResourceWorkers(), 2u);
    EXPECT_EQ(config.GetResourceMaxPendingTasks(), 32u);

    // 验证缺省 prompt 参数会回退到安全默认值。
    EXPECT_EQ(config.GetPromptWorkers(), 2u);
    EXPECT_EQ(config.GetPromptMaxPendingTasks(), 32u);

    // 验证旧配置未填写 SSE 字段时的安全默认值。
    EXPECT_EQ(config.GetSseMaxClients(), 16u);
    EXPECT_EQ(config.GetSseMaxPendingEventsPerClient(), 128u);

    // 验证旧配置未填写时，独立 SSE 服务使用安全默认值。
    EXPECT_EQ(config.GetSsePort(), 8090);
    EXPECT_EQ(config.GetSseWorkers(), 2u);
}

TEST_F(ConfigTest, RejectsInvalidWorkerThreads) {
    std::string file_path =
        temp_dir_ + "invalid_worker_threads_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "worker_threads": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidMaxPendingTasks) {
    std::string file_path =
        temp_dir_ + "invalid_pending_tasks_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "max_pending_tasks": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidRequestTimeout) {
    std::string file_path =
        temp_dir_ + "invalid_request_timeout_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "request_timeout_ms": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}


TEST_F(ConfigTest, RejectsInvalidToolWorkers) {
    std::string file_path =
        temp_dir_ + "invalid_tool_workers_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "tool_workers": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidToolMaxPendingTasks) {
    std::string file_path =
        temp_dir_ + "invalid_tool_pending_tasks_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "tool_max_pending_tasks": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidResourceWorkers) {
    std::string file_path =
        temp_dir_ + "invalid_resource_workers_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "resource_workers": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidPromptWorkers) {
    // 使用 TearDown() 会清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_prompt_workers_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "prompt_workers": 0
        }
    })";
    file.close();

    // 0 个 worker 无法消费 prompt 队列，因此配置应被拒绝。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidPromptMaxPendingTasks) {
    // 使用 TearDown() 会清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_prompt_pending_tasks_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "prompt_max_pending_tasks": 0
        }
    })";
    file.close();

    // 0 容量队列不能接收任何 prompt 任务，因此配置应被拒绝。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidSseMaxClients) {
    // 使用 TearDown() 自动清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_sse_max_clients_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "sse_max_clients": 0
        }
    })";
    file.close();

    // 0 个允许客户端没有实际意义，应在启动前拒绝该配置。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidSseMaxPendingEventsPerClient) {
    // 使用 TearDown() 自动清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_sse_pending_events_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "sse_max_pending_events_per_client": 0
        }
    })";
    file.close();

    // 0 容量无法缓存事件，应在启动前拒绝该配置。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidSsePort) {
    // 使用 TearDown() 自动清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_sse_port_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "sse_port": 0
        }
    })";
    file.close();

    // TCP 端口不能为 0，配置加载应失败。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsSsePortThatConflictsWithServerPort) {
    // 使用与其他 SSE 端口测试隔离的临时配置文件。
    std::string file_path =
        temp_dir_ + "conflicting_sse_port_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "sse_port": 8080
        }
    })";
    file.close();

    // 两个 HTTP 服务不能监听同一个端口。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidSseWorkers) {
    // 使用 TearDown() 自动清理的临时配置文件。
    std::string file_path =
        temp_dir_ + "invalid_sse_workers_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "sse_port": 8090,
            "sse_workers": 0
        }
    })";
    file.close();

    // 0 个 worker 无法处理任何 SSE 长连接，应拒绝该配置。
    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

TEST_F(ConfigTest, RejectsInvalidResourceMaxPendingTasks) {
    std::string file_path =
        temp_dir_ + "invalid_resource_pending_tasks_config.json";

    std::ofstream file(file_path);
    file << R"({
        "server": {
            "port": 8080,
            "resource_max_pending_tasks": 0
        }
    })";
    file.close();

    EXPECT_FALSE(MCP_CONFIG.LoadFromFile(file_path));
}

// ===================================================================
// 🔧 扩展位置：在这里添加新配置字段的测试用例
// ===================================================================
// 示例：
// // 测试服务器主机地址配置
// TEST_F(ConfigTest, GetServerHostTest) {
//     Config& config = Config::GetInstance();
//     config.LoadFromFile(config_file_);
//     EXPECT_EQ(config.GetServerHost(), "0.0.0.0");
// }
//
// // 测试日志文件路径配置
// TEST_F(ConfigTest, GetLogFilePathTest) {
//     Config& config = Config::GetInstance();
//     config.LoadFromFile(config_file_);
//     EXPECT_EQ(config.GetLogFilePath(), "logs/server.log");
// }
// ===================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
