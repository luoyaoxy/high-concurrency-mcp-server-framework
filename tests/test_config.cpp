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
            temp_dir_ + "common_port_config.json"
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
    EXPECT_EQ(config.GetServerPort(), 8080);
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

    EXPECT_EQ(config.GetServerPort(), 8080);
}

// 测试配置宏
TEST_F(ConfigTest, ConfigMacroTest) {
    MCP_CONFIG.LoadFromFile(config_file_);

    EXPECT_EQ(MCP_CONFIG.GetServerPort(), 8080);
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
