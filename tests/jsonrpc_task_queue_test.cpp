#include "jsonrpc_task_queue.h"

#include <gtest/gtest.h>

#include <memory>

using namespace mcp;

namespace {

JsonRpcTaskEnvelopePtr make_envelope(std::uint64_t id) {
    JsonRpcTask task;
    task.task_id = id;
    task.request_id = id;
    task.method = "test";

    return std::make_shared<JsonRpcTaskEnvelope>(std::move(task));
}

} // namespace

TEST(JsonRpcTaskQueueTest, RejectsTaskWhenFull) {
    BoundedJsonRpcTaskQueue queue(1);

    EXPECT_EQ(
        queue.try_push(make_envelope(1)),
        TaskSubmitStatus::Accepted
    );

    EXPECT_EQ(
        queue.try_push(make_envelope(2)),
        TaskSubmitStatus::QueueFull
    );
}

TEST(JsonRpcTaskQueueTest, RejectsTaskAfterClose) {
    BoundedJsonRpcTaskQueue queue(1);

    queue.close();

    EXPECT_EQ(
        queue.try_push(make_envelope(1)),
        TaskSubmitStatus::QueueClosed
    );
}

TEST(JsonRpcTaskQueueTest, DrainsAcceptedTasksBeforeStopping) {
    BoundedJsonRpcTaskQueue queue(1);
    auto envelope = make_envelope(1);

    ASSERT_EQ(
        queue.try_push(envelope),
        TaskSubmitStatus::Accepted
    );

    queue.close();

    EXPECT_EQ(queue.wait_pop(), envelope);
    EXPECT_EQ(queue.wait_pop(), nullptr);
}