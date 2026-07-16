#include "weather_client.h"

#include "tool_execution_error.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <sstream>
#include <utility>

namespace mcp {
namespace {

using json = nlohmann::json;

constexpr long kConnectTimeoutMs = 1500;
constexpr long kRequestTimeoutMs = 2500;

void ensure_curl_initialized() {
    static std::once_flag initialized;
    static CURLcode init_result = CURLE_OK;

    std::call_once(initialized, [] {
        init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
    });

    if (init_result != CURLE_OK) {
        throw std::runtime_error("Failed to initialize libcurl");
    }
}

size_t write_response(
    char* data,
    size_t size,
    size_t count,
    void* user_data
) {
    auto* response = static_cast<std::string*>(user_data);
    response->append(data, size * count);
    return size * count;
}

int check_cancellation(
    void* user_data,
    curl_off_t,
    curl_off_t,
    curl_off_t,
    curl_off_t
) {
    const auto* stop_requested =
        static_cast<const WeatherClient::StopRequested*>(user_data);
    return *stop_requested && (*stop_requested)() ? 1 : 0;
}

std::string curl_get(
    const std::string& url,
    const WeatherClient::StopRequested& stop_requested
) {
    ensure_curl_initialized();

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw RetryableToolError("Failed to create weather HTTP client");
    }

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mcp-tutorial-weather/1.0");

    // 下载过程中持续检查 JSON-RPC 的取消或 deadline。
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, check_cancellation);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop_requested);

    const CURLcode code = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);

    if (
        code == CURLE_ABORTED_BY_CALLBACK &&
        stop_requested && stop_requested()
    ) {
        throw WeatherRequestCancelled("Weather request cancelled");
    }

    if (code != CURLE_OK) {
        throw RetryableToolError(
            std::string("Weather API transport failure: ") +
            curl_easy_strerror(code)
        );
    }

    if (status_code == 408 || status_code == 429 || status_code >= 500) {
        throw RetryableToolError(
            "Weather API temporarily unavailable (HTTP " +
            std::to_string(status_code) + ")"
        );
    }

    if (status_code < 200 || status_code >= 300) {
        throw std::runtime_error(
            "Weather API returned HTTP " + std::to_string(status_code)
        );
    }

    return response_body;
}

} // namespace

WeatherClient::WeatherClient(
    HttpGet http_get,
    StopRequested stop_requested
)
    : http_get_(std::move(http_get))
    , stop_requested_(std::move(stop_requested)) {}

WeatherReport WeatherClient::get_current_weather(
    const std::string& city
) const {
    if (city.empty()) {
        throw std::invalid_argument("city must not be empty");
    }

    throw_if_stopped();

    const std::string geocoding_url =
        "https://geocoding-api.open-meteo.com/v1/search?name=" +
        url_encode(city) +
        "&count=1&language=zh&format=json";

    const json geocoding = json::parse(get(geocoding_url));
    if (
        !geocoding.contains("results") ||
        !geocoding["results"].is_array() ||
        geocoding["results"].empty()
    ) {
        throw std::runtime_error("City not found: " + city);
    }

    const json& location = geocoding["results"].front();
    const double latitude = location.at("latitude").get<double>();
    const double longitude = location.at("longitude").get<double>();

    throw_if_stopped();

    std::ostringstream forecast_url;
    forecast_url
        << "https://api.open-meteo.com/v1/forecast?latitude="
        << latitude
        << "&longitude=" << longitude
        << "&current=temperature_2m,relative_humidity_2m,"
           "apparent_temperature,weather_code,wind_speed_10m"
        << "&wind_speed_unit=kmh&timezone=auto";

    const json forecast = json::parse(get(forecast_url.str()));
    const json& current = forecast.at("current");

    WeatherReport report;
    report.requested_city = city;
    report.resolved_city = location.value("name", city);
    report.country = location.value("country", "");
    report.observed_at = current.at("time").get<std::string>();
    report.temperature_c = current.at("temperature_2m").get<double>();
    report.apparent_temperature_c =
        current.at("apparent_temperature").get<double>();
    report.humidity_percent =
        current.at("relative_humidity_2m").get<int>();
    report.wind_speed_kmh = current.at("wind_speed_10m").get<double>();
    report.weather_code = current.at("weather_code").get<int>();
    report.condition = weather_condition(report.weather_code);
    return report;
}

std::string WeatherClient::get(const std::string& url) const {
    throw_if_stopped();

    if (http_get_) {
        return http_get_(url);
    }

    return curl_get(url, stop_requested_);
}

void WeatherClient::throw_if_stopped() const {
    if (stop_requested_ && stop_requested_()) {
        throw WeatherRequestCancelled("Weather request cancelled");
    }
}

std::string WeatherClient::url_encode(const std::string& value) {
    ensure_curl_initialized();

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("Failed to create weather URL encoder");
    }

    char* encoded = curl_easy_escape(
        curl,
        value.c_str(),
        static_cast<int>(value.size())
    );
    if (encoded == nullptr) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Failed to encode city name");
    }

    const std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

std::string WeatherClient::weather_condition(int weather_code) {
    switch (weather_code) {
        case 0: return "晴";
        case 1: return "大部晴朗";
        case 2: return "局部多云";
        case 3: return "阴";
        case 45:
        case 48: return "雾";
        case 51:
        case 53:
        case 55: return "毛毛雨";
        case 56:
        case 57: return "冻毛毛雨";
        case 61:
        case 63:
        case 65: return "雨";
        case 66:
        case 67: return "冻雨";
        case 71:
        case 73:
        case 75:
        case 77: return "雪";
        case 80:
        case 81:
        case 82: return "阵雨";
        case 85:
        case 86: return "阵雪";
        case 95: return "雷暴";
        case 96:
        case 99: return "雷暴伴冰雹";
        default: return "未知天气";
    }
}

} // namespace mcp
