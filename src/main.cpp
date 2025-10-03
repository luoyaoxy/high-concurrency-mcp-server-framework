#include "mcp/config.h"
#include "mcp/logger.h"
#include "mcp/jsonrpc.h"
#include <iostream>

using namespace mcp;
using namespace mcp::logger;

/**
 * Convert string log level to spdlog level enum
 */
spdlog::level::level_enum StringToLogLevel(const std::string& level_str) {
    if (level_str == "trace") {
        return spdlog::level::trace;
    } else if (level_str == "debug") {
        return spdlog::level::debug;
    } else if (level_str == "info") {
        return spdlog::level::info;
    } else if (level_str == "warn") {
        return spdlog::level::warn;
    } else if (level_str == "error") {
        return spdlog::level::err;
    } else if (level_str == "critical") {
        return spdlog::level::critical;
    }
    // Default to info if unknown
    return spdlog::level::info;
}

int main(int argc, char* argv[]) {
    // ===================================================================
    // Step 1: Load Configuration
    // ===================================================================
    std::string config_file = "../../config/server.json";
    if (argc > 1) {
        config_file = argv[1];
    }

    if (!MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
        return 1;
    }

    std::string log_file = MCP_CONFIG.GetLogFilePath();
    std::string log_level_str = MCP_CONFIG.GetLogLevel();
    size_t log_file_size = MCP_CONFIG.GetLogFileSize();
    int log_file_count = MCP_CONFIG.GetLogFileCount();
    bool log_console = MCP_CONFIG.GetLogConsoleOutput();

  
    spdlog::level::level_enum log_level = StringToLogLevel(log_level_str);


    MCP_LOG_INIT("mcp_server", log_file, log_file_size, log_file_count, log_console);
    MCP_LOG_SET_LEVEL(log_level);

  
    int port = MCP_CONFIG.GetServerPort();

  
    MCP_LOG_INFO("Successfully started MCP Server on port {}...", port);
    MCP_LOG_INFO("Log file: {}", log_file);
    MCP_LOG_INFO("Log level: {}", log_level_str);
    MCP_LOG_INFO("Log file size: {}", log_file_size);
    MCP_LOG_INFO("Log file count: {}", log_file_count);
    MCP_LOG_INFO("Log console output: {}", log_console);

    // ==============================
    // 注册 JSON-RPC 方法并启动 stdio 服务器
    // ==============================
    mcp::JsonRpcDispatcher dispatcher;
    dispatcher.registerHandler("initialize", [](const nlohmann::json& params) -> nlohmann::json {
        nlohmann::json capabilities = {
            {"protocolVersion", "2024-11-05"},
            {"implementation", {
                {"name", "mcp-tutorial"},
                {"version", "1.0.0"}
            }}
        };
        return capabilities;
    });

    dispatcher.registerHandler("echo", [](const nlohmann::json& params) -> nlohmann::json {
        return params;
    });

    mcp::StdioJsonRpcServer server(std::move(dispatcher));
    server.run();

    MCP_LOG_SHUTDOWN();
    return 0;
}
