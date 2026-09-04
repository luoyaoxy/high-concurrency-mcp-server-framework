#include "jsonrpc_worker_pool.h"
#include "jsonrpc_cancellation_registry.h"
#include "jsonrpc_metrics.h"
#include "jsonrpc_task.h"
#include "jsonrpc_request_context.h"
#include "logger.h"

#include <exception>
#include <future>
#include <stdexcept>
#include <utility>

#include <chrono>

namespace mcp {

/**
 * @brief 构造函数：绑定任务阻塞队列与RPC路由分发器
 * @param queue 有界阻塞任务队列，由 HTTP、stdio 等入口投递 JSON-RPC 任务
 * @param dispatcher RPC方法调度器，保存method与handler映射关系
 */
JsonRpcWorkerPool::JsonRpcWorkerPool(
    BoundedJsonRpcTaskQueue& queue,
    const JsonRpcDispatcher& dispatcher,
    JsonRpcCancellationRegistry& cancellation_registry,
    JsonRpcMetrics& metrics
)
    : queue_(queue)
    , dispatcher_(dispatcher)
    , cancellation_registry_(cancellation_registry)
    , metrics_(metrics) {}

/**
 * @brief 析构函数：自动调用stop，安全回收所有工作线程
 */
JsonRpcWorkerPool::~JsonRpcWorkerPool() {
    stop();
}

/**
 * @brief 启动线程池，创建指定数量的工作线程循环消费任务
 * @param worker_count 工作线程数量，必须大于0
 * @exception std::invalid_argument 线程数为0时抛出
 * @exception std::logic_error 池已启动 / 已停止无法重启时抛出
 */
void JsonRpcWorkerPool::start(std::size_t worker_count) {
    if (worker_count == 0) {
        throw std::invalid_argument(
            "Worker count must be greater than zero"
        );
    }

    // 生命周期互斥锁，防止并发启停线程池产生竞态
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    // 禁止重复启动
    if (started_) {
        throw std::logic_error("Worker pool has already started");
    }

    // 停止后的线程池不支持二次启动
    if (stopped_) {
        throw std::logic_error("Stopped worker pool cannot be restarted");
    }

    // 预分配线程容器内存，避免扩容开销
    workers_.reserve(worker_count);

    // 创建worker工作线程，绑定循环处理函数worker_loop
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers_.emplace_back(
            &JsonRpcWorkerPool::worker_loop,
            this,
            index
        );
    }

    started_ = true;

    MCP_LOG_INFO("JSON-RPC worker pool started with {} workers", worker_count);
}

/**
 * @brief 优雅停止线程池
 * 1. 关闭任务队列，不再接收新任务
 * 2. 等待队列中剩余任务全部消费完成
 * 3. 回收所有worker线程资源
 */
void JsonRpcWorkerPool::stop() {
    std::vector<std::thread> workers_to_join;

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        // 已停止直接返回，避免重复执行回收逻辑
        if (stopped_) {
            return;
        }

        // 标记队列关闭：新任务投递会失败，worker消费完现有任务后退出循环
        queue_.close();

        // 交换线程容器，释放锁后再join，缩短锁持有时间
        workers_to_join.swap(workers_);
        started_ = false;
        stopped_ = true;
    }

    // 阻塞等待所有工作线程执行完毕并回收资源
    for (std::thread& worker : workers_to_join) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    MCP_LOG_INFO("JSON-RPC worker pool stopped");
}

/**
 * @brief 获取当前线程池内工作线程数量
 * @return 存活worker线程个数
 */
std::size_t JsonRpcWorkerPool::worker_count() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return workers_.size();
}

/**
 * @brief 单个工作线程主循环函数
 * 持续从阻塞队列拉取 RPC 任务，执行后回填 promise，并按需触发完成回调
 * 供 HTTP 同步等待或 stdio 异步写回响应。
 * @param worker_index 当前工作线程编号，用于日志区分
 */
void JsonRpcWorkerPool::worker_loop(std::size_t worker_index) {
    MCP_LOG_DEBUG("JSON-RPC worker {} started", worker_index);

    while (true) {
        // 阻塞等待任务入队；队列关闭且无剩余任务时返回空指针
        JsonRpcTaskEnvelopePtr envelope = queue_.wait_pop();

        // 队列已关闭、无任务，线程退出循环终止
        if (!envelope) {
            break;
        }

        const auto started_at = std::chrono::steady_clock::now();
        metrics_.record_worker_started(envelope->task.lane);

        MCP_LOG_DEBUG(
            "[trace_id={}] Worker {} started: task_id={}, method={}",
            envelope->task.trace_id,
            worker_index,
            envelope->task.task_id,
            envelope->task.method
        );

        try {
            // 从任务字段创建本次执行可查询的请求上下文。
            JsonRpcRequestContext context(
                envelope->task.trace_id,
                envelope->task.deadline,
                envelope->task.cancellation
            );

            // 仅在本次 handler 调用期间绑定到当前 worker 线程。
            JsonRpcRequestContextScope context_scope(context);

            JsonRpcTaskResult result =
                execute_jsonrpc_task(envelope->task, dispatcher_);

            const auto cost_ms = std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::steady_clock::now() - started_at
            ).count();

            metrics_.record_worker_finished(
                envelope->task.lane,
                result,
                static_cast<std::uint64_t>(cost_ms)
            );

            // 执行完成后，再将结果交还给入口层。
            MCP_LOG_DEBUG(
                "[trace_id={}] Worker {} finished: task_id={}, method={}, cost_ms={}",
                envelope->task.trace_id,
                worker_index,
                envelope->task.task_id,
                envelope->task.method,
                cost_ms
            );

            // 保留 future 接口，供 HTTP 等同步入口等待结果。
            envelope->completion.set_value(result);

            // stdio 等异步入口可在任务完成时接收结果。
            if (envelope->on_completed.has_value()) {
                try {
                    (*envelope->on_completed)(std::move(result));
                } catch (const std::exception& e) {
                    MCP_LOG_ERROR(
                        "JSON-RPC completion callback failed: {}",
                        e.what()
                    );
                } catch (...) {
                    MCP_LOG_ERROR(
                        "JSON-RPC completion callback failed with unknown error"
                    );
                }
            }

        } catch (...) {
            MCP_LOG_ERROR(
                "[trace_id={}] Worker {} failed: task_id={}, method={}",
                envelope->task.trace_id,
                worker_index,
                envelope->task.task_id,
                envelope->task.method
            );

            JsonRpcTaskResult failed_result;
            if (!envelope->task.is_notification()) {
                JsonRpcResponse response;
                response.jsonrpc = kJsonRpcVersion;
                response.id = envelope->task.request_id.value_or(nullptr);
                response.error = JsonRpcError{
                    jsonrpc_errc::InternalError,
                    "Unhandled worker exception",
                    std::nullopt
                };
                failed_result = std::move(response);
            }

            const auto cost_ms = std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::steady_clock::now() - started_at
            ).count();

            metrics_.record_worker_finished(
                envelope->task.lane,
                failed_result,
                static_cast<std::uint64_t>(cost_ms)
            );

            try {
                envelope->completion.set_value(failed_result);
            } catch (const std::future_error& e) {
                MCP_LOG_ERROR(
                    "Worker {} failed to publish task result: {}",
                    worker_index,
                    e.what()
                );
            }

            // 即使发生未知异常，也必须通知异步入口完成收尾。
            if (envelope->on_completed.has_value()) {
                try {
                    (*envelope->on_completed)(std::move(failed_result));
                } catch (const std::exception& e) {
                    MCP_LOG_ERROR(
                        "JSON-RPC completion callback failed: {}",
                        e.what()
                    );
                } catch (...) {
                    MCP_LOG_ERROR(
                        "JSON-RPC completion callback failed with unknown error"
                    );
                }
            }
        }

        // 已完成的请求不再允许被取消，也不应继续占用登记表。
        if (envelope->task.request_id.has_value()) {
            cancellation_registry_.unregister_request(
                envelope->task.client_id,
                *envelope->task.request_id
            );
        }
    }

    MCP_LOG_DEBUG("JSON-RPC worker {} stopped", worker_index);
}

} // namespace mcp
