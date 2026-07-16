#pragma once

#include "jsonrpc_task.h"

#include <optional>
#include <string>

namespace mcp {

// 执行阶段查询请求取消与截止时间的上下文。
class JsonRpcRequestContext {
public:
    using Clock = JsonRpcTask::Clock;

    JsonRpcRequestContext(
        std::string trace_id,
        std::optional<Clock::time_point> deadline,
        CancellationToken cancellation
    );

    const std::string& trace_id() const;
    const std::optional<Clock::time_point>& deadline() const;
    const CancellationToken& cancellation_token() const;

    bool is_cancelled() const;
    bool deadline_exceeded() const;
    bool should_stop() const;

private:
    std::string trace_id_;
    std::optional<Clock::time_point> deadline_;
    CancellationToken cancellation_;
};

// 返回当前 worker 线程正在执行的请求上下文；非 worker 线程返回 nullptr。
const JsonRpcRequestContext* current_jsonrpc_request_context();

// 在一个作用域内把 context 绑定到当前 worker 线程。
class JsonRpcRequestContextScope {
public:
    explicit JsonRpcRequestContextScope(
        const JsonRpcRequestContext& context
    );

    ~JsonRpcRequestContextScope();

    JsonRpcRequestContextScope(
        const JsonRpcRequestContextScope&
    ) = delete;

    JsonRpcRequestContextScope& operator=(
        const JsonRpcRequestContextScope&
    ) = delete;

private:
    const JsonRpcRequestContext* previous_context_;
};

} // namespace mcp
