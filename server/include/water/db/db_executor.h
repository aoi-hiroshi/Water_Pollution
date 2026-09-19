#pragma once

#include "water/concurrency/thread_pool.h"
#include "water/db/connection_pool.h"

#include <chrono>
#include <functional>
#include <exception>
#include <future>
#include <optional>
#include <type_traits>
#include <utility>

namespace water::db {

struct DbExecutorOptions {
    ConnectionPoolOptions connection_pool{};
    concurrency::ThreadPoolOptions workers{
        .name = "database",
        .core_threads = 2,
        .max_threads = 8,
        .queue_capacity = 512,
        .idle_timeout = std::chrono::seconds{30},
    };
};

struct DbExecutorStatistics {
    ConnectionPoolStatistics connections;
    concurrency::ThreadPoolStatistics workers;
};

// Bridges the non-blocking server layer and a blocking database driver.
// Muduo callbacks enqueue work and immediately return. A database worker then
// obtains an RAII lease, executes the repository operation, and fulfills the
// returned future.
class DbExecutor final {
public:
    using FireAndForgetTask = std::function<void(DbConnection&)>;

    DbExecutor(DbExecutorOptions options,
               ConnectionPool::ConnectionFactory factory);
    ~DbExecutor();

    DbExecutor(const DbExecutor&) = delete;
    DbExecutor& operator=(const DbExecutor&) = delete;
    DbExecutor(DbExecutor&&) = delete;
    DbExecutor& operator=(DbExecutor&&) = delete;

    void start();
    void shutdown(
        concurrency::ShutdownMode mode = concurrency::ShutdownMode::Drain);

    [[nodiscard]] bool post(FireAndForgetTask task);
    // Reports errors occurring before the task gets its connection, too.
    [[nodiscard]] bool post(FireAndForgetTask task,
                            std::function<void(std::exception_ptr)> on_error);

    template <typename Function>
    [[nodiscard]] auto submit(Function&& function)
        -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                            DbConnection&>>;

    template <typename Function>
    [[nodiscard]] auto trySubmit(Function&& function)
        -> std::optional<std::future<std::invoke_result_t<
            std::decay_t<Function>, DbConnection&>>>;

    [[nodiscard]] DbExecutorStatistics statistics() const;

private:
    DbExecutor(DbExecutorOptions options,
               ConnectionPool::ConnectionFactory factory,
               int validated_tag);

    std::chrono::milliseconds acquire_timeout_;
    ConnectionPool connection_pool_;
    concurrency::ThreadPool workers_;
};

template <typename Function>
auto DbExecutor::submit(Function&& function)
    -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                        DbConnection&>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        DbConnection&>;
    return workers_.submit(
        [this, task = std::decay_t<Function>(
                   std::forward<Function>(function))]() mutable -> Result {
            auto lease = connection_pool_.acquireFor(acquire_timeout_);
            return std::invoke(std::move(task), *lease);
        });
}

template <typename Function>
auto DbExecutor::trySubmit(Function&& function)
    -> std::optional<std::future<std::invoke_result_t<
        std::decay_t<Function>, DbConnection&>>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        DbConnection&>;
    return workers_.trySubmit(
        [this, task = std::decay_t<Function>(
                   std::forward<Function>(function))]() mutable -> Result {
            auto lease = connection_pool_.acquireFor(acquire_timeout_);
            return std::invoke(std::move(task), *lease);
        });
}

}  // namespace water::db
