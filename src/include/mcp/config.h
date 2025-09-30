#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>

namespace mcp {

using json = nlohmann::json;

/**
 * 配置管理类 - 单例模式
 * 负责加载和管理服务器配置
 */
class Config {
public:
    /**
     * 获取配置管理器实例
     */
    static Config& GetInstance();

    /**
     * 从配置文件加载配置
     * @param config_file_path 配置文件路径
     * @return 是否加载成功
     */
    bool LoadFromFile(const std::string& config_file_path);

    /**
     * 检查配置是否已加载
     */
    bool IsLoaded() const { return loaded_; }

    // ===== 服务器配置 =====
    int GetServerPort() const;
    std::string GetServerHost() const;
    bool GetServerCorsEnabled() const;
    int GetServerRequestBufferSize() const;
    int GetServerListenBacklog() const;
    int GetServerShutdownCheckIntervalMs() const;
    int GetServerSocketTimeoutMs() const;

    // ===== 日志配置 =====
    std::string GetLoggingLoggerName() const;
    std::string GetLoggingLogFilePath() const;
    int GetLoggingMaxFileSize() const;
    int GetLoggingMaxFiles() const;
    bool GetLoggingConsoleOutput() const;
    std::string GetLoggingDefaultLevel() const;
    std::string GetLoggingFlushLevel() const;

    // ===== 天气API配置 =====
    std::string GetOpenWeatherMapBaseUrl() const;
    int GetOpenWeatherMapTimeoutSeconds() const;
    int GetOpenWeatherMapConnectTimeoutSeconds() const;

    // ===== 模拟天气配置 =====
    std::pair<double, double> GetMockTemperatureRange() const;
    std::pair<double, double> GetMockHumidityRange() const;
    std::pair<double, double> GetMockPressureRange() const;
    std::pair<double, double> GetMockWindSpeedRange() const;
    std::pair<double, double> GetMockForecastTempRange() const;
    std::string GetMockDefaultCountry() const;
    std::vector<std::string> GetMockWeatherDescriptions() const;
    std::vector<std::string> GetMockWeatherIcons() const;

    // ===== MCP协议配置 =====
    std::string GetMcpProtocolVersion() const;
    std::string GetMcpVersion() const;
    std::string GetMcpName() const;
    int GetMcpErrorCode(const std::string& error_type) const;

    // ===== 天气规则配置 =====
    double GetWeatherRulesHotThreshold() const;
    double GetWeatherRulesColdThreshold() const;
    std::pair<double, double> GetWeatherRulesLatitudeRange() const;
    std::pair<double, double> GetWeatherRulesLongitudeRange() const;
    int GetWeatherRulesForecastMaxDays() const;
    double GetWeatherRulesKelvinToCelsiusOffset() const;

    // ===== HTTP配置 =====
    std::string GetHttpResponseContentType() const;
    int GetHttpSseReconnectDelaySeconds() const;
    int GetHttpStatusCode(const std::string& status_type) const;
    std::string GetHttpCorsHeader(const std::string& header_type) const;

    // ===== 客户端配置 =====
    int GetClientDefaultTimeoutSeconds() const;
    int GetClientDefaultConnectTimeoutSeconds() const;

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /**
     * 验证配置的有效性
     */
    bool ValidateConfig() const;

    /**
     * 设置默认值
     */
    void SetDefaults();

    json config_data_;
    bool loaded_ = false;
    std::string config_file_path_;
};

/**
 * 便捷宏定义
 */
#define MCP_CONFIG Config::GetInstance()

} // namespace mcp