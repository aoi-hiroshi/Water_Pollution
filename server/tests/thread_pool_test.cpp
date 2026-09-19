#include "water/concurrency/thread_pool.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using water::concurrency::ShutdownMode;
using water::concurrency::ThreadPool;
using water::concurrency::ThreadPoolOptions;
using water::concurrency::ThreadPoolState;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            throw std::runtime_error(std::string("check failed: ") + #condition); \
        }                                                                       \
    } while (false)

struct Calculator {
    int add(int left, int right) const { return left + right; }
};

void waitUntil(const std::function<bool()>& condition,
               std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("condition wait timed out");
        }
        std::this_thread::sleep_for(2ms);
    }
}

void testSubmitBindFutureAndException() {
    ThreadPool pool({.name = "future-test",
                     .core_threads = 1,
                     .max_threads = 2,
                     .queue_capacity = 8,
                     .idle_timeout = 100ms});
    pool.start();

    Calculator calculator;
    auto member_result = pool.submit(&Calculator::add, &calculator, 20, 22);
    auto lambda_result = pool.submit([](std::string value) {
        return value + "-done";
    }, std::string{"task"});
    auto exception_result = pool.submit([]() -> int {
        throw std::runtime_error("expected");
    });

    CHECK(member_result.get() == 42);
    CHECK(lambda_result.get() == "task-done");
    try {
        static_cast<void>(exception_result.get());
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK(std::string{error.what()} == "expected");
    }

    pool.shutdown();
    CHECK(pool.state() == ThreadPoolState::Stopped);
}

void testCallerProvidedPackagedTask() {
    ThreadPool pool({.core_threads = 1,
                     .max_threads = 1,
                     .queue_capacity = 4,
                     .idle_timeout = 100ms});
    pool.start();

    std::packaged_task<int()> task([] { return 7 * 6; });
    auto result = pool.submitPackaged(std::move(task));
    CHECK(result.get() == 42);
    pool.shutdown();
}

void testTrySubmitTimedSubmitReserveAndPostException() {
    ThreadPool pool({.core_threads = 1,
                     .max_threads = 3,
                     .queue_capacity = 8,
                     .idle_timeout = 100ms});
    pool.start();
    pool.reserve(3);
    CHECK(pool.statistics().live_threads == 3);

    auto optional_result = pool.trySubmit([](int value) { return value * 2; }, 21);
    CHECK(optional_result.has_value());
    CHECK(optional_result->get() == 42);

    auto timed_result = pool.submitFor(100ms, [] { return std::string{"accepted"}; });
    CHECK(timed_result.get() == "accepted");
    CHECK(pool.post([] { throw std::runtime_error("fire-and-forget"); }));
    CHECK(pool.waitIdleFor(1s));
    CHECK(pool.statistics().uncaught_task_exceptions == 1);
    pool.shutdown();
}

void testDynamicGrowthAndRetirement() {
    ThreadPool pool({.name = "adaptive-test",
                     .core_threads = 1,
                     .max_threads = 4,
                     .queue_capacity = 16,
                     .idle_timeout = 80ms});
    pool.start();

    std::latch release_tasks{1};
    std::vector<std::future<int>> results;
    for (int value = 0; value < 4; ++value) {
        results.push_back(pool.submit([value, &release_tasks] {
            release_tasks.wait();
            return value;
        }));
    }

    waitUntil([&pool] { return pool.statistics().live_threads == 4; }, 1s);
    CHECK(pool.statistics().peak_threads == 4);

    release_tasks.count_down();
    for (int value = 0; value < 4; ++value) {
        CHECK(results.at(static_cast<std::size_t>(value)).get() == value);
    }
    CHECK(pool.waitIdleFor(1s));

    waitUntil([&pool] { return pool.statistics().live_threads == 1; }, 1s);
    pool.shutdown();
}

void testBoundedQueueRejectionAndTimedSubmission() {
    ThreadPool pool({.name = "bounded-test",
                     .core_threads = 1,
                     .max_threads = 1,
                     .queue_capacity = 1,
                     .idle_timeout = 100ms});
    pool.start();

    std::promise<void> started;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    auto running = pool.submit([&started, release_signal] {
        started.set_value();
        release_signal.wait();
    });
    started.get_future().wait();

    auto queued = pool.submit([] { return 42; });
    CHECK(!pool.post([] {}));

    auto producer = std::async(std::launch::async, [&pool] {
        return pool.postFor([] {}, 500ms);
    });
    std::this_thread::sleep_for(20ms);
    release.set_value();

    running.get();
    CHECK(queued.get() == 42);
    CHECK(producer.get());
    CHECK(pool.waitIdleFor(1s));
    CHECK(pool.statistics().rejected_tasks == 1);
    pool.shutdown();
}

void testDrainExecutesAcceptedTasks() {
    ThreadPool pool({.core_threads = 2,
                     .max_threads = 4,
                     .queue_capacity = 32,
                     .idle_timeout = 100ms});
    pool.start();

    std::atomic_int counter{0};
    for (int index = 0; index < 20; ++index) {
        CHECK(pool.post([&counter] { counter.fetch_add(1); }));
    }

    pool.shutdown(ShutdownMode::Drain);
    CHECK(counter.load() == 20);
    const auto stats = pool.statistics();
    CHECK(stats.completed_tasks == 20);
    CHECK(stats.cancelled_tasks == 0);
}

void testConcurrentProducers() {
    ThreadPool pool({.name = "producer-consumer-test",
                     .core_threads = 2,
                     .max_threads = 8,
                     .queue_capacity = 64,
                     .idle_timeout = 100ms});
    pool.start();

    constexpr int producer_count = 8;
    constexpr int tasks_per_producer = 250;
    std::atomic_int executed{0};
    std::vector<std::future<void>> producers;
    producers.reserve(producer_count);

    for (int producer = 0; producer < producer_count; ++producer) {
        producers.push_back(std::async(std::launch::async, [&pool, &executed] {
            for (int index = 0; index < tasks_per_producer; ++index) {
                const bool accepted = pool.postFor(
                    [&executed] {
                        std::this_thread::sleep_for(50us);
                        executed.fetch_add(1, std::memory_order_relaxed);
                    },
                    1s);
                CHECK(accepted);
            }
        }));
    }

    for (auto& producer : producers) {
        producer.get();
    }
    CHECK(pool.waitIdleFor(5s));
    CHECK(executed.load(std::memory_order_relaxed) ==
          producer_count * tasks_per_producer);

    const auto stats = pool.statistics();
    CHECK(stats.submitted_tasks == producer_count * tasks_per_producer);
    CHECK(stats.completed_tasks == stats.submitted_tasks);
    CHECK(stats.peak_threads > stats.core_threads);
    pool.shutdown();
}

void testCancelPendingAndStopToken() {
    ThreadPool pool({.core_threads = 1,
                     .max_threads = 1,
                     .queue_capacity = 8,
                     .idle_timeout = 100ms});
    pool.start();

    std::promise<void> started;
    auto running = pool.submitStoppable([&started](std::stop_token token) {
        started.set_value();
        while (!token.stop_requested()) {
            std::this_thread::sleep_for(1ms);
        }
        return 11;
    });
    started.get_future().wait();
    auto cancelled = pool.submit([] { return 22; });

    pool.shutdown(ShutdownMode::CancelPending);
    CHECK(running.get() == 11);
    try {
        static_cast<void>(cancelled.get());
        CHECK(false);
    } catch (const std::future_error& error) {
        CHECK(error.code() ==
              std::make_error_code(std::future_errc::broken_promise));
    }
    CHECK(pool.statistics().cancelled_tasks == 1);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"submit/bind/future/exception", testSubmitBindFutureAndException},
        {"caller packaged_task", testCallerProvidedPackagedTask},
        {"try/timed/reserve/post exception", testTrySubmitTimedSubmitReserveAndPostException},
        {"dynamic growth and retirement", testDynamicGrowthAndRetirement},
        {"bounded queue and timed submit", testBoundedQueueRejectionAndTimedSubmission},
        {"drain shutdown", testDrainExecutesAcceptedTasks},
        {"concurrent producers", testConcurrentProducers},
        {"cancel pending and stop token", testCancelPendingAndStopToken},
    };

    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
