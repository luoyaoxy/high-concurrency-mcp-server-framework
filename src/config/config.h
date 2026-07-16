// =============================================
// MCP 配置模块头文件
// 提供单例类 Config，用于加载与访问配置
// - 支持从 JSON 文件加载
// - 提供基础的有效性校验与默认值设置
// - 线程外部请自行保证并发访问安全（本类不做同步）
// =============================================
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>

#include <cstddef>

namespace mcp {

using json = nlohmann::json;

// Config: 配置管理单例
// 用途：
//  - 通过 LoadFromFile 加载配置
//  - 通过 GetServerPort 等方法读取配置项
//  - 通过 IsLoaded 判断当前是否已成功加载
class Config {
public:
    // 获取全局唯一实例（懒加载/静态局部变量方式）
    static Config& GetInstance();

    // 从给定路径加载 JSON 配置文件
    // 返回：true 表示加载并校验成功；false 表示失败（文件不存在/解析失败/校验失败）
    bool LoadFromFile(const std::string& config_file_path);

    // 配置是否已成功加载
    bool IsLoaded() const { return loaded_; }

    // 读取服务器端口（要求在校验通过后使用）
    // 若未加载或配置不合法，建议调用方在外层处理
    int GetServerPort() const;

    std::size_t GetWorkerThreads() const;
    std::size_t GetMaxPendingTasks() const;
    int GetRequestTimeoutMs() const;

    std::size_t GetToolWorkers() const;
    std::size_t GetToolMaxPendingTasks() const;

    std::size_t GetToolCircuitFailureThreshold() const;
    int GetToolCircuitOpenMs() const;

    std::size_t GetResourceWorkers() const;
    std::size_t GetResourceMaxPendingTasks() const;

    std::size_t GetPromptWorkers() const;
    std::size_t GetPromptMaxPendingTasks() const;

    // SSE 长连接与单客户端事件积压的限制。
    std::size_t GetSseMaxClients() const;
    std::size_t GetSseMaxPendingEventsPerClient() const;
    std::size_t GetSseReplayBufferEvents() const;

    // 独立 SSE HTTP 服务的监听端口与专用线程数。
    int GetSsePort() const;
    std::size_t GetSseWorkers() const;

    // ===================================================================
    // 日志配置获取方法
    // ===================================================================

    // 获取日志文件路径
    std::string GetLogFilePath() const;

    // 获取日志级别（返回字符串：trace, debug, info, warn, error, critical）
    std::string GetLogLevel() const;

    // 获取单个日志文件最大大小（字节）
    size_t GetLogFileSize() const;

    // 获取日志文件数量
    int GetLogFileCount() const;

    // 是否输出到控制台
    bool GetLogConsoleOutput() const;

    // ===================================================================
    // 🔧 扩展位置：在这里添加新配置字段的获取方法
    // ===================================================================
    // 示例：
    // std::string GetServerHost() const;
    // std::string GetDatabaseUrl() const;
    // ===================================================================

private:
    // 单例构造与析构禁用拷贝
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // 校验当前 config_data_ 内容是否合法（端口范围、必要字段等）
    bool ValidateConfig() const;

    // 为缺失字段设置默认值（不会覆盖已存在的有效字段）
    void SetDefaults();

    // 原始 JSON 配置数据
    json config_data_;
    // 是否成功加载标记
    bool loaded_ = false;
    // 最近一次加载的配置文件路径（可用于调试/日志）
    std::string config_file_path_;
};


// 便捷宏：获取 Config 单例引用
#define MCP_CONFIG Config::GetInstance()

} // namespace mcp
