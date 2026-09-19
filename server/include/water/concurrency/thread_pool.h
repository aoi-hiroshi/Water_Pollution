#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace water::concurrency {

enum class ThreadPoolState {
    Created,
    Running,
    Draining,
    Stopping,
    Stopped,
};

enum class ShutdownMode {
    // Stop accepting new tasks and execute every accepted task.
    Drain,
    // Stop accepting new tasks, discard queued tasks and request cooperative
    // cancellation from currently running stoppable tasks.
    CancelPending,
};

struct ThreadPoolOptions {
    std::string name{"business"};
    std::size_t core_threads{2};
    std::size_t max_threads{8};
    std::size_t queue_capacity{1024};
    std::chrono::milliseconds idle_timeout{std::chrono::seconds{30}};
};

struct ThreadPoolStatistics {
    ThreadPoolState state{ThreadPoolState::Created};
    std::size_t core_threads{0};
    std::size_t max_threads{0};
    std::size_t live_threads{0};
    std::size_t active_threads{0};
    std::size_t peak_threads{0};
    std::size_t queued_tasks{0};
    std::size_t peak_queued_tasks{0};
    std::size_t queue_capacity{0};
    std::uint64_t submitted_tasks{0};
    std::uint64_t completed_tasks{0};
    std::uint64_t rejected_tasks{0};
    std::uint64_t cancelled_tasks{0};
    std::uint64_t uncaught_task_exceptions{0};
    std::uint64_t total_threads_created{0};
    std::uint64_t worker_creation_failures{0};
};

class RejectedExecution final : public std::runtime_error {
public:
    explicit RejectedExecution(const std::string& message)
        : std::runtime_error(message) {}
};

// Adaptive producer-consumer thread pool.
//
// Lifecycle: Created -> Running -> Draining/Stopping -> Stopped.
// A stopped pool cannot be restarted. start() and shutdown() must be called by
// an owning/control thread, never by one of the pool's worker tasks.
class ThreadPool final {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(ThreadPoolOptions options = {});
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    void start();

    // Fire-and-forget APIs. They never block. false means the pool is not
    // running or the bounded queue is full.
    [[nodiscard]] bool post(Task task);
    [[nodiscard]] bool postFor(Task task, std::chrono::milliseconds timeout);

    // Submit a callable using std::bind + std::packaged_task. The returned
    // future carries both the result and any exception thrown by the callable.
    // submit() never waits for queue space and throws RejectedExecution when
    // the task cannot be accepted.
    template <typename Function, typename... Args>
    [[nodiscard]] auto submit(Function&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                            std::decay_t<Args>...>>;

    // Non-throwing, non-blocking submission. nullopt means rejection.
    template <typename Function, typename... Args>
    [[nodiscard]] auto trySubmit(Function&& function, Args&&... args)
        -> std::optional<std::future<std::invoke_result_t<
            std::decay_t<Function>, std::decay_t<Args>...>>>;

    // Wait at most timeout for bounded queue space. Throws RejectedExecution
    // on timeout or when the pool is not running.
    template <typename Function, typename... Args>
    [[nodiscard]] auto submitFor(std::chrono::milliseconds timeout,
                                 Function&& function,
                                 Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                            std::decay_t<Args>...>>;

    // Accept a packaged_task created by the caller.
    template <typename Result>
    [[nodiscard]] std::future<Result> submitPackaged(
        std::packaged_task<Result()>&& task);

    // C++20 cooperative cancellation API. The callable's first parameter must
    // be std::stop_token. CancelPending requests this token; Drain does not.
    template <typename Function, typename... Args>
    [[nodiscard]] auto submitStoppable(Function&& function, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                            std::stop_token,
                                            std::decay_t<Args>...>>;

    // Pre-create workers up to target_threads. Workers above core_threads are
    // still allowed to retire after idle_timeout.
    void reserve(std::size_t target_threads);

    [[nodiscard]] bool waitIdleFor(std::chrono::milliseconds timeout);
    void shutdown(ShutdownMode mode = ShutdownMode::Drain);

    [[nodiscard]] ThreadPoolState state() const;
    [[nodiscard]] ThreadPoolStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] bool enqueue(Task task,
                               std::optional<std::chrono::milliseconds> timeout);
    [[nodiscard]] std::stop_token stopToken() const noexcept;

    template <typename Result, typename Callable>
    [[nodiscard]] std::future<Result> enqueueFuture(Callable&& callable,
                                                    std::optional<std::chrono::milliseconds> timeout);
};

template <typename Result, typename Callable>
std::future<Result> ThreadPool::enqueueFuture(
    Callable&& callable,
    std::optional<std::chrono::milliseconds> timeout) {
    auto packaged = std::make_shared<std::packaged_task<Result()>>(
        std::forward<Callable>(callable));
    auto future = packaged->get_future();

    if (!enqueue([packaged] { (*packaged)(); }, timeout)) {
        throw RejectedExecution("thread pool rejected the task");
    }
    return future;
}

template <typename Function, typename... Args>
auto ThreadPool::submit(Function&& function, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                        std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        std::decay_t<Args>...>;
    auto bound = std::bind(std::forward<Function>(function),
                           std::forward<Args>(args)...);
    return enqueueFuture<Result>(std::move(bound), std::nullopt);
}

template <typename Function, typename... Args>
auto ThreadPool::trySubmit(Function&& function, Args&&... args)
    -> std::optional<std::future<std::invoke_result_t<
        std::decay_t<Function>, std::decay_t<Args>...>>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        std::decay_t<Args>...>;
    auto bound = std::bind(std::forward<Function>(function),
                           std::forward<Args>(args)...);
    auto packaged =
        std::make_shared<std::packaged_task<Result()>>(std::move(bound));
    auto future = packaged->get_future();

    if (!enqueue([packaged] { (*packaged)(); }, std::nullopt)) {
        return std::nullopt;
    }
    return std::optional<std::future<Result>>(std::move(future));
}

template <typename Function, typename... Args>
auto ThreadPool::submitFor(std::chrono::milliseconds timeout,
                           Function&& function,
                           Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                        std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        std::decay_t<Args>...>;
    auto bound = std::bind(std::forward<Function>(function),
                           std::forward<Args>(args)...);
    return enqueueFuture<Result>(std::move(bound), timeout);
}

template <typename Result>
std::future<Result> ThreadPool::submitPackaged(
    std::packaged_task<Result()>&& task) {
    auto packaged = std::make_shared<std::packaged_task<Result()>>(
        std::move(task));
    auto future = packaged->get_future();

    if (!enqueue([packaged] { (*packaged)(); }, std::nullopt)) {
        throw RejectedExecution("thread pool rejected the packaged task");
    }
    return future;
}

template <typename Function, typename... Args>
auto ThreadPool::submitStoppable(Function&& function, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<Function>,
                                        std::stop_token,
                                        std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<Function>,
                                        std::stop_token,
                                        std::decay_t<Args>...>;

    auto token = stopToken();
    auto callable = [fn = std::decay_t<Function>(
                         std::forward<Function>(function)),
                     bound_args = std::tuple<std::decay_t<Args>...>(
                         std::forward<Args>(args)...),
                     token]() mutable -> Result {
        return std::apply(
            [&fn, token](auto&&... values) mutable -> Result {
                return std::invoke(std::move(fn), token,
                                   std::forward<decltype(values)>(values)...);
            },
            std::move(bound_args));
    };

    return enqueueFuture<Result>(std::move(callable), std::nullopt);
}

}  // namespace water::concurrency
