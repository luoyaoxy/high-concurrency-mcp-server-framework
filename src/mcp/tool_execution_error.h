#pragma once

#include <stdexcept>
#include <string>

namespace mcp {

// 表示网络临时错误、服务暂不可用等允许自动重试的工具失败。
class RetryableToolError : public std::runtime_error {
public:
    explicit RetryableToolError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace mcp
