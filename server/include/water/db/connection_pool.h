#pragma once

#include "water/db/database.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace water::db {

enum class ConnectionPoolState {
    Created,
    Starting,
    Running,
    Stopping,
    Stopped,
};

struct ConnectionPoolOptions {
    std::string name{"mysql"};
    std::size_t min_connections{2};
    std::size_t max_connections{16};
    std::chrono::milliseconds acquire_timeout{std::chrono::milliseconds{500}};
    std::chrono::milliseconds idle_timeout{std::chrono::minutes{5}};
    std::chrono::milliseconds max_lifetime{std::chrono::minutes{30}};
    std::chrono::milliseconds validation_interval{std::chrono::seconds{30}};
    std::chrono::milliseconds maintenance_interval{std::chrono::seconds{5}};
    std::chrono::milliseconds shutdown_timeout{std::chrono::seconds{5}};
};

struct ConnectionPoolStatistics {
    ConnectionPoolState state{ConnectionPoolState::Created};
    std::size_t min_connections{0};
    std::size_t max_connections{0};
    std::size_t total_connections{0};
    std::size_t idle_connections{0};
    std::size_t borrowed_connections{0};
    std::size_t creating_connections{0};
    std::size_t waiting_borrowers{0};
    std::size_t peak_connections{0};
    std::size_t peak_borrowed_connections{0};
    std::uint64_t successful_acquires{0};
    std::uint64_t acquire_timeouts{0};
    std::uint64_t creation_failures{0};
    std::uint64_t validation_failures{0};
    std::uint64_t discarded_connections{0};
    std::uint64_t cumulative_wait_microseconds{0};
    std::uint64_t max_wait_microseconds{0};
};

class ConnectionPoolTimeout final : public DbError {
public:
    using DbError::DbError;
};

class ConnectionPoolStopped final : public DbError {
public:
    using DbError::DbError;
};

namespace detail {
struct ConnectionRecord;
struct ConnectionPoolSharedState;
}  // namespace detail

// Move-only RAII handle. Destroying or release()ing it resets the database
// session and returns a healthy connection to the pool.
class ConnectionLease final {
public:
    ConnectionLease() noexcept;
    ~ConnectionLease();

    ConnectionLease(ConnectionLease&& other) noexcept;
    ConnectionLease& operator=(ConnectionLease&& other) noexcept;

    ConnectionLease(const ConnectionLease&) = delete;
    ConnectionLease& operator=(const ConnectionLease&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] DbConnection* get() noexcept;
    [[nodiscard]] const DbConnection* get() const noexcept;
    [[nodiscard]] DbConnection& operator*();
    [[nodiscard]] const DbConnection& operator*() const;
    [[nodiscard]] DbConnection* operator->() noexcept;
    [[nodiscard]] const DbConnection* operator->() const noexcept;

    void release() noexcept;

private:
    friend class ConnectionPool;
    ConnectionLease(
        std::shared_ptr<detail::ConnectionPoolSharedState> state,
        std::unique_ptr<detail::ConnectionRecord> record) noexcept;

    std::shared_ptr<detail::ConnectionPoolSharedState> state_;
    std::unique_ptr<detail::ConnectionRecord> record_;
};

class ConnectionPool final {
public:
    using ConnectionFactory = std::function<std::unique_ptr<DbConnection>()>;

    ConnectionPool(ConnectionPoolOptions options, ConnectionFactory factory);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) = delete;
    ConnectionPool& operator=(ConnectionPool&&) = delete;

    void start();

    [[nodiscard]] ConnectionLease acquire();
    [[nodiscard]] ConnectionLease acquireFor(
        std::chrono::milliseconds timeout);

    // Returns true if every borrowed/creating connection finished before the
    // deadline. Outstanding leases remain safe and are closed when returned.
    [[nodiscard]] bool shutdown();
    [[nodiscard]] bool shutdownFor(std::chrono::milliseconds timeout);

    [[nodiscard]] ConnectionPoolState state() const;
    [[nodiscard]] ConnectionPoolStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace water::db
