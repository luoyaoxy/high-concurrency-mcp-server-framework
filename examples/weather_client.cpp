#include "mcp/client.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace mcp;

// 打印天气数据的函数
void print_weather_data(const json& weather) {
    std::cout << "=== 天气信息 ===" << std::endl;
    std::cout << "城市: " << weather.value("city", "未知") << std::endl;
    std::cout << "国家: " << weather.value("country", "未知") << std::endl;
    std::cout << "温度: " << weather.value("temperature", 0.0) << "°C" << std::endl;
    std::cout << "描述: " << weather.value("description", "未知") << std::endl;
    std::cout << "湿度: " << weather.value("humidity", 0.0) << "%" << std::endl;
    std::cout << "气压: " << weather.value("pressure", 0.0) << " hPa" << std::endl;
    std::cout << "风速: " << weather.value("wind_speed", 0.0) << " m/s" << std::endl;
    std::cout << "================" << std::endl;
}

// 打印天气预报数据的函数
void print_forecast_data(const json& forecast) {
    std::cout << "=== 天气预报 ===" << std::endl;
    
    if (forecast.contains("forecast") && forecast["forecast"].is_array()) {
        for (const auto& day : forecast["forecast"]) {
            std::cout << "日期: " << day.value("date", "未知") << std::endl;
            std::cout << "  最低温度: " << day.value("temp_min", 0.0) << "°C" << std::endl;
            std::cout << "  最高温度: " << day.value("temp_max", 0.0) << "°C" << std::endl;
            std::cout << "  描述: " << day.value("description", "未知") << std::endl;
            std::cout << "  湿度: " << day.value("humidity", 0.0) << "%" << std::endl;
            std::cout << "  降水概率: " << day.value("precipitation_chance", 0.0) << "%" << std::endl;
            std::cout << std::endl;
        }
    }
    
    std::cout << "================" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string server_url = "http://localhost:8080/mcp";
    std::string city = "Beijing";
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--server" && i + 1 < argc) {
            server_url = argv[++i];
        } else if (arg == "--city" && i + 1 < argc) {
            city = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --server URL     MCP server URL (default: http://localhost:8080/mcp)\n"
                      << "  --city CITY      City name (default: Beijing)\n"
                      << "  --help           Show this help message\n"
                      << std::endl;
            return 0;
        }
    }
    
    try {
        // 创建客户端配置
        ClientConfig config;
        config.server_url = server_url;
        config.timeout_seconds = 30;
        
        // 创建 MCP 客户端
        McpClient client(config);
        
        std::cout << "连接到 MCP 服务器: " << server_url << std::endl;
        
        // 连接到服务器
        if (!client.Connect().get()) {
            std::cerr << "无法连接到服务器" << std::endl;
            return 1;
        }
        
        std::cout << "成功连接到服务器" << std::endl;
        
        // 获取服务器信息
        auto server_info = client.GetServerInfo();
        if (server_info) {
            std::cout << "服务器信息:" << std::endl;
            std::cout << "  名称: " << server_info->name << std::endl;
            std::cout << "  版本: " << server_info->version << std::endl;
            std::cout << "  协议版本: " << server_info->protocol_version << std::endl;
        }
        
        std::cout << std::endl;
        
        // 列出可用的工具
        std::cout << "获取可用工具..." << std::endl;
        auto tools = client.ListTools().get();
        
        std::cout << "可用工具:" << std::endl;
        for (const auto& tool : tools) {
            std::cout << "  - " << tool.name << ": " << tool.description << std::endl;
        }
        
        std::cout << std::endl;
        
        // 获取当前天气
        std::cout << "获取 " << city << " 的当前天气..." << std::endl;
        
        json weather_args;
        weather_args["city"] = city;
        
        auto weather_result = client.CallTool("get_current_weather", weather_args).get();
        
        if (weather_result.is_error && weather_result.is_error.value()) {
            std::cerr << "获取天气失败: " << weather_result.content.dump() << std::endl;
        } else {
            if (weather_result.content.contains("weather")) {
                print_weather_data(weather_result.content["weather"]);
            }
            
            if (weather_result.content.contains("summary")) {
                std::cout << "摘要: " << weather_result.content["summary"] << std::endl;
            }
            
            if (weather_result.content.contains("advice")) {
                std::cout << "建议: " << weather_result.content["advice"] << std::endl;
            }
        }
        
        std::cout << std::endl;
        
        // 获取天气预报
        std::cout << "获取 " << city << " 的天气预报..." << std::endl;
        
        json forecast_args;
        forecast_args["city"] = city;
        forecast_args["days"] = 3;
        
        auto forecast_result = client.CallTool("get_forecast", forecast_args).get();
        
        if (forecast_result.is_error && forecast_result.is_error.value()) {
            std::cerr << "获取预报失败: " << forecast_result.content.dump() << std::endl;
        } else {
            print_forecast_data(forecast_result.content);
            
            if (forecast_result.content.contains("summary")) {
                std::cout << "摘要: " << forecast_result.content["summary"] << std::endl;
            }
        }
        
        std::cout << std::endl;
        
        // 通过坐标获取天气（北京坐标）
        std::cout << "通过坐标获取北京天气..." << std::endl;
        
        json coords_args;
        coords_args["latitude"] = 39.9042;
        coords_args["longitude"] = 116.4074;
        
        auto coords_result = client.CallTool("get_weather_by_coordinates", coords_args).get();
        
        if (coords_result.is_error && coords_result.is_error.value()) {
            std::cerr << "获取坐标天气失败: " << coords_result.content.dump() << std::endl;
        } else {
            if (coords_result.content.contains("weather")) {
                print_weather_data(coords_result.content["weather"]);
            }
            
            if (coords_result.content.contains("summary")) {
                std::cout << "摘要: " << coords_result.content["summary"] << std::endl;
            }
        }
        
        std::cout << std::endl;
        
        // 读取帮助资源
        std::cout << "读取帮助资源..." << std::endl;
        
        auto help_content = client.ReadResource("weather://help").get();
        
        if (!help_content.empty()) {
            std::cout << "帮助信息:" << std::endl;
            std::cout << help_content.dump(2) << std::endl;
        }
        
        // 断开连接
        client.Disconnect();
        std::cout << "已断开连接" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}