#include "weather_client.h"
#include "tool_execution_error.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace mcp;

TEST(WeatherClientTest, ResolvesCityAndParsesCurrentWeather) {
    std::vector<std::string> requested_urls;

    WeatherClient client([&requested_urls](const std::string& url) {
        requested_urls.push_back(url);

        if (url.find("geocoding-api.open-meteo.com") != std::string::npos) {
            return R"({
                "results": [{
                    "name": "北京",
                    "country": "中国",
                    "latitude": 39.9042,
                    "longitude": 116.4074
                }]
            })";
        }

        return R"({
            "current": {
                "time": "2026-07-16T12:00",
                "temperature_2m": 29.5,
                "apparent_temperature": 31.2,
                "relative_humidity_2m": 66,
                "weather_code": 61,
                "wind_speed_10m": 12.4
            }
        })";
    });

    const WeatherReport weather = client.get_current_weather("北京");

    ASSERT_EQ(requested_urls.size(), 2u);
    EXPECT_NE(requested_urls[0].find("name=%E5%8C%97%E4%BA%AC"), std::string::npos);
    EXPECT_NE(requested_urls[1].find("latitude=39.9042"), std::string::npos);
    EXPECT_EQ(weather.resolved_city, "北京");
    EXPECT_EQ(weather.country, "中国");
    EXPECT_EQ(weather.condition, "雨");
    EXPECT_DOUBLE_EQ(weather.temperature_c, 29.5);
    EXPECT_EQ(weather.humidity_percent, 66);
}

TEST(WeatherClientTest, ReturnsBusinessErrorWhenCityIsNotFound) {
    WeatherClient client([](const std::string&) {
        return R"({"results": []})";
    });

    EXPECT_THROW(
        client.get_current_weather("not-a-real-city"),
        std::runtime_error
    );
}

TEST(WeatherClientTest, KeepsTemporaryFailuresRetryable) {
    WeatherClient client([](const std::string&) -> std::string {
        throw RetryableToolError("temporary weather service failure");
    });

    EXPECT_THROW(
        client.get_current_weather("北京"),
        RetryableToolError
    );
}

TEST(WeatherClientTest, StopsBeforeMakingNetworkRequest) {
    bool requested = false;
    WeatherClient client(
        [&requested](const std::string&) {
            requested = true;
            return std::string{};
        },
        [] { return true; }
    );

    EXPECT_THROW(
        client.get_current_weather("北京"),
        WeatherRequestCancelled
    );
    EXPECT_FALSE(requested);
}
