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
        SetDefaults(); // 补充相关默认值

        if (!ValidateConfig()) {
            std::cerr << "Config validation failed" << std::endl;
            return false;
        }

        loaded_ = true;
        std::cout << "Config loaded successfully: " << config_file_path << std::endl;
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

    // Check port range
    int port = config_data_["server"].value("port", 8080);
    if (port < 1 || port > 65535) {
        std::cerr << "Server port must be in range 1-65535: " << port << std::endl;
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
