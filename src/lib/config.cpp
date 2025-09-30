#include "mcp/config.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace mcp {

Config& Config::GetInstance() {
    static Config instance;
    return instance;
}

bool Config::LoadFromFile(const std::string& config_file_path) {
    config_file_path_ = config_file_path;
    
    try {
        std::ifstream config_file(config_file_path);
        if (!config_file.is_open()) {
            std::cerr << "无法打开配置文件: " << config_file_path << std::endl;
            return false;
        }

        config_file >> config_data_;
        config_file.close();

        if (!ValidateConfig()) {
            std::cerr << "配置文件验证失败" << std::endl;
            return false;
        }

        loaded_ = true;
        std::cout << "配置文件加载成功: " << config_file_path << std::endl;
        return true;

    } catch (const json::parse_error& e) {
        std::cerr << "配置文件解析错误: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "加载配置文件时出错: " << e.what() << std::endl;
        return false;
    }
}

bool Config::ValidateConfig() const {
    // 检查必要的配置节是否存在
    if (!config_data_.contains("server")) {
        std::cerr << "缺少 'server' 配置节" << std::endl;
        return false;
    }

    if (!config_data_.contains("logging")) {
        std::cerr << "缺少 'logging' 配置节" << std::endl;
        return false;
    }

    if (!config_data_.contains("weather_api")) {
        std::cerr << "缺少 'weather_api' 配置节" << std::endl;
        return false;
    }

    // 检查端口范围
    int port = config_data_["server"].value("port", 8080);
    if (port < 1 || port > 65535) {
        std::cerr << "服务器端口必须在 1-65535 范围内: " << port << std::endl;
        return false;
    }

    return true;
}

void Config::SetDefaults() {
    // 如果配置文件缺少某些值，设置默认值
    if (!config_data_.contains("server")) {
        config_data_["server"] = json::object();
    }
    
    auto& server = config_data_["server"];
    if (!server.contains("port")) server["port"] = 8080;
    if (!server.contains("host")) server["host"] = "0.0.0.0";
    if (!server.contains("cors_enabled")) server["cors_enabled"] = true;
}

// ===== 服务器配置实现 =====
int Config::GetServerPort() const {
    return config_data_["server"].value("port", 8080);
}

std::string Config::GetServerHost() const {
    return config_data_["server"].value("host", std::string("0.0.0.0"));
}

bool Config::GetServerCorsEnabled() const {
    return config_data_["server"].value("cors_enabled", true);
}

int Config::GetServerRequestBufferSize() const {
    return config_data_["server"].value("request_buffer_size", 8192);
}

int Config::GetServerListenBacklog() const {
    return config_data_["server"].value("listen_backlog", 10);
}

int Config::GetServerShutdownCheckIntervalMs() const {
    return config_data_["server"].value("shutdown_check_interval_ms", 100);
}

int Config::GetServerSocketTimeoutMs() const {
    return config_data_["server"].value("socket_timeout_ms", 10);
}

// ===== 日志配置实现 =====
std::string Config::GetLoggingLoggerName() const {
    return config_data_["logging"].value("logger_name", std::string("mcp_server"));
}

std::string Config::GetLoggingLogFilePath() const {
    return config_data_["logging"].value("log_file_path", std::string("logs/mcp_server.log"));
}

int Config::GetLoggingMaxFileSize() const {
    return config_data_["logging"].value("max_file_size", 5242880); // 5MB
}

int Config::GetLoggingMaxFiles() const {
    return config_data_["logging"].value("max_files", 3);
}

bool Config::GetLoggingConsoleOutput() const {
    return config_data_["logging"].value("console_output", true);
}

std::string Config::GetLoggingDefaultLevel() const {
    return config_data_["logging"].value("default_level", std::string("info"));
}

std::string Config::GetLoggingFlushLevel() const {
    return config_data_["logging"].value("flush_level", std::string("warn"));
}

// ===== 天气API配置实现 =====
std::string Config::GetOpenWeatherMapBaseUrl() const {
    return config_data_["weather_api"]["openweathermap"].value("base_url", std::string("https://api.openweathermap.org/data/2.5/"));
}

int Config::GetOpenWeatherMapTimeoutSeconds() const {
    return config_data_["weather_api"]["openweathermap"].value("timeout_seconds", 30);
}

int Config::GetOpenWeatherMapConnectTimeoutSeconds() const {
    return config_data_["weather_api"]["openweathermap"].value("connect_timeout_seconds", 10);
}

// ===== 模拟天气配置实现 =====
std::pair<double, double> Config::GetMockTemperatureRange() const {
    auto range = config_data_["weather_api"]["mock"]["temperature_range"];
    return {range.value("min", -10.0), range.value("max", 35.0)};
}

std::pair<double, double> Config::GetMockHumidityRange() const {
    auto range = config_data_["weather_api"]["mock"]["humidity_range"];
    return {range.value("min", 30.0), range.value("max", 90.0)};
}

std::pair<double, double> Config::GetMockPressureRange() const {
    auto range = config_data_["weather_api"]["mock"]["pressure_range"];
    return {range.value("min", 980.0), range.value("max", 1030.0)};
}

std::pair<double, double> Config::GetMockWindSpeedRange() const {
    auto range = config_data_["weather_api"]["mock"]["wind_speed_range"];
    return {range.value("min", 0.0), range.value("max", 15.0)};
}

std::pair<double, double> Config::GetMockForecastTempRange() const {
    auto range = config_data_["weather_api"]["mock"]["forecast_temp_range"];
    return {range.value("min", -5.0), range.value("max", 30.0)};
}

std::string Config::GetMockDefaultCountry() const {
    return config_data_["weather_api"]["mock"].value("default_country", std::string("CN"));
}

std::vector<std::string> Config::GetMockWeatherDescriptions() const {
    auto descriptions = config_data_["weather_api"]["mock"].value("weather_descriptions", json::array());
    std::vector<std::string> result;
    for (const auto& desc : descriptions) {
        result.push_back(desc.get<std::string>());
    }
    if (result.empty()) {
        // 默认描述
        result = {"clear sky", "few clouds", "scattered clouds", "broken clouds",
                 "shower rain", "rain", "thunderstorm", "snow", "mist"};
    }
    return result;
}

std::vector<std::string> Config::GetMockWeatherIcons() const {
    auto icons = config_data_["weather_api"]["mock"].value("weather_icons", json::array());
    std::vector<std::string> result;
    for (const auto& icon : icons) {
        result.push_back(icon.get<std::string>());
    }
    if (result.empty()) {
        // 默认图标
        result = {"01d", "02d", "03d", "04d", "09d", "10d", "11d", "13d", "50d"};
    }
    return result;
}

// ===== MCP协议配置实现 =====
std::string Config::GetMcpProtocolVersion() const {
    return config_data_["mcp"].value("protocol_version", std::string("2024-11-05"));
}

std::string Config::GetMcpVersion() const {
    return config_data_["mcp"].value("version", std::string("1.0.0"));
}

std::string Config::GetMcpName() const {
    return config_data_["mcp"].value("name", std::string("MCP Weather Server"));
}

int Config::GetMcpErrorCode(const std::string& error_type) const {
    auto error_codes = config_data_["mcp"]["error_codes"];
    
    if (error_type == "parse_error") {
        return error_codes.value("parse_error", -32700);
    } else if (error_type == "invalid_request") {
        return error_codes.value("invalid_request", -32600);
    } else if (error_type == "method_not_found") {
        return error_codes.value("method_not_found", -32601);
    } else if (error_type == "invalid_params") {
        return error_codes.value("invalid_params", -32602);
    } else if (error_type == "internal_error") {
        return error_codes.value("internal_error", -32603);
    }
    
    return -32603; // 默认返回内部错误
}

// ===== 天气规则配置实现 =====
double Config::GetWeatherRulesHotThreshold() const {
    return config_data_["weather_rules"].value("hot_temperature_threshold", 25.0);
}

double Config::GetWeatherRulesColdThreshold() const {
    return config_data_["weather_rules"].value("cold_temperature_threshold", 10.0);
}

std::pair<double, double> Config::GetWeatherRulesLatitudeRange() const {
    auto range = config_data_["weather_rules"]["coordinate_validation"]["latitude_range"];
    return {range.value("min", -90.0), range.value("max", 90.0)};
}

std::pair<double, double> Config::GetWeatherRulesLongitudeRange() const {
    auto range = config_data_["weather_rules"]["coordinate_validation"]["longitude_range"];
    return {range.value("min", -180.0), range.value("max", 180.0)};
}

int Config::GetWeatherRulesForecastMaxDays() const {
    return config_data_["weather_rules"].value("forecast_max_days", 5);
}

double Config::GetWeatherRulesKelvinToCelsiusOffset() const {
    return config_data_["weather_rules"].value("kelvin_to_celsius_offset", 273.15);
}

// ===== HTTP配置实现 =====
std::string Config::GetHttpResponseContentType() const {
    return config_data_["http"].value("response_content_type", std::string("application/json"));
}

int Config::GetHttpSseReconnectDelaySeconds() const {
    return config_data_["http"].value("sse_reconnect_delay_seconds", 5);
}

int Config::GetHttpStatusCode(const std::string& status_type) const {
    auto status_codes = config_data_["http"]["status_codes"];
    
    if (status_type == "ok") {
        return status_codes.value("ok", 200);
    } else if (status_type == "bad_request") {
        return status_codes.value("bad_request", 400);
    } else if (status_type == "not_found") {
        return status_codes.value("not_found", 404);
    } else if (status_type == "internal_error") {
        return status_codes.value("internal_error", 500);
    }
    
    return 500; // 默认返回内部错误
}

std::string Config::GetHttpCorsHeader(const std::string& header_type) const {
    auto cors_headers = config_data_["http"]["cors_headers"];
    
    if (header_type == "allow_origin") {
        return cors_headers.value("allow_origin", std::string("*"));
    } else if (header_type == "allow_methods") {
        return cors_headers.value("allow_methods", std::string("GET, POST, PUT, DELETE, OPTIONS"));
    } else if (header_type == "allow_headers") {
        return cors_headers.value("allow_headers", std::string("Content-Type, Authorization"));
    }
    
    return "";
}

// ===== 客户端配置实现 =====
int Config::GetClientDefaultTimeoutSeconds() const {
    return config_data_["client"].value("default_timeout_seconds", 30);
}

int Config::GetClientDefaultConnectTimeoutSeconds() const {
    return config_data_["client"].value("default_connect_timeout_seconds", 10);
}

} // namespace mcp