#include "mcp/weather_server.h"
#include "mcp/config.h"
#include <iostream>

namespace mcp {

// 构造函数：初始化天气 API 并设置工具和资源
WeatherMcpServer::WeatherMcpServer(std::unique_ptr<WeatherApi> weather_api) 
    : weather_api_(std::move(weather_api)) {
    SetupTools();
    SetupResources();
}

// 设置天气相关的 MCP 工具
void WeatherMcpServer::SetupTools() {
    // 当前天气查询工具
    Tool current_weather_tool;
    current_weather_tool.name = "get_current_weather";
    current_weather_tool.description = "Get current weather information for a city";
    
    ToolParameter city_param;
    city_param.type = "string";
    city_param.description = "Name of the city to get weather for";
    city_param.required = true;
    current_weather_tool.parameters["city"] = city_param;
    
    AddTool(current_weather_tool);
    
    // 天气预报查询工具
    Tool forecast_tool;
    forecast_tool.name = "get_forecast";
    forecast_tool.description = "Get weather forecast for a city";
    
    ToolParameter city_param2;
    city_param2.type = "string";
    city_param2.description = "Name of the city to get forecast for";
    city_param2.required = true;
    forecast_tool.parameters["city"] = city_param2;
    
    ToolParameter days_param;
    days_param.type = "integer";
    days_param.description = "Number of days to forecast (1-5)";
    days_param.required = false;
    days_param.default_value = 5;
    forecast_tool.parameters["days"] = days_param;
    
    AddTool(forecast_tool);
    
    // 根据坐标查询天气工具
    Tool coords_weather_tool;
    coords_weather_tool.name = "get_weather_by_coordinates";
    coords_weather_tool.description = "Get current weather information by latitude and longitude";
    
    ToolParameter lat_param;
    lat_param.type = "number";
    lat_param.description = "Latitude coordinate";
    lat_param.required = true;
    coords_weather_tool.parameters["latitude"] = lat_param;
    
    ToolParameter lon_param;
    lon_param.type = "number";
    lon_param.description = "Longitude coordinate";
    lon_param.required = true;
    coords_weather_tool.parameters["longitude"] = lon_param;
    
    AddTool(coords_weather_tool);
}

// 设置天气相关的 MCP 资源
void WeatherMcpServer::SetupResources() {
    Resource weather_help;
    weather_help.uri = "weather://help";
    weather_help.name = "Weather API Help";
    weather_help.description = "Help information for weather API usage";
    weather_help.mime_type = "application/json";
    
    AddResource(weather_help);
}

// 执行指定的工具，根据工具名称分发到相应的处理函数
ToolResult WeatherMcpServer::ExecuteTool(const std::string& name, const json& arguments) {
    try {
        if (name == "get_current_weather") {
            return HandleGetCurrentWeather(arguments);
        } else if (name == "get_forecast") {
            return HandleGetForecast(arguments);
        } else if (name == "get_weather_by_coordinates") {
            return HandleGetWeatherByCoordinates(arguments);
        }
        
        return CreateErrorResult("Unknown tool: " + name);
    } catch (const std::exception& e) {
        return CreateErrorResult("Tool execution failed: " + std::string(e.what()));
    }
}

// 读取指定的资源内容
json WeatherMcpServer::ReadResource(const std::string& uri) {
    if (uri == "weather://help") {
        return json{
            {"tools", {
                {"get_current_weather", {
                    {"description", "Get current weather for a city"},
                    {"parameters", {
                        {"city", "string (required) - Name of the city"}
                    }},
                    {"example", {
                        {"city", "Beijing"}
                    }}
                }},
                {"get_forecast", {
                    {"description", "Get weather forecast for a city"},
                    {"parameters", {
                        {"city", "string (required) - Name of the city"},
                        {"days", "integer (optional, default=5) - Number of days (1-5)"}
                    }},
                    {"example", {
                        {"city", "Shanghai"},
                        {"days", 3}
                    }}
                }},
                {"get_weather_by_coordinates", {
                    {"description", "Get weather by geographical coordinates"},
                    {"parameters", {
                        {"latitude", "number (required) - Latitude"},
                        {"longitude", "number (required) - Longitude"}
                    }},
                    {"example", {
                        {"latitude", 39.9042},
                        {"longitude", 116.4074}
                    }}
                }}
            }},
            {"usage_notes", {
                "City names should be in English",
                "Coordinates should be in decimal degrees",
                "Temperature is returned in Celsius",
                "Wind speed is in m/s"
            }}
        };
    }
    
    throw std::runtime_error("Resource not found: " + uri);
}

// 处理获取当前天气的请求
ToolResult WeatherMcpServer::HandleGetCurrentWeather(const json& arguments) {
    if (!arguments.contains("city") || !arguments["city"].is_string()) {
        return CreateErrorResult("Missing or invalid 'city' parameter");
    }
    
    std::string city = arguments["city"];
    
    try {
        auto weather_data = weather_api_->GetCurrentWeather(city);
        
        // 构建返回结果
        json result;
        result["weather"] = weather_data.ToJson();
        // 生成中文摘要
        result["summary"] = city + "今天" + weather_data.description + 
                           "，气温" + std::to_string(static_cast<int>(weather_data.temperature)) + 
                           "℃，湿度" + std::to_string(static_cast<int>(weather_data.humidity)) + "%";
        
        // 根据温度提供建议
        double hot_threshold = MCP_CONFIG.GetWeatherRulesHotThreshold();
        double cold_threshold = MCP_CONFIG.GetWeatherRulesColdThreshold();
        
        if (weather_data.temperature > hot_threshold) {
            result["advice"] = "天气较热，建议穿着轻薄衣物，多补充水分";
        } else if (weather_data.temperature < cold_threshold) {
            result["advice"] = "天气较冷，建议添加衣物保暖";
        } else {
            result["advice"] = "天气适宜，适合户外活动";
        }
        
        return CreateSuccessResult(result);
        
    } catch (const std::exception& e) {
        return CreateErrorResult("Failed to get weather data: " + std::string(e.what()));
    }
}

// 处理获取天气预报的请求
ToolResult WeatherMcpServer::HandleGetForecast(const json& arguments) {
    if (!arguments.contains("city") || !arguments["city"].is_string()) {
        return CreateErrorResult("Missing or invalid 'city' parameter");
    }
    
    std::string city = arguments["city"];
    int days = arguments.value("days", 5);
    
    // 从配置获取最大预报天数
    int max_days = MCP_CONFIG.GetWeatherRulesForecastMaxDays();
    if (days < 1 || days > max_days) {
        days = max_days;
    }
    
    try {
        auto forecast_data = weather_api_->GetForecast(city, days);
        
        // 构建预报结果
        json result;
        result["city"] = city;
        result["forecast"] = json::array();
        
        for (const auto& day : forecast_data) {
            result["forecast"].push_back(day.ToJson());
        }
        
        result["summary"] = city + "未来" + std::to_string(days) + "天天气预报";
        
        return CreateSuccessResult(result);
        
    } catch (const std::exception& e) {
        return CreateErrorResult("Failed to get forecast data: " + std::string(e.what()));
    }
}

// 处理根据坐标获取天气的请求
ToolResult WeatherMcpServer::HandleGetWeatherByCoordinates(const json& arguments) {
    if (!arguments.contains("latitude") || !arguments.contains("longitude") ||
        !arguments["latitude"].is_number() || !arguments["longitude"].is_number()) {
        return CreateErrorResult("Missing or invalid 'latitude' or 'longitude' parameters");
    }
    
    double lat = arguments["latitude"];
    double lon = arguments["longitude"];
    
    // 从配置获取坐标范围验证
    auto [lat_min, lat_max] = MCP_CONFIG.GetWeatherRulesLatitudeRange();
    auto [lon_min, lon_max] = MCP_CONFIG.GetWeatherRulesLongitudeRange();
    
    if (lat < lat_min || lat > lat_max || lon < lon_min || lon > lon_max) {
        return CreateErrorResult("Invalid coordinates: latitude must be [" + 
                               std::to_string(lat_min) + "," + std::to_string(lat_max) + 
                               "], longitude must be [" + 
                               std::to_string(lon_min) + "," + std::to_string(lon_max) + "]");
    }
    
    try {
        auto weather_data = weather_api_->GetWeatherByCoordinates(lat, lon);
        
        // 构建坐标天气结果
        json result;
        result["weather"] = weather_data.ToJson();
        result["coordinates"] = {{"latitude", lat}, {"longitude", lon}};
        result["summary"] = weather_data.city + "(" + std::to_string(lat) + "," + 
                           std::to_string(lon) + ")今天" + weather_data.description + 
                           "，气温" + std::to_string(static_cast<int>(weather_data.temperature)) + "℃";
        
        return CreateSuccessResult(result);
        
    } catch (const std::exception& e) {
        return CreateErrorResult("Failed to get weather data: " + std::string(e.what()));
    }
}

// 创建错误结果
ToolResult WeatherMcpServer::CreateErrorResult(const std::string& error_message) const {
    ToolResult result;
    result.type = "text";
    result.content = json{{"error", error_message}};
    result.is_error = true;
    return result;
}

// 创建成功结果
ToolResult WeatherMcpServer::CreateSuccessResult(const json& data) const {
    ToolResult result;
    result.type = "text";
    result.content = data;
    result.is_error = false;
    return result;
}

} // namespace mcp