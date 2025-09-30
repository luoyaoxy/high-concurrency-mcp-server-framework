#include "mcp/weather_api.h"
#include "mcp/http_transport.h"
#include "mcp/config.h"
#include <random>
#include <ctime>
#include <sstream>

namespace mcp {

json WeatherData::ToJson() const {
    json j;
    j["city"] = city;
    j["country"] = country;
    j["temperature"] = temperature;
    j["description"] = description;
    j["humidity"] = humidity;
    j["pressure"] = pressure;
    j["wind_speed"] = wind_speed;
    j["icon"] = icon;
    j["timestamp"] = timestamp;
    return j;
}

WeatherData WeatherData::FromJson(const json& j) {
    WeatherData data;
    data.city = j.value("city", "");
    data.country = j.value("country", "");
    data.temperature = j.value("temperature", 0.0);
    data.description = j.value("description", "");
    data.humidity = j.value("humidity", 0.0);
    data.pressure = j.value("pressure", 0.0);
    data.wind_speed = j.value("wind_speed", 0.0);
    data.icon = j.value("icon", "");
    data.timestamp = j.value("timestamp", 0L);
    return data;
}

json ForecastData::ToJson() const {
    json j;
    j["date"] = date;
    j["temp_min"] = temp_min;
    j["temp_max"] = temp_max;
    j["description"] = description;
    j["icon"] = icon;
    j["humidity"] = humidity;
    j["precipitation_chance"] = precipitation_chance;
    return j;
}

ForecastData ForecastData::FromJson(const json& j) {
    ForecastData data;
    data.date = j.value("date", "");
    data.temp_min = j.value("temp_min", 0.0);
    data.temp_max = j.value("temp_max", 0.0);
    data.description = j.value("description", "");
    data.icon = j.value("icon", "");
    data.humidity = j.value("humidity", 0.0);
    data.precipitation_chance = j.value("precipitation_chance", 0.0);
    return data;
}

WeatherApi::WeatherApi(const std::string& api_key) : api_key_(api_key) {}

OpenWeatherMapApi::OpenWeatherMapApi(const std::string& api_key) 
    : WeatherApi(api_key), base_url_(MCP_CONFIG.GetOpenWeatherMapBaseUrl()) {
    http_client_ = std::make_unique<HttpClient>();
    http_client_->SetTimeout(MCP_CONFIG.GetOpenWeatherMapTimeoutSeconds());
    http_client_->SetConnectTimeout(MCP_CONFIG.GetOpenWeatherMapConnectTimeoutSeconds());
}

WeatherData OpenWeatherMapApi::GetCurrentWeather(const std::string& city) {
    std::string url = BuildUrl("weather", city);
    json response = MakeRequest(url);
    
    WeatherData data;
    data.city = response["name"];
    data.country = response["sys"]["country"];
    data.temperature = response["main"]["temp"].get<double>() - MCP_CONFIG.GetWeatherRulesKelvinToCelsiusOffset();
    data.description = response["weather"][0]["description"];
    data.humidity = response["main"]["humidity"];
    data.pressure = response["main"]["pressure"];
    data.wind_speed = response.value("wind", json::object()).value("speed", 0.0);
    data.icon = response["weather"][0]["icon"];
    data.timestamp = std::time(nullptr);
    
    return data;
}

std::vector<ForecastData> OpenWeatherMapApi::GetForecast(const std::string& city, int days) {
    std::string url = BuildUrl("forecast", city);
    json response = MakeRequest(url);
    
    std::vector<ForecastData> forecast;
    
    if (response.contains("list")) {
        auto list = response["list"];
        std::string current_date;
        
        for (size_t i = 0; i < list.size() && forecast.size() < static_cast<size_t>(days); ++i) {
            auto item = list[i];
            std::string dt_txt = item["dt_txt"];
            std::string date = dt_txt.substr(0, 10); // Extract date part
            
            if (date != current_date) {
                current_date = date;
                
                ForecastData data;
                data.date = date;
                double offset = MCP_CONFIG.GetWeatherRulesKelvinToCelsiusOffset();
                data.temp_min = item["main"]["temp_min"].get<double>() - offset;
                data.temp_max = item["main"]["temp_max"].get<double>() - offset;
                data.description = item["weather"][0]["description"];
                data.icon = item["weather"][0]["icon"];
                data.humidity = item["main"]["humidity"];
                data.precipitation_chance = item.value("pop", 0.0) * 100;
                
                forecast.push_back(data);
            }
        }
    }
    
    return forecast;
}

WeatherData OpenWeatherMapApi::GetWeatherByCoordinates(double lat, double lon) {
    std::string url = BuildUrl("weather", "", lat, lon);
    json response = MakeRequest(url);
    
    WeatherData data;
    data.city = response["name"];
    data.country = response["sys"]["country"];
    data.temperature = response["main"]["temp"].get<double>() - MCP_CONFIG.GetWeatherRulesKelvinToCelsiusOffset();
    data.description = response["weather"][0]["description"];
    data.humidity = response["main"]["humidity"];
    data.pressure = response["main"]["pressure"];
    data.wind_speed = response.value("wind", json::object()).value("speed", 0.0);
    data.icon = response["weather"][0]["icon"];
    data.timestamp = std::time(nullptr);
    
    return data;
}

std::string OpenWeatherMapApi::BuildUrl(const std::string& endpoint, const std::string& city, 
                                        double lat, double lon) const {
    std::ostringstream url;
    url << base_url_ << endpoint << "?";
    
    if (!city.empty()) {
        url << "q=" << city;
    } else {
        url << "lat=" << lat << "&lon=" << lon;
    }
    
    url << "&appid=" << api_key_;
    
    return url.str();
}

json OpenWeatherMapApi::MakeRequest(const std::string& url) const {
    auto response = http_client_->get(url);
    
    if (!response.IsSuccess()) {
        throw std::runtime_error("HTTP request failed: " + std::to_string(response.status_code) + 
                               " " + response.error_message);
    }
    
    try {
        return json::parse(response.body);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSON response: " + std::string(e.what()));
    }
}

MockWeatherApi::MockWeatherApi() : WeatherApi("mock") {}

WeatherData MockWeatherApi::GetCurrentWeather(const std::string& city) {
    return GenerateMockWeather(city);
}

std::vector<ForecastData> MockWeatherApi::GetForecast(const std::string& city, int days) {
    return GenerateMockForecast(city, days);
}

WeatherData MockWeatherApi::GetWeatherByCoordinates(double lat, double lon) {
    std::string mock_city = "City(" + std::to_string(lat) + "," + std::to_string(lon) + ")";
    return GenerateMockWeather(mock_city);
}

WeatherData MockWeatherApi::GenerateMockWeather(const std::string& city) const {
    std::random_device rd;
    std::mt19937 gen(rd());
    auto temp_range = MCP_CONFIG.GetMockTemperatureRange();
    auto humidity_range = MCP_CONFIG.GetMockHumidityRange();
    auto pressure_range = MCP_CONFIG.GetMockPressureRange();
    auto wind_range = MCP_CONFIG.GetMockWindSpeedRange();
    
    std::uniform_real_distribution<> temp_dist(temp_range.first, temp_range.second);
    std::uniform_real_distribution<> humidity_dist(humidity_range.first, humidity_range.second);
    std::uniform_real_distribution<> pressure_dist(pressure_range.first, pressure_range.second);
    std::uniform_real_distribution<> wind_dist(wind_range.first, wind_range.second);
    
    auto descriptions = MCP_CONFIG.GetMockWeatherDescriptions();
    auto icons = MCP_CONFIG.GetMockWeatherIcons();
    
    std::uniform_int_distribution<> desc_dist(0, descriptions.size() - 1);
    
    WeatherData data;
    data.city = city;
    data.country = MCP_CONFIG.GetMockDefaultCountry();
    data.temperature = temp_dist(gen);
    data.description = descriptions[desc_dist(gen)];
    data.humidity = humidity_dist(gen);
    data.pressure = pressure_dist(gen);
    data.wind_speed = wind_dist(gen);
    data.icon = icons[desc_dist(gen)];
    data.timestamp = std::time(nullptr);
    
    return data;
}

std::vector<ForecastData> MockWeatherApi::GenerateMockForecast(const std::string& city, int days) const {
    std::vector<ForecastData> forecast;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    auto forecast_temp_range = MCP_CONFIG.GetMockForecastTempRange();
    auto humidity_range = MCP_CONFIG.GetMockHumidityRange();
    
    std::uniform_real_distribution<> temp_dist(forecast_temp_range.first, forecast_temp_range.second);
    std::uniform_real_distribution<> humidity_dist(humidity_range.first, humidity_range.second);
    std::uniform_real_distribution<> precip_dist(0.0, 100.0);
    
    auto descriptions = MCP_CONFIG.GetMockWeatherDescriptions();
    auto icons = MCP_CONFIG.GetMockWeatherIcons();
    
    std::uniform_int_distribution<> desc_dist(0, descriptions.size() - 1);
    
    for (int i = 1; i <= days; ++i) {
        ForecastData data;
        
        // Generate date string
        std::time_t now = std::time(nullptr);
        std::time_t future_time = now + (i * 24 * 60 * 60); // Add i days
        std::tm* tm_future = std::localtime(&future_time);
        
        char date_buf[11];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_future);
        data.date = date_buf;
        
        double base_temp = temp_dist(gen);
        data.temp_min = base_temp - 5;
        data.temp_max = base_temp + 5;
        data.description = descriptions[desc_dist(gen)];
        data.icon = icons[desc_dist(gen)];
        data.humidity = humidity_dist(gen);
        data.precipitation_chance = precip_dist(gen);
        
        forecast.push_back(data);
    }
    
    return forecast;
}

} // namespace mcp