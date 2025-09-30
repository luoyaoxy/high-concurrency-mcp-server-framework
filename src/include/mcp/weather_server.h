#pragma once

#include "mcp_protocol.h"
#include "weather_api.h"
#include <memory>

namespace mcp {

// 天气 MCP 服务器类，提供天气查询相关的 MCP 工具和资源
class WeatherMcpServer : public McpServer {
public:
    // 构造函数：接受天气 API 实例
    WeatherMcpServer(std::unique_ptr<WeatherApi> weather_api);
    ~WeatherMcpServer() = default;

protected:
    // 实现工具执行逻辑
    ToolResult ExecuteTool(const std::string& name, const json& arguments) override;
    // 实现资源读取逻辑
    json ReadResource(const std::string& uri) override;

private:
    std::unique_ptr<WeatherApi> weather_api_;  // 天气 API 实例
    
    // 初始化工具配置
    void SetupTools();
    // 初始化资源配置
    void SetupResources();
    
    // 处理获取当前天气的工具调用
    ToolResult HandleGetCurrentWeather(const json& arguments);
    // 处理获取天气预报的工具调用
    ToolResult HandleGetForecast(const json& arguments);
    // 处理根据坐标获取天气的工具调用
    ToolResult HandleGetWeatherByCoordinates(const json& arguments);
    
    // 创建错误结果
    ToolResult CreateErrorResult(const std::string& error_message) const;
    // 创建成功结果
    ToolResult CreateSuccessResult(const json& data) const;
};

} // namespace mcp