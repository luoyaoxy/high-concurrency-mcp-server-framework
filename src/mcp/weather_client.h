#pragma once

#include <functional>
#include <stdexcept>
#include <string>

namespace mcp {

// 取消属于请求生命周期，不应被当成可重试的天气服务故障。
class WeatherRequestCancelled : public std::runtime_error {
public:
    explicit WeatherRequestCancelled(const std::string& message)
        : std::runtime_error(message) {}
};

struct WeatherReport {
    std::string requested_city;
    std::string resolved_city;
    std::string country;
    std::string observed_at;
    double temperature_c = 0.0;
    double apparent_temperature_c = 0.0;
    int humidity_percent = 0;
    double wind_speed_kmh = 0.0;
    int weather_code = 0;
    std::string condition;
};

// Open-Meteo 的城市解析与当前天气查询客户端。
class WeatherClient {
public:
    using HttpGet = std::function<std::string(const std::string& url)>;
    using StopRequested = std::function<bool()>;

    // 注入 HttpGet 可让单元测试不依赖真实网络。
    explicit WeatherClient(
        HttpGet http_get = {},
        StopRequested stop_requested = {}
    );

    WeatherReport get_current_weather(const std::string& city) const;

private:
    std::string get(const std::string& url) const;
    void throw_if_stopped() const;

    static std::string url_encode(const std::string& value);
    static std::string weather_condition(int weather_code);

    HttpGet http_get_;
    StopRequested stop_requested_;
};

} // namespace mcp
