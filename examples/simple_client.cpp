#include "mcp/client.h"
#include <iostream>
#include <thread>

using namespace mcp;

int main(int argc, char* argv[]) {
    std::string server_url = "http://localhost:8080/mcp";
    
    if (argc > 1) {
        server_url = argv[1];
    }
    
    try {
        // Create client configuration
        ClientConfig config;
        config.server_url = server_url;
        config.timeout_seconds = 30;
        
        std::cout << "Simple MCP Client Example" << std::endl;
        std::cout << "=========================" << std::endl;
        std::cout << "Connecting to: " << server_url << std::endl;
        
        // Create MCP client
        McpClient client(config);
        
        // Connect to server
        std::cout << "Connecting..." << std::endl;
        if (!client.Connect().get()) {
            std::cerr << "Failed to connect to server" << std::endl;
            return 1;
        }
        
        std::cout << "Connected successfully!" << std::endl;
        
        // Get server info
        auto server_info = client.GetServerInfo();
        if (server_info) {
            std::cout << "\nServer Info:" << std::endl;
            std::cout << "  Name: " << server_info->name << std::endl;
            std::cout << "  Version: " << server_info->version << std::endl;
            std::cout << "  Protocol: " << server_info->protocol_version << std::endl;
        }
        
        // List available tools
        std::cout << "\nListing available tools..." << std::endl;
        auto tools = client.ListTools().get();
        
        std::cout << "Available tools (" << tools.size() << "):" << std::endl;
        for (size_t i = 0; i < tools.size(); ++i) {
            const auto& tool = tools[i];
            std::cout << "  " << (i + 1) << ". " << tool.name << std::endl;
            std::cout << "     Description: " << tool.description << std::endl;
            std::cout << "     Parameters:" << std::endl;
            
            for (const auto& [param_name, param] : tool.parameters) {
                std::cout << "       - " << param_name << " (" << param.type << ")";
                if (param.required) {
                    std::cout << " [required]";
                }
                std::cout << ": " << param.description << std::endl;
            }
            std::cout << std::endl;
        }
        
        // List available resources
        std::cout << "Listing available resources..." << std::endl;
        auto resources = client.ListResources().get();
        
        std::cout << "Available resources (" << resources.size() << "):" << std::endl;
        for (size_t i = 0; i < resources.size(); ++i) {
            const auto& resource = resources[i];
            std::cout << "  " << (i + 1) << ". " << resource.name << std::endl;
            std::cout << "     URI: " << resource.uri << std::endl;
            if (resource.description) {
                std::cout << "     Description: " << resource.description.value() << std::endl;
            }
            if (resource.mime_type) {
                std::cout << "     MIME Type: " << resource.mime_type.value() << std::endl;
            }
            std::cout << std::endl;
        }
        
        // Interactive tool calling
        std::cout << "Interactive mode - Enter commands (type 'help' for available commands, 'quit' to exit):" << std::endl;
        
        std::string input;
        while (true) {
            std::cout << "> ";
            std::getline(std::cin, input);
            
            if (input.empty()) continue;
            
            if (input == "quit" || input == "exit") {
                break;
            }
            
            if (input == "help") {
                std::cout << "Available commands:" << std::endl;
                std::cout << "  help                    - Show this help" << std::endl;
                std::cout << "  list tools              - List available tools" << std::endl;
                std::cout << "  list resources          - List available resources" << std::endl;
                std::cout << "  weather <city>          - Get weather for city" << std::endl;
                std::cout << "  forecast <city> [days]  - Get forecast for city" << std::endl;
                std::cout << "  coords <lat> <lon>      - Get weather by coordinates" << std::endl;
                std::cout << "  read <resource_uri>     - Read a resource" << std::endl;
                std::cout << "  quit                    - Exit" << std::endl;
                continue;
            }
            
            if (input == "list tools") {
                for (const auto& tool : tools) {
                    std::cout << "  " << tool.name << ": " << tool.description << std::endl;
                }
                continue;
            }
            
            if (input == "list resources") {
                for (const auto& resource : resources) {
                    std::cout << "  " << resource.uri << ": " << resource.name << std::endl;
                }
                continue;
            }
            
            // Parse weather command
            if (input.substr(0, 7) == "weather") {
                std::string city = "Beijing";
                if (input.length() > 8) {
                    city = input.substr(8);
                }
                
                json args;
                args["city"] = city;
                
                try {
                    auto result = client.CallTool("get_current_weather", args).get();
                    if (result.is_error && result.is_error.value()) {
                        std::cout << "Error: " << result.content.dump(2) << std::endl;
                    } else {
                        std::cout << "Result: " << result.content.dump(2) << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "Exception: " << e.what() << std::endl;
                }
                continue;
            }
            
            // Parse forecast command
            if (input.substr(0, 8) == "forecast") {
                std::string rest = input.length() > 9 ? input.substr(9) : "";
                std::string city = "Beijing";
                int days = 3;
                
                if (!rest.empty()) {
                    size_t space_pos = rest.find(' ');
                    if (space_pos != std::string::npos) {
                        city = rest.substr(0, space_pos);
                        try {
                            days = std::stoi(rest.substr(space_pos + 1));
                        } catch (...) {
                            days = 3;
                        }
                    } else {
                        city = rest;
                    }
                }
                
                json args;
                args["city"] = city;
                args["days"] = days;
                
                try {
                    auto result = client.CallTool("get_forecast", args).get();
                    if (result.is_error && result.is_error.value()) {
                        std::cout << "Error: " << result.content.dump(2) << std::endl;
                    } else {
                        std::cout << "Result: " << result.content.dump(2) << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "Exception: " << e.what() << std::endl;
                }
                continue;
            }
            
            // Parse coords command
            if (input.substr(0, 6) == "coords") {
                std::string rest = input.length() > 7 ? input.substr(7) : "";
                double lat = 39.9042, lon = 116.4074; // Default to Beijing
                
                if (!rest.empty()) {
                    std::istringstream iss(rest);
                    iss >> lat >> lon;
                }
                
                json args;
                args["latitude"] = lat;
                args["longitude"] = lon;
                
                try {
                    auto result = client.CallTool("get_weather_by_coordinates", args).get();
                    if (result.is_error && result.is_error.value()) {
                        std::cout << "Error: " << result.content.dump(2) << std::endl;
                    } else {
                        std::cout << "Result: " << result.content.dump(2) << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "Exception: " << e.what() << std::endl;
                }
                continue;
            }
            
            // Parse read command
            if (input.substr(0, 4) == "read") {
                std::string uri = "weather://help";
                if (input.length() > 5) {
                    uri = input.substr(5);
                }
                
                try {
                    auto content = client.ReadResource(uri).get();
                    std::cout << "Resource content: " << content.dump(2) << std::endl;
                } catch (const std::exception& e) {
                    std::cout << "Exception: " << e.what() << std::endl;
                }
                continue;
            }
            
            std::cout << "Unknown command: " << input << " (type 'help' for available commands)" << std::endl;
        }
        
        // Disconnect
        client.Disconnect();
        std::cout << "Disconnected. Goodbye!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}