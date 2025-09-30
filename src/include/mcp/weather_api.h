#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>

namespace mcp {

using json = nlohmann::json;

// 天气数据结构体，包含当前天气的详细信息
struct WeatherData {
    std::string city;          // 城市名称
    std::string country;       // 国家代码
    double temperature;        // 温度（摄氏度）
    std::string description;   // 天气描述
    double humidity;           // 湿度（百分比）
    double pressure;           // 气压（hPa）
    double wind_speed;         // 风速（m/s）
    std::string icon;          // 天气图标代码
    long timestamp;            // 时间戳

    json ToJson() const;                        // 转换为 JSON 格式
    static WeatherData FromJson(const json& j); // 从 JSON 格式解析
};

// 天气预报数据结构体，包含未来某天的天气信息
struct ForecastData {
    std::string date;              // 日期（YYYY-MM-DD 格式）
    double temp_min;               // 最低温度（摄氏度）
    double temp_max;               // 最高温度（摄氏度）
    std::string description;       // 天气描述
    std::string icon;              // 天气图标代码
    double humidity;               // 湿度（百分比）
    double precipitation_chance;   // 降水概率（百分比）

    json ToJson() const;                          // 转换为 JSON 格式
    static ForecastData FromJson(const json& j);  // 从 JSON 格式解析
};

// 天气 API 抽象基类，定义天气查询的通用接口
class WeatherApi {
public:
    WeatherApi(const std::string& api_key);
    virtual ~WeatherApi() = default;

    // 纯虚函数：获取指定城市的当前天气
    virtual WeatherData GetCurrentWeather(const std::string& city) = 0;
    // 纯虚函数：获取指定城市的天气预报
    virtual std::vector<ForecastData> GetForecast(const std::string& city, int days = 5) = 0;
    // 纯虚函数：根据经纬度获取当前天气
    virtual WeatherData GetWeatherByCoordinates(double lat, double lon) = 0;
    
protected:
    std::string api_key_;  // API 密钥
};

// OpenWeatherMap API 实现类，连接真实的天气服务
class OpenWeatherMapApi : public WeatherApi {
public:
    OpenWeatherMapApi(const std::string& api_key);

    // 实现获取当前天气功能
    WeatherData GetCurrentWeather(const std::string& city) override;
    // 实现获取天气预报功能
    std::vector<ForecastData> GetForecast(const std::string& city, int days = 5) override;
    // 实现根据坐标获取天气功能
    WeatherData GetWeatherByCoordinates(double lat, double lon) override;

private:
    std::string base_url_;                           // API 基础 URL
    std::unique_ptr<class HttpClient> http_client_;  // HTTP 客户端实例
    
    // 构建 API 请求 URL
    std::string BuildUrl(const std::string& endpoint, const std::string& city = "", 
                         double lat = 0.0, double lon = 0.0) const;
    // 发送 HTTP 请求并解析响应
    json MakeRequest(const std::string& url) const;
};

// 模拟天气 API 实现类，用于测试和演示
class MockWeatherApi : public WeatherApi {
public:
    MockWeatherApi();

    // 实现获取模拟当前天气功能
    WeatherData GetCurrentWeather(const std::string& city) override;
    // 实现获取模拟天气预报功能
    std::vector<ForecastData> GetForecast(const std::string& city, int days = 5) override;
    // 实现根据坐标获取模拟天气功能
    WeatherData GetWeatherByCoordinates(double lat, double lon) override;

private:
    // 生成模拟的天气数据
    WeatherData GenerateMockWeather(const std::string& city) const;
    // 生成模拟的天气预报数据
    std::vector<ForecastData> GenerateMockForecast(const std::string& city, int days) const;
};

} // namespace mcp