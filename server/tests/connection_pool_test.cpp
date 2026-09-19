#include "support/fake_database.h"
#include "water/db/connection_pool.h"
#include "water/db/db_executor.h"

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using water::concurrency::ShutdownMode;
using water::db::ConnectionPool;
using water::db::ConnectionPoolOptions;
using water::db::ConnectionPoolState;
using water::db::ConnectionPoolTimeout;
using water::db::DbExecutor;
using water::db::DbExecutorOptions;
using water::db::Transaction;
using water::db::test::FakeConnection;
using water::db::test::FakeDatabaseState;
using water::db::test::makeFakeFactory;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            throw std::runtime_error(std::string("check failed: ") + #condition); \
        }                                                                       \
    } while (false)

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

ConnectionPoolOptions options(std::size_t minimum,
                              std::size_t maximum) {
    return {
        .name = "fake-mysql",
        .min_connections = minimum,
        .max_connections = maximum,
        .acquire_timeout = 100ms,
        .idle_timeout = 5s,
        .max_lifetime = 30s,
        .validation_interval = 30s,
        .maintenance_interval = 10ms,
        .shutdown_timeout = 1s,
    };
}

void testStartReuseAndTransaction() {
    auto database = std::make_shared<FakeDatabaseState>();
    ConnectionPool pool(options(1, 3), makeFakeFactory(database));
    pool.start();

    CHECK(pool.statistics().total_connections == 1);
    int first_id = 0;
    {
        auto lease = pool.acquire();
        first_id = dynamic_cast<FakeConnection&>(*lease).id();
        Transaction transaction(*lease);
        const auto result = lease->execute("INSERT", {});
        CHECK(result.affected_rows == 1);
        transaction.commit();
    }

    {
        auto lease = pool.acquire();
        CHECK(dynamic_cast<FakeConnection&>(*lease).id() == first_id);
        Transaction transaction(*lease);
        // Destructor must roll this transaction back.
    }

    CHECK(database->created.load() == 1);
    CHECK(database->begins.load() == 2);
    CHECK(database->commits.load() == 1);
    CHECK(database->rollbacks.load() == 1);
    CHECK(pool.shutdown());
}

void testDynamicGrowthLimitAndAcquireTimeout() {
    auto database = std::make_shared<FakeDatabaseState>();
    ConnectionPool pool(options(1, 2), makeFakeFactory(database));
    pool.start();

    auto first = pool.acquire();
    auto second = pool.acquire();
    CHECK(pool.statistics().total_connections == 2);

    try {
        auto unavailable = pool.acquireFor(30ms);
        static_cast<void>(unavailable);
        CHECK(false);
    } catch (const ConnectionPoolTimeout&) {
    }

    CHECK(pool.statistics().acquire_timeouts == 1);
    first.release();
    auto reused = pool.acquireFor(100ms);
    CHECK(static_cast<bool>(reused));
    reused.release();
    second.release();
    CHECK(pool.shutdown());
}

void testConcurrentBorrowersRespectMaximum() {
    auto database = std::make_shared<FakeDatabaseState>();
    auto config = options(1, 4);
    config.acquire_timeout = 1s;
    ConnectionPool pool(config, makeFakeFactory(database));
    pool.start();

    std::atomic_int current{0};
    std::atomic_int observed_peak{0};
    std::vector<std::future<void>> borrowers;
    for (int index = 0; index < 32; ++index) {
        borrowers.push_back(std::async(std::launch::async, [&] {
            auto lease = pool.acquire();
            const int active = current.fetch_add(1) + 1;
            int peak = observed_peak.load();
            while (active > peak &&
                   !observed_peak.compare_exchange_weak(peak, active)) {
            }
            std::this_thread::sleep_for(5ms);
            current.fetch_sub(1);
        }));
    }
    for (auto& borrower : borrowers) {
        borrower.get();
    }

    const auto stats = pool.statistics();
    CHECK(observed_peak.load() <= 4);
    CHECK(stats.peak_connections == 4);
    CHECK(stats.peak_borrowed_connections == 4);
    CHECK(stats.successful_acquires == 32);
    CHECK(pool.shutdown());
}

void testValidationFailureCreatesReplacement() {
    auto database = std::make_shared<FakeDatabaseState>();
    auto config = options(1, 2);
    config.validation_interval = 0ms;
    ConnectionPool pool(config, makeFakeFactory(database));
    pool.start();

    int unhealthy_id = 0;
    {
        auto lease = pool.acquire();
        auto& connection = dynamic_cast<FakeConnection&>(*lease);
        unhealthy_id = connection.id();
        connection.setPingHealthy(false);
    }

    auto replacement = pool.acquire();
    CHECK(dynamic_cast<FakeConnection&>(*replacement).id() != unhealthy_id);
    CHECK(pool.statistics().validation_failures == 1);
    CHECK(database->created.load() == 2);
    replacement.release();
    CHECK(pool.shutdown());
}

void testResetFailureDiscardsConnection() {
    auto database = std::make_shared<FakeDatabaseState>();
    ConnectionPool pool(options(1, 2), makeFakeFactory(database));
    pool.start();

    {
        auto lease = pool.acquire();
        dynamic_cast<FakeConnection&>(*lease).setResetHealthy(false);
    }

    waitUntil([&pool] {
        return pool.statistics().total_connections == 1;
    }, 1s);
    CHECK(pool.statistics().discarded_connections >= 1);
    CHECK(database->created.load() >= 2);
    CHECK(pool.shutdown());
}

void testIdleShrinkToMinimum() {
    auto database = std::make_shared<FakeDatabaseState>();
    auto config = options(1, 3);
    config.idle_timeout = 30ms;
    config.maintenance_interval = 5ms;
    ConnectionPool pool(config, makeFakeFactory(database));
    pool.start();

    auto first = pool.acquire();
    auto second = pool.acquire();
    auto third = pool.acquire();
    CHECK(pool.statistics().total_connections == 3);
    first.release();
    second.release();
    third.release();

    waitUntil([&pool] {
        return pool.statistics().total_connections == 1;
    }, 1s);
    CHECK(pool.statistics().idle_connections == 1);
    CHECK(pool.shutdown());
}

void testMaximumLifetimeRotatesMinimumConnection() {
    auto database = std::make_shared<FakeDatabaseState>();
    auto config = options(1, 2);
    config.max_lifetime = 20ms;
    ConnectionPool pool(config, makeFakeFactory(database));
    pool.start();

    int original_id = 0;
    {
        auto lease = pool.acquire();
        original_id = dynamic_cast<FakeConnection&>(*lease).id();
    }
    std::this_thread::sleep_for(30ms);

    auto replacement = pool.acquire();
    CHECK(dynamic_cast<FakeConnection&>(*replacement).id() != original_id);
    CHECK(pool.statistics().discarded_connections >= 1);
    replacement.release();
    CHECK(pool.shutdown());
}

void testShutdownWithOutstandingLease() {
    auto database = std::make_shared<FakeDatabaseState>();
    ConnectionPool pool(options(1, 1), makeFakeFactory(database));
    pool.start();
    auto lease = pool.acquire();

    CHECK(!pool.shutdownFor(10ms));
    CHECK(pool.state() == ConnectionPoolState::Stopped);
    lease.release();
    CHECK(pool.statistics().total_connections == 0);
    CHECK(pool.statistics().borrowed_connections == 0);
}

void testDbExecutorBridgesWorkerAndConnectionPools() {
    auto database = std::make_shared<FakeDatabaseState>();
    DbExecutorOptions config;
    config.connection_pool = options(2, 4);
    config.workers = {
        .name = "database-workers",
        .core_threads = 2,
        .max_threads = 4,
        .queue_capacity = 128,
        .idle_timeout = 100ms,
    };

    DbExecutor executor(config, makeFakeFactory(database));
    executor.start();

    std::vector<std::future<std::uint64_t>> results;
    for (int index = 0; index < 100; ++index) {
        results.push_back(executor.submit([](water::db::DbConnection& connection) {
            return connection.execute("UPDATE sample SET value = ?", {}).affected_rows;
        }));
    }
    for (auto& result : results) {
        CHECK(result.get() == 1);
    }

    executor.shutdown(ShutdownMode::Drain);
    CHECK(database->executions.load() == 100);
    CHECK(executor.statistics().workers.completed_tasks == 100);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"start/reuse/transaction", testStartReuseAndTransaction},
        {"dynamic growth/limit/timeout", testDynamicGrowthLimitAndAcquireTimeout},
        {"concurrent borrowers/max limit", testConcurrentBorrowersRespectMaximum},
        {"validation replacement", testValidationFailureCreatesReplacement},
        {"reset failure", testResetFailureDiscardsConnection},
        {"idle shrink", testIdleShrinkToMinimum},
        {"maximum lifetime rotation", testMaximumLifetimeRotatesMinimumConnection},
        {"outstanding lease shutdown", testShutdownWithOutstandingLease},
        {"database executor integration", testDbExecutorBridgesWorkerAndConnectionPools},
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
