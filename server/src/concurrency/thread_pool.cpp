#include "water/concurrency/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace water::concurrency {

struct ThreadPool::Impl {
    struct WorkerRecord {
        explicit WorkerRecord(bool core_worker)
            : core(core_worker),
              finished(std::make_shared<std::atomic_bool>(false)) {}

        bool core;
        std::shared_ptr<std::atomic_bool> finished;
        std::jthread thread;
    };

    explicit Impl(ThreadPoolOptions pool_options)
        : options(std::move(pool_options)) {
        if (options.core_threads == 0) {
            throw std::invalid_argument("core_threads must be greater than zero");
        }
        if (options.max_threads < options.core_threads) {
            throw std::invalid_argument(
                "max_threads must be greater than or equal to core_threads");
        }
        if (options.queue_capacity == 0) {
            throw std::invalid_argument("queue_capacity must be greater than zero");
        }
        if (options.idle_timeout <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("idle_timeout must be positive");
        }
    }

    void spawnWorkerLocked(bool core_worker) {
        auto worker = std::make_unique<WorkerRecord>(core_worker);
        auto finished = worker->finished;

        ++live_threads;
        peak_threads = std::max(peak_threads, live_threads);
        try {
            worker->thread = std::jthread(
                [this, core_worker, finished](std::stop_token worker_token) {
                    workerLoop(worker_token, core_worker, std::move(finished));
                });
            ++total_threads_created;
            workers.push_back(std::move(worker));
        } catch (...) {
            --live_threads;
            ++worker_creation_failures;
            throw;
        }
    }

    void maybeGrowLocked() noexcept {
        // active + queued approximates the number of workers that can make
        // immediate progress. Core workers absorb light traffic; backlog grows
        // the pool up to max_threads.
        const auto desired = std::min(options.max_threads,
                                      active_threads + tasks.size());
        while (live_threads < desired) {
            try {
                spawnWorkerLocked(false);
            } catch (...) {
                // Existing workers can still drain the queue. The failure is
                // observable through statistics and a later submission retries.
                break;
            }
        }
    }

    void reapFinishedWorkersLocked() {
        auto iterator = workers.begin();
        while (iterator != workers.end()) {
            if (!(*iterator)->finished->load(std::memory_order_acquire)) {
                ++iterator;
                continue;
            }
            if ((*iterator)->thread.joinable()) {
                (*iterator)->thread.join();
            }
            iterator = workers.erase(iterator);
        }
    }

    void workerLoop(std::stop_token worker_token,
                    bool core_worker,
                    std::shared_ptr<std::atomic_bool> finished) {
        for (;;) {
            Task task;
            {
                std::unique_lock lock(mutex);
                const auto ready = [this, &worker_token] {
                    return !tasks.empty() || state != ThreadPoolState::Running ||
                           worker_token.stop_requested();
                };

                if (core_worker) {
                    task_available.wait(lock, ready);
                } else if (!task_available.wait_for(lock, options.idle_timeout,
                                                    ready)) {
                    // Only elastic workers retire. Core capacity remains ready.
                    --live_threads;
                    lock.unlock();
                    idle_changed.notify_all();
                    finished->store(true, std::memory_order_release);
                    return;
                }

                if (state == ThreadPoolState::Stopping ||
                    worker_token.stop_requested()) {
                    --live_threads;
                    lock.unlock();
                    idle_changed.notify_all();
                    finished->store(true, std::memory_order_release);
                    return;
                }

                if (state == ThreadPoolState::Draining && tasks.empty()) {
                    --live_threads;
                    lock.unlock();
                    idle_changed.notify_all();
                    finished->store(true, std::memory_order_release);
                    return;
                }

                if (tasks.empty()) {
                    continue;
                }

                task = std::move(tasks.front());
                tasks.pop_front();
                ++active_threads;
            }
            queue_space.notify_one();

            try {
                task();
            } catch (...) {
                // packaged_task stores callable exceptions in its future. This
                // counter therefore represents uncaught fire-and-forget errors.
                std::lock_guard lock(mutex);
                ++uncaught_task_exceptions;
            }

            {
                std::lock_guard lock(mutex);
                --active_threads;
                ++completed_tasks;
            }
            idle_changed.notify_all();
        }
    }

    [[nodiscard]] bool calledFromWorkerLocked() const {
        const auto caller = std::this_thread::get_id();
        return std::any_of(workers.begin(), workers.end(),
                           [caller](const auto& worker) {
                               return worker->thread.get_id() == caller;
                           });
    }

    ThreadPoolOptions options;
    mutable std::mutex mutex;
    std::mutex shutdown_mutex;
    std::condition_variable task_available;
    std::condition_variable queue_space;
    std::condition_variable idle_changed;
    std::deque<Task> tasks;
    std::vector<std::unique_ptr<WorkerRecord>> workers;
    std::stop_source pool_stop_source;

    ThreadPoolState state{ThreadPoolState::Created};
    std::size_t live_threads{0};
    std::size_t active_threads{0};
    std::size_t peak_threads{0};
    std::size_t peak_queued_tasks{0};
    std::uint64_t submitted_tasks{0};
    std::uint64_t completed_tasks{0};
    std::uint64_t rejected_tasks{0};
    std::uint64_t cancelled_tasks{0};
    std::uint64_t uncaught_task_exceptions{0};
    std::uint64_t total_threads_created{0};
    std::uint64_t worker_creation_failures{0};
};

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

ThreadPool::~ThreadPool() {
    try {
        shutdown(ShutdownMode::Drain);
    } catch (...) {
        // Destroying a pool from one of its own tasks violates the documented
        // lifetime contract and cannot be recovered without a use-after-free.
        std::terminate();
    }
}

void ThreadPool::start() {
    std::unique_lock lock(impl_->mutex);
    if (impl_->state != ThreadPoolState::Created) {
        throw std::logic_error("thread pool can only be started once");
    }

    impl_->state = ThreadPoolState::Running;
    try {
        for (std::size_t i = 0; i < impl_->options.core_threads; ++i) {
            impl_->spawnWorkerLocked(true);
        }
    } catch (...) {
        impl_->state = ThreadPoolState::Stopping;
        impl_->pool_stop_source.request_stop();
        for (const auto& worker : impl_->workers) {
            worker->thread.request_stop();
        }
        lock.unlock();
        impl_->task_available.notify_all();
        for (const auto& worker : impl_->workers) {
            if (worker->thread.joinable()) {
                worker->thread.join();
            }
        }
        lock.lock();
        impl_->workers.clear();
        impl_->state = ThreadPoolState::Stopped;
        throw;
    }
}

bool ThreadPool::enqueue(
    Task task,
    std::optional<std::chrono::milliseconds> timeout) {
    if (!task) {
        throw std::invalid_argument("task must not be empty");
    }

    std::unique_lock lock(impl_->mutex);
    if (impl_->state != ThreadPoolState::Running) {
        ++impl_->rejected_tasks;
        return false;
    }

    if (impl_->tasks.size() >= impl_->options.queue_capacity) {
        if (!timeout.has_value() || *timeout <= std::chrono::milliseconds::zero()) {
            ++impl_->rejected_tasks;
            return false;
        }

        const bool has_space = impl_->queue_space.wait_for(
            lock, *timeout, [this] {
                return impl_->state != ThreadPoolState::Running ||
                       impl_->tasks.size() < impl_->options.queue_capacity;
            });
        if (!has_space || impl_->state != ThreadPoolState::Running) {
            ++impl_->rejected_tasks;
            return false;
        }
    }

    impl_->reapFinishedWorkersLocked();
    impl_->tasks.push_back(std::move(task));
    ++impl_->submitted_tasks;
    impl_->peak_queued_tasks =
        std::max(impl_->peak_queued_tasks, impl_->tasks.size());
    impl_->maybeGrowLocked();
    lock.unlock();
    impl_->task_available.notify_one();
    return true;
}

bool ThreadPool::post(Task task) {
    return enqueue(std::move(task), std::nullopt);
}

bool ThreadPool::postFor(Task task, std::chrono::milliseconds timeout) {
    return enqueue(std::move(task), timeout);
}

void ThreadPool::reserve(std::size_t target_threads) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->state != ThreadPoolState::Running) {
        throw std::logic_error("reserve requires a running thread pool");
    }

    impl_->reapFinishedWorkersLocked();
    const auto target = std::min(target_threads, impl_->options.max_threads);
    while (impl_->live_threads < target) {
        impl_->spawnWorkerLocked(false);
    }
}

bool ThreadPool::waitIdleFor(std::chrono::milliseconds timeout) {
    std::unique_lock lock(impl_->mutex);
    return impl_->idle_changed.wait_for(lock, timeout, [this] {
        return impl_->tasks.empty() && impl_->active_threads == 0;
    });
}

void ThreadPool::shutdown(ShutdownMode mode) {
    std::lock_guard shutdown_lock(impl_->shutdown_mutex);
    std::deque<Task> cancelled;

    {
        std::unique_lock lock(impl_->mutex);
        impl_->reapFinishedWorkersLocked();
        if (impl_->calledFromWorkerLocked()) {
            throw std::logic_error(
                "shutdown must not be called from a pool worker");
        }
        if (impl_->state == ThreadPoolState::Stopped) {
            return;
        }
        if (impl_->state == ThreadPoolState::Created) {
            impl_->state = ThreadPoolState::Stopped;
            return;
        }

        if (mode == ShutdownMode::Drain) {
            impl_->state = ThreadPoolState::Draining;
        } else {
            impl_->state = ThreadPoolState::Stopping;
            impl_->pool_stop_source.request_stop();
            impl_->cancelled_tasks += impl_->tasks.size();
            cancelled.swap(impl_->tasks);
            for (const auto& worker : impl_->workers) {
                worker->thread.request_stop();
            }
        }
    }

    // Destroy cancelled packaged_task objects without holding the pool mutex.
    // Their futures become ready with future_errc::broken_promise.
    cancelled.clear();
    impl_->task_available.notify_all();
    impl_->queue_space.notify_all();

    for (const auto& worker : impl_->workers) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->workers.clear();
        impl_->live_threads = 0;
        impl_->state = ThreadPoolState::Stopped;
    }
    impl_->idle_changed.notify_all();
}

ThreadPoolState ThreadPool::state() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->state;
}

ThreadPoolStatistics ThreadPool::statistics() const {
    std::lock_guard lock(impl_->mutex);
    return ThreadPoolStatistics{
        .state = impl_->state,
        .core_threads = impl_->options.core_threads,
        .max_threads = impl_->options.max_threads,
        .live_threads = impl_->live_threads,
        .active_threads = impl_->active_threads,
        .peak_threads = impl_->peak_threads,
        .queued_tasks = impl_->tasks.size(),
        .peak_queued_tasks = impl_->peak_queued_tasks,
        .queue_capacity = impl_->options.queue_capacity,
        .submitted_tasks = impl_->submitted_tasks,
        .completed_tasks = impl_->completed_tasks,
        .rejected_tasks = impl_->rejected_tasks,
        .cancelled_tasks = impl_->cancelled_tasks,
        .uncaught_task_exceptions = impl_->uncaught_task_exceptions,
        .total_threads_created = impl_->total_threads_created,
        .worker_creation_failures = impl_->worker_creation_failures,
    };
}

std::stop_token ThreadPool::stopToken() const noexcept {
    return impl_->pool_stop_source.get_token();
}

}  // namespace water::concurrency
