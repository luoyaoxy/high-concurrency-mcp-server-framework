#include "config.h"
#include "logger.h"
#include <iostream>

using namespace mcp;
using namespace mcp::logger;

/**
 * Configuration and Logging Demo
 *
 * This example demonstrates:
 * 1. Loading configuration from JSON file
 * 2. Accessing configuration values
 * 3. Using logging macros with different levels
 */
int main() {
    std::cout << "==============================================\n";
    std::cout << "MCP Configuration & Logging Demo\n";
    std::cout << "==============================================\n\n";

    // ===================================================================
    // Part 1: Configuration Demo
    // ===================================================================
    std::cout << "1. Loading Configuration...\n";

    if (!MCP_CONFIG.LoadFromFile("../config/server.json")) {
        std::cerr << "Failed to load configuration\n";
        return 1;
    }

    std::cout << "   ✓ Configuration loaded successfully\n";
    std::cout << "   - Server Port: " << MCP_CONFIG.GetServerPort() << "\n\n";

    // ===================================================================
    // Part 2: Logger Demo
    // ===================================================================
    std::cout << "2. Initializing Logger...\n";

    // Initialize logger with file output
    MCP_LOG_INIT("demo", "../logs/demo.log", 5*1024*1024, 3, true);
    MCP_LOG_SET_LEVEL(spdlog::level::debug);

    std::cout << "   ✓ Logger initialized\n\n";

    // ===================================================================
    // Part 3: Different Log Levels
    // ===================================================================
    std::cout << "3. Testing Different Log Levels...\n\n";

    MCP_LOG_TRACE("This is a TRACE level message (very detailed)");
    MCP_LOG_DEBUG("This is a DEBUG level message (debugging info)");
    MCP_LOG_INFO("This is an INFO level message (general info)");
    MCP_LOG_WARN("This is a WARN level message (warning)");
    MCP_LOG_ERROR("This is an ERROR level message (error occurred)");
    MCP_LOG_CRITICAL("This is a CRITICAL level message (critical error)");

    // ===================================================================
    // Part 4: Formatted Logging
    // ===================================================================
    std::cout << "\n4. Testing Formatted Logging...\n\n";

    int user_id = 12345;
    std::string username = "john_doe";
    double balance = 1234.56;

    MCP_LOG_INFO("User login: id={}, name={}", user_id, username);
    MCP_LOG_INFO("Account balance: ${:.2f}", balance);

    // ===================================================================
    // Part 5: Conditional Logging
    // ===================================================================
    std::cout << "\n5. Testing Conditional Logging...\n\n";

    int status_code = 404;
    MCP_LOG_ERROR_IF(status_code >= 400, "HTTP error: {}", status_code);

    bool is_authenticated = true;
    MCP_LOG_DEBUG_IF(is_authenticated, "User is authenticated");

    // ===================================================================
    // Part 6: Configuration Values in Logs
    // ===================================================================
    std::cout << "\n6. Logging Configuration Values...\n\n";

    MCP_LOG_INFO("Server configuration:");
    MCP_LOG_INFO("  Port: {}", MCP_CONFIG.GetServerPort());

    // ===================================================================
    // Cleanup
    // ===================================================================
    std::cout << "\n7. Shutting Down...\n";

    MCP_LOG_INFO("Demo completed successfully");
    MCP_LOG_SHUTDOWN();

    std::cout << "   ✓ Logger shut down\n";
    std::cout << "\n==============================================\n";
    std::cout << "Demo Complete!\n";
    std::cout << "Check logs/demo.log for log output\n";
    std::cout << "==============================================\n";

    return 0;
}
