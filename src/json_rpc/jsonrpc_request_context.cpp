#include "jsonrpc_request_context.h"

#include <utility>

namespace mcp {

namespace {

// 每个 worker 线程独立保存自己的当前请求，互不共享。
thread_local const JsonRpcRequestContext* g_current_request_context =
    nullptr;

} // namespace

JsonRpcRequestContext::JsonRpcRequestContext(
    std::string trace_id,
    std::optional<Clock::time_point> deadline,
    CancellationToken cancellation
)
    : trace_id_(std::move(trace_id))
    , deadline_(deadline)
    , cancellation_(std::move(cancellation)) {}

const std::string& JsonRpcRequestContext::trace_id() const {
    return trace_id_;
}

const std::optional<JsonRpcRequestContext::Clock::time_point>&
JsonRpcRequestContext::deadline() const {
    return deadline_;
}

const CancellationToken& JsonRpcRequestContext::cancellation_token() const {
    return cancellation_;
}

bool JsonRpcRequestContext::is_cancelled() const {
    return cancellation_.is_cancelled();
}

bool JsonRpcRequestContext::deadline_exceeded() const {
    return deadline_.has_value() && Clock::now() > *deadline_;
}

bool JsonRpcRequestContext::should_stop() const {
    return is_cancelled() || deadline_exceeded();
}

const JsonRpcRequestContext* current_jsonrpc_request_context() {
    return g_current_request_context;
}

JsonRpcRequestContextScope::JsonRpcRequestContextScope(
    const JsonRpcRequestContext& context
)
    : previous_context_(g_current_request_context) {
    // 进入执行作用域时，绑定当前任务的 context。
    g_current_request_context = &context;
}

JsonRpcRequestContextScope::~JsonRpcRequestContextScope() {
    // 离开作用域时恢复旧值，防止线程池复用时泄漏到下一个任务。
    g_current_request_context = previous_context_;
}

} // namespace mcp
