#include "config.h"
#include <fstream>
#include <iostream>

namespace mcp {

Config& Config::GetInstance() {
    static Config instance;
    return instance;
}

bool Config::LoadFromFile(const std::string& config_file_path) {
    config_file_path_ = config_file_path;

    try {
        std::ifstream config_file(config_file_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to open config file: " << config_file_path << std::endl;
            return false;
        }

        config_file >> config_data_;
        config_file.close();

        if (!config_data_.contains("server") ||
            !config_data_["server"].is_object()) {
            std::cerr << "Missing or invalid 'server' section\n";
            return false;
        }

        SetDefaults(); // 补充相关默认值

        if (!ValidateConfig()) {
            std::cerr << "Config validation failed" << std::endl;
            return false;
        }

        loaded_ = true;
        return true;

    } catch (const json::parse_error& e) {
        std::cerr << "Config parse error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

bool Config::ValidateConfig() const {
    // Check if required sections exist
    if (!config_data_.contains("server")) {
        std::cerr << "Missing 'server' section in config" << std::endl;
        return false;
    }

    int worker_threads = config_data_["server"].value("worker_threads", 4);
    if (worker_threads < 1 || worker_threads > 1024) {
        std::cerr << "worker_threads must be in range 1-1024: "
                << worker_threads << std::endl;
        return false;
    }

    int max_pending_tasks =
        config_data_["server"].value("max_pending_tasks", 128);
    if (max_pending_tasks < 1 || max_pending_tasks > 1000000) {
        std::cerr << "max_pending_tasks must be in range 1-1000000: "
                << max_pending_tasks << std::endl;
        return false;
    }

    int tool_workers =
        config_data_["server"].value("tool_workers", 2);

    if (tool_workers < 1 || tool_workers > 1024) {
        std::cerr << "tool_workers must be in range 1-1024: "
                << tool_workers << std::endl;
        return false;
    }

    int tool_max_pending_tasks =
        config_data_["server"].value(
            "tool_max_pending_tasks",
            32
        );

    if (
        tool_max_pending_tasks < 1 ||
        tool_max_pending_tasks > 1000000
    ) {
        std::cerr
            << "tool_max_pending_tasks must be in range 1-1000000: "
            << tool_max_pending_tasks
            << std::endl;
        return false;
    }

    int tool_circuit_failure_threshold =
        config_data_["server"].value(
            "tool_circuit_failure_threshold",
            5
        );

    if (
        tool_circuit_failure_threshold < 1 ||
        tool_circuit_failure_threshold > 1000000
    ) {
        std::cerr
            << "tool_circuit_failure_threshold must be in range 1-1000000: "
            << tool_circuit_failure_threshold
            << std::endl;
        return false;
    }

    int tool_circuit_open_ms =
        config_data_["server"].value("tool_circuit_open_ms", 30000);

    if (tool_circuit_open_ms < 1 || tool_circuit_open_ms > 3600000) {
        std::cerr << "tool_circuit_open_ms must be in range 1-3600000: "
                  << tool_circuit_open_ms << std::endl;
        return false;
    }

    int resource_workers =
        config_data_["server"].value("resource_workers", 2);

    if (resource_workers < 1 || resource_workers > 1024) {
        std::cerr << "resource_workers must be in range 1-1024: "
                << resource_workers << std::endl;
        return false;
    }

    int resource_max_pending_tasks =
        config_data_["server"].value(
            "resource_max_pending_tasks",
            32
        );

    if (
        resource_max_pending_tasks < 1 ||
        resource_max_pending_tasks > 1000000
    ) {
        std::cerr
            << "resource_max_pending_tasks must be in range 1-1000000: "
            << resource_max_pending_tasks
            << std::endl;
        return false;
    }

    int prompt_workers =
        config_data_["server"].value("prompt_workers", 2);

    if (prompt_workers < 1 || prompt_workers > 1024) {
        std::cerr << "prompt_workers must be in range 1-1024: "
                << prompt_workers << std::endl;
        return false;
    }

    int prompt_max_pending_tasks =
        config_data_["server"].value(
            "prompt_max_pending_tasks",
            32
        );

    if (
        prompt_max_pending_tasks < 1 ||
        prompt_max_pending_tasks > 1000000
    ) {
        std::cerr
            << "prompt_max_pending_tasks must be in range 1-1000000: "
            << prompt_max_pending_tasks
            << std::endl;
        return false;
    }

    // SSE 长连接数必须有限，避免连接无限占用 HTTP 处理线程。
    int sse_max_clients =
        config_data_["server"].value("sse_max_clients", 16);

    if (sse_max_clients < 1 || sse_max_clients > 100000) {
        std::cerr << "sse_max_clients must be in range 1-100000: "
                  << sse_max_clients << std::endl;
        return false;
    }

    // 单客户端事件缓冲必须有限，避免慢客户端造成内存持续增长。
    int sse_max_pending_events_per_client =
        config_data_["server"].value(
            "sse_max_pending_events_per_client",
            128
        );

    if (
        sse_max_pending_events_per_client < 1 ||
        sse_max_pending_events_per_client > 1000000
    ) {
        std::cerr
            << "sse_max_pending_events_per_client must be in range 1-1000000: "
            << sse_max_pending_events_per_client
            << std::endl;
        return false;
    }

    int sse_replay_buffer_events =
        config_data_["server"].value("sse_replay_buffer_events", 256);

    if (sse_replay_buffer_events < 1 || sse_replay_buffer_events > 1000000) {
        std::cerr << "sse_replay_buffer_events must be in range 1-1000000: "
                  << sse_replay_buffer_events << std::endl;
        return false;
    }

    int request_timeout_ms =
        config_data_["server"].value("request_timeout_ms", 30000);
    if (request_timeout_ms < 1 || request_timeout_ms > 3600000) {
        std::cerr << "request_timeout_ms must be in range 1-3600000: "
                << request_timeout_ms << std::endl;
        return false;
    }

    // Check port range
    int port = config_data_["server"].value("port", 8080);
    if (port < 1 || port > 65535) {
        std::cerr << "Server port must be in range 1-65535: " << port << std::endl;
        return false;
    }

    // SSE 服务使用独立端口，必须处于有效 TCP 端口范围内。
    int sse_port = config_data_["server"].value("sse_port", 8090);
    if (sse_port < 1 || sse_port > 65535) {
        std::cerr << "SSE port must be in range 1-65535: "
                  << sse_port << std::endl;
        return false;
    }

    // 两个 HTTP 服务不能监听同一个端口。
    if (sse_port == port) {
        std::cerr << "SSE port must differ from server port: "
                  << sse_port << std::endl;
        return false;
    }

    // SSE worker 数必须大于 0，才能处理流式连接。
    int sse_workers =
        config_data_["server"].value("sse_workers", 2);

    if (sse_workers < 1 || sse_workers > 1024) {
        std::cerr << "sse_workers must be in range 1-1024: "
                  << sse_workers << std::endl;
        return false;
    }

    // Validate logging configuration if present
    if (config_data_.contains("logging")) {
        // Validate log level
        std::string log_level = config_data_["logging"].value("log_level", std::string("info"));
        if (log_level != "trace" && log_level != "debug" && log_level != "info" &&
            log_level != "warn" && log_level != "error" && log_level != "critical") {
            std::cerr << "Invalid log level: " << log_level << std::endl;
            std::cerr << "Valid levels: trace, debug, info, warn, error, critical" << std::endl;
            return false;
        }

        // Validate log file size (should be positive)
        size_t log_file_size = config_data_["logging"].value("log_file_size", 10 * 1024 * 1024);
        if (log_file_size == 0) {
            std::cerr << "Log file size must be greater than 0" << std::endl;
            return false;
        }

        // Validate log file count (should be positive)
        int log_file_count = config_data_["logging"].value("log_file_count", 5);
        if (log_file_count <= 0) {
            std::cerr << "Log file count must be greater than 0" << std::endl;
            return false;
        }
    }

    // ===================================================================
    // 🔧 Extension point: Add validation logic for new fields here
    // ===================================================================
    // Example:
    // if (!config_data_.contains("database")) {
    //     std::cerr << "Missing 'database' section in config" << std::endl;
    //     return false;
    // }
    //
    // std::string db_url = config_data_["database"].value("url", "");
    // if (db_url.empty()) {
    //     std::cerr << "Database URL cannot be empty" << std::endl;
    //     return false;
    // }
    // ===================================================================

    return true;
}

void Config::SetDefaults() {
    // Set default values if missing in config file
    if (!config_data_.contains("server")) {
        config_data_["server"] = json::object();
    }

    auto& server = config_data_["server"];
    if (!server.contains("port")) {
        server["port"] = 8080;  // Default port
    }

    // SSE 使用独立端口与专用 HTTP worker，避免占用 JSON-RPC 线程。
    if (!server.contains("sse_port")) {
        server["sse_port"] = 8090;
    }

    if (!server.contains("sse_workers")) {
        server["sse_workers"] = 2;
    }

    if (!server.contains("worker_threads")) {
        server["worker_threads"] = 4;
    }

    if (!server.contains("max_pending_tasks")) {
        server["max_pending_tasks"] = 128;
    }

    if (!server.contains("request_timeout_ms")) {
        server["request_timeout_ms"] = 30000;
    }

    // tools/call 使用独立执行池，避免慢工具耗尽默认 worker。
    if (!server.contains("tool_workers")) {
        server["tool_workers"] = 2;
    }

    if (!server.contains("tool_max_pending_tasks")) {
        server["tool_max_pending_tasks"] = 32;
    }

    // 连续失败的工具会暂时熔断，保护 tool worker 不被持续占用。
    if (!server.contains("tool_circuit_failure_threshold")) {
        server["tool_circuit_failure_threshold"] = 5;
    }

    if (!server.contains("tool_circuit_open_ms")) {
        server["tool_circuit_open_ms"] = 30000;
    }

    // resources/read 使用独立执行池，避免慢资源读取阻塞其他请求。
    if (!server.contains("resource_workers")) {
        server["resource_workers"] = 2;
    }

    if (!server.contains("resource_max_pending_tasks")) {
        server["resource_max_pending_tasks"] = 32;
    }

    // prompts/get 使用独立执行池，避免慢 prompt 生成阻塞其他请求。
    if (!server.contains("prompt_workers")) {
        server["prompt_workers"] = 2;
    }

    if (!server.contains("prompt_max_pending_tasks")) {
        server["prompt_max_pending_tasks"] = 32;
    }

    // SSE 采用有界连接数和有界客户端事件缓冲，防止慢客户端耗尽资源。
    if (!server.contains("sse_max_clients")) {
        server["sse_max_clients"] = 16;
    }

    if (!server.contains("sse_max_pending_events_per_client")) {
        server["sse_max_pending_events_per_client"] = 128;
    }

    if (!server.contains("sse_replay_buffer_events")) {
        server["sse_replay_buffer_events"] = 256;
    }

    // Set logging defaults
    if (!config_data_.contains("logging")) {
        config_data_["logging"] = json::object();
    }

    auto& logging = config_data_["logging"];
    if (!logging.contains("log_file_path")) {
        logging["log_file_path"] = "../../logs/server.log";
    }
    if (!logging.contains("log_level")) {
        logging["log_level"] = "info";
    }
    if (!logging.contains("log_file_size")) {
        logging["log_file_size"] = 10 * 1024 * 1024;  // 10MB
    }
    if (!logging.contains("log_file_count")) {
        logging["log_file_count"] = 5;
    }
    if (!logging.contains("log_console_output")) {
        logging["log_console_output"] = true;
    }
}

// ===================================================================
// Config getter implementations
// ===================================================================

int Config::GetServerPort() const {
    return config_data_["server"].value("port", 8080);
}


std::size_t Config::GetWorkerThreads() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 4;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value("worker_threads", 4)
    );
}

std::size_t Config::GetMaxPendingTasks() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 128;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value("max_pending_tasks", 128)
    );
}

int Config::GetRequestTimeoutMs() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 30000;
    }

    return config_data_["server"].value(
        "request_timeout_ms",
        30000
    );
}

std::size_t Config::GetToolWorkers() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 2;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value("tool_workers", 2)
    );
}

std::size_t Config::GetToolMaxPendingTasks() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 32;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value(
            "tool_max_pending_tasks",
            32
        )
    );
}

std::size_t Config::GetToolCircuitFailureThreshold() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 5;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value(
            "tool_circuit_failure_threshold",
            5
        )
    );
}

int Config::GetToolCircuitOpenMs() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 30000;
    }

    return config_data_["server"].value("tool_circuit_open_ms", 30000);
}

std::size_t Config::GetResourceWorkers() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 2;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value("resource_workers", 2)
    );
}

std::size_t Config::GetResourceMaxPendingTasks() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 32;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value(
            "resource_max_pending_tasks",
            32
        )
    );
}

std::size_t Config::GetPromptWorkers() const {
    // 配置尚未加载时，返回与默认配置一致的 worker 数。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 2;
    }

    // 从 server 配置读取 prompt 专用 worker 数。
    return static_cast<std::size_t>(
        config_data_["server"].value("prompt_workers", 2)
    );
}

std::size_t Config::GetPromptMaxPendingTasks() const {
    // 配置尚未加载时，返回与默认配置一致的队列容量。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 32;
    }

    // 从 server 配置读取 prompt 专用队列容量。
    return static_cast<std::size_t>(
        config_data_["server"].value(
            "prompt_max_pending_tasks",
            32
        )
    );
}

std::size_t Config::GetSseMaxClients() const {
    // 配置尚未加载时，限制 SSE 连接数为默认值。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 16;
    }

    // 读取 SSE 同时在线客户端上限。
    return static_cast<std::size_t>(
        config_data_["server"].value("sse_max_clients", 16)
    );
}

std::size_t Config::GetSseMaxPendingEventsPerClient() const {
    // 配置尚未加载时，限制单客户端事件缓冲为默认值。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 128;
    }

    // 读取单个 SSE 客户端允许积压的事件数量。
    return static_cast<std::size_t>(
        config_data_["server"].value(
            "sse_max_pending_events_per_client",
            128
        )
    );
}

std::size_t Config::GetSseReplayBufferEvents() const {
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 256;
    }

    return static_cast<std::size_t>(
        config_data_["server"].value("sse_replay_buffer_events", 256)
    );
}

int Config::GetSsePort() const {
    // 配置尚未加载时，使用独立 SSE 服务的默认端口。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 8090;
    }

    // 读取独立 SSE HTTP 服务的监听端口。
    return config_data_["server"].value("sse_port", 8090);
}

std::size_t Config::GetSseWorkers() const {
    // 配置尚未加载时，使用默认 SSE worker 数。
    if (
        !config_data_.is_object() ||
        !config_data_.contains("server") ||
        !config_data_["server"].is_object()
    ) {
        return 2;
    }

    // 读取专门处理流式 SSE 连接的 worker 数。
    return static_cast<std::size_t>(
        config_data_["server"].value("sse_workers", 2)
    );
}

// ===================================================================
// Logging configuration implementations
// ===================================================================

std::string Config::GetLogFilePath() const {
    return config_data_["logging"].value("log_file_path", std::string("logs/server.log"));
}

std::string Config::GetLogLevel() const {
    return config_data_["logging"].value("log_level", std::string("info"));
}

size_t Config::GetLogFileSize() const {
    return config_data_["logging"].value("log_file_size", 10 * 1024 * 1024); // Default 10MB
}

int Config::GetLogFileCount() const {
    return config_data_["logging"].value("log_file_count", 5);
}

bool Config::GetLogConsoleOutput() const {
    return config_data_["logging"].value("log_console_output", true);
}

// ===================================================================
// 🔧 Extension point: Implement new config getter methods here
// ===================================================================
// Example:
// std::string Config::GetServerHost() const {
//     return config_data_["server"].value("host", std::string("0.0.0.0"));
// }
//
// std::string Config::GetLogFilePath() const {
//     return config_data_["logging"].value("log_file_path", std::string("logs/server.log"));
// }
//
// std::string Config::GetDatabaseUrl() const {
//     return config_data_["database"].value("url", std::string(""));
// }
// ===================================================================

} // namespace mcp
