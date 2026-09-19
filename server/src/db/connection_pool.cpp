#include "water/db/connection_pool.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace water::db::detail {

using Clock = std::chrono::steady_clock;

struct ConnectionRecord {
    explicit ConnectionRecord(std::unique_ptr<DbConnection> value)
        : connection(std::move(value)),
          created_at(Clock::now()),
          last_used(created_at),
          last_validated(created_at) {}

    std::unique_ptr<DbConnection> connection;
    Clock::time_point created_at;
    Clock::time_point last_used;
    Clock::time_point last_validated;
};

struct ConnectionPoolSharedState {
    ConnectionPoolSharedState(ConnectionPoolOptions pool_options,
                              ConnectionPool::ConnectionFactory pool_factory)
        : options(std::move(pool_options)), factory(std::move(pool_factory)) {}

    void recordWaitLocked(Clock::time_point started) {
        const auto wait = std::chrono::duration_cast<std::chrono::microseconds>(
                              Clock::now() - started)
                              .count();
        const auto value = static_cast<std::uint64_t>(std::max<std::int64_t>(0, wait));
        cumulative_wait_microseconds += value;
        max_wait_microseconds = std::max(max_wait_microseconds, value);
    }

    ConnectionPoolOptions options;
    ConnectionPool::ConnectionFactory factory;
    mutable std::mutex mutex;
    std::mutex shutdown_mutex;
    std::condition_variable available;
    std::condition_variable lifecycle_changed;
    std::condition_variable maintenance_wakeup;
    std::deque<std::unique_ptr<ConnectionRecord>> idle;
    std::jthread maintenance_thread;

    ConnectionPoolState state{ConnectionPoolState::Created};
    std::size_t total_connections{0};
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

void releaseConnection(
    const std::shared_ptr<ConnectionPoolSharedState>& state,
    std::unique_ptr<ConnectionRecord> record) noexcept {
    if (!state || !record) {
        return;
    }

    const bool reset_succeeded = record->connection->resetSession();
    const auto now = Clock::now();
    const bool lifetime_expired =
        state->options.max_lifetime > std::chrono::milliseconds::zero() &&
        now - record->created_at >= state->options.max_lifetime;

    bool returned_to_pool = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->borrowed_connections > 0) {
            --state->borrowed_connections;
        }

        if (state->state == ConnectionPoolState::Running &&
            reset_succeeded && !lifetime_expired) {
            record->last_used = now;
            state->idle.push_back(std::move(record));
            returned_to_pool = true;
        } else {
            if (state->total_connections > 0) {
                --state->total_connections;
            }
            if (state->state == ConnectionPoolState::Running) {
                ++state->discarded_connections;
            }
        }
    }

    state->available.notify_one();
    state->lifecycle_changed.notify_all();
    if (!returned_to_pool) {
        state->maintenance_wakeup.notify_one();
    }
}

void maintenanceLoop(ConnectionPoolSharedState* state,
                     std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        std::vector<std::unique_ptr<ConnectionRecord>> expired;
        std::size_t connections_to_create = 0;

        {
            std::unique_lock lock(state->mutex);
            state->maintenance_wakeup.wait_for(
                lock, state->options.maintenance_interval,
                [state, &stop_token] {
                    return stop_token.stop_requested() ||
                           state->state != ConnectionPoolState::Running ||
                           state->total_connections +
                                   state->creating_connections <
                               state->options.min_connections;
                });

            if (stop_token.stop_requested() ||
                state->state != ConnectionPoolState::Running) {
                return;
            }

            const auto now = Clock::now();
            auto iterator = state->idle.begin();
            while (iterator != state->idle.end() &&
                   state->total_connections >
                       state->options.min_connections) {
                const bool idle_expired =
                    state->options.idle_timeout >
                        std::chrono::milliseconds::zero() &&
                    now - (*iterator)->last_used >=
                        state->options.idle_timeout;
                const bool lifetime_expired =
                    state->options.max_lifetime >
                        std::chrono::milliseconds::zero() &&
                    now - (*iterator)->created_at >=
                        state->options.max_lifetime;

                if (!idle_expired && !lifetime_expired) {
                    ++iterator;
                    continue;
                }

                expired.push_back(std::move(*iterator));
                iterator = state->idle.erase(iterator);
                --state->total_connections;
                ++state->discarded_connections;
            }

            if (state->total_connections + state->creating_connections <
                state->options.min_connections) {
                connections_to_create =
                    state->options.min_connections -
                    state->total_connections - state->creating_connections;
                state->creating_connections += connections_to_create;
            }
        }

        // Close expired connections without holding the pool mutex.
        expired.clear();

        for (std::size_t index = 0; index < connections_to_create; ++index) {
            std::unique_ptr<DbConnection> connection;
            try {
                connection = state->factory();
                if (!connection) {
                    throw DbError("connection factory returned null");
                }
            } catch (...) {
                std::lock_guard lock(state->mutex);
                --state->creating_connections;
                ++state->creation_failures;
                state->lifecycle_changed.notify_all();
                continue;
            }

            auto record = std::make_unique<ConnectionRecord>(
                std::move(connection));
            {
                std::lock_guard lock(state->mutex);
                --state->creating_connections;
                if (state->state == ConnectionPoolState::Running) {
                    state->idle.push_back(std::move(record));
                    ++state->total_connections;
                    state->peak_connections = std::max(
                        state->peak_connections, state->total_connections);
                }
            }
            state->available.notify_one();
            state->lifecycle_changed.notify_all();
        }
    }
}

}  // namespace water::db::detail

namespace water::db {

struct ConnectionPool::Impl {
    Impl(ConnectionPoolOptions options, ConnectionFactory factory)
        : shared(std::make_shared<detail::ConnectionPoolSharedState>(
              std::move(options), std::move(factory))) {
        const auto& config = shared->options;
        if (!shared->factory) {
            throw std::invalid_argument("connection factory must not be empty");
        }
        if (config.max_connections == 0) {
            throw std::invalid_argument("max_connections must be greater than zero");
        }
        if (config.min_connections > config.max_connections) {
            throw std::invalid_argument(
                "min_connections must not exceed max_connections");
        }
        if (config.acquire_timeout < std::chrono::milliseconds::zero() ||
            config.maintenance_interval <= std::chrono::milliseconds::zero() ||
            config.shutdown_timeout < std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("connection pool timeouts are invalid");
        }
    }

    std::unique_ptr<detail::ConnectionRecord> acquireRecord(
        std::chrono::milliseconds timeout) {
        if (timeout < std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("acquire timeout must not be negative");
        }

        const auto started = detail::Clock::now();
        const auto deadline = started + timeout;

        for (;;) {
            std::unique_lock lock(shared->mutex);
            if (shared->state != ConnectionPoolState::Running) {
                shared->recordWaitLocked(started);
                throw ConnectionPoolStopped("connection pool is not running");
            }

            if (!shared->idle.empty()) {
                auto record = std::move(shared->idle.front());
                shared->idle.pop_front();
                ++shared->borrowed_connections;
                shared->peak_borrowed_connections = std::max(
                    shared->peak_borrowed_connections,
                    shared->borrowed_connections);

                const bool lifetime_expired =
                    shared->options.max_lifetime >
                        std::chrono::milliseconds::zero() &&
                    detail::Clock::now() - record->created_at >=
                        shared->options.max_lifetime;
                const bool should_validate =
                    shared->options.validation_interval <=
                        std::chrono::milliseconds::zero() ||
                    detail::Clock::now() - record->last_validated >=
                        shared->options.validation_interval;
                lock.unlock();

                if (lifetime_expired) {
                    lock.lock();
                    --shared->borrowed_connections;
                    --shared->total_connections;
                    ++shared->discarded_connections;
                    lock.unlock();
                    record.reset();
                    shared->available.notify_one();
                    shared->maintenance_wakeup.notify_one();
                    shared->lifecycle_changed.notify_all();
                    continue;
                }

                if (should_validate && !record->connection->ping()) {
                    lock.lock();
                    --shared->borrowed_connections;
                    --shared->total_connections;
                    ++shared->validation_failures;
                    ++shared->discarded_connections;
                    lock.unlock();
                    record.reset();
                    shared->available.notify_one();
                    shared->maintenance_wakeup.notify_one();
                    shared->lifecycle_changed.notify_all();
                    continue;
                }
                if (should_validate) {
                    record->last_validated = detail::Clock::now();
                }

                lock.lock();
                ++shared->successful_acquires;
                shared->recordWaitLocked(started);
                return record;
            }

            if (shared->total_connections + shared->creating_connections <
                shared->options.max_connections) {
                ++shared->creating_connections;
                lock.unlock();

                std::unique_ptr<DbConnection> connection;
                try {
                    connection = shared->factory();
                    if (!connection) {
                        throw DbError("connection factory returned null");
                    }
                } catch (...) {
                    lock.lock();
                    --shared->creating_connections;
                    ++shared->creation_failures;
                    shared->recordWaitLocked(started);
                    lock.unlock();
                    shared->available.notify_one();
                    shared->lifecycle_changed.notify_all();
                    throw;
                }

                auto record = std::make_unique<detail::ConnectionRecord>(
                    std::move(connection));
                lock.lock();
                --shared->creating_connections;
                if (shared->state != ConnectionPoolState::Running) {
                    shared->recordWaitLocked(started);
                    lock.unlock();
                    shared->available.notify_all();
                    shared->lifecycle_changed.notify_all();
                    throw ConnectionPoolStopped(
                        "connection pool stopped while creating a connection");
                }

                ++shared->total_connections;
                ++shared->borrowed_connections;
                ++shared->successful_acquires;
                shared->peak_connections = std::max(
                    shared->peak_connections, shared->total_connections);
                shared->peak_borrowed_connections = std::max(
                    shared->peak_borrowed_connections,
                    shared->borrowed_connections);
                shared->recordWaitLocked(started);
                lock.unlock();
                shared->lifecycle_changed.notify_all();
                return record;
            }

            ++shared->waiting_borrowers;
            const bool ready = shared->available.wait_until(
                lock, deadline, [this] {
                    return shared->state != ConnectionPoolState::Running ||
                           !shared->idle.empty() ||
                           shared->total_connections +
                                   shared->creating_connections <
                               shared->options.max_connections;
                });
            --shared->waiting_borrowers;

            if (!ready) {
                ++shared->acquire_timeouts;
                shared->recordWaitLocked(started);
                throw ConnectionPoolTimeout(
                    "timed out waiting for a database connection");
            }
        }
    }

    std::shared_ptr<detail::ConnectionPoolSharedState> shared;
};

ConnectionLease::ConnectionLease() noexcept = default;

ConnectionLease::ConnectionLease(
    std::shared_ptr<detail::ConnectionPoolSharedState> state,
    std::unique_ptr<detail::ConnectionRecord> record) noexcept
    : state_(std::move(state)), record_(std::move(record)) {}

ConnectionLease::~ConnectionLease() {
    release();
}

ConnectionLease::ConnectionLease(ConnectionLease&& other) noexcept = default;

ConnectionLease& ConnectionLease::operator=(ConnectionLease&& other) noexcept {
    if (this != &other) {
        release();
        state_ = std::move(other.state_);
        record_ = std::move(other.record_);
    }
    return *this;
}

ConnectionLease::operator bool() const noexcept {
    return record_ && record_->connection;
}

DbConnection* ConnectionLease::get() noexcept {
    return record_ ? record_->connection.get() : nullptr;
}

const DbConnection* ConnectionLease::get() const noexcept {
    return record_ ? record_->connection.get() : nullptr;
}

DbConnection& ConnectionLease::operator*() {
    if (!get()) {
        throw DbError("attempted to dereference an empty connection lease");
    }
    return *get();
}

const DbConnection& ConnectionLease::operator*() const {
    if (!get()) {
        throw DbError("attempted to dereference an empty connection lease");
    }
    return *get();
}

DbConnection* ConnectionLease::operator->() noexcept {
    return get();
}

const DbConnection* ConnectionLease::operator->() const noexcept {
    return get();
}

void ConnectionLease::release() noexcept {
    if (!record_) {
        return;
    }
    detail::releaseConnection(state_, std::move(record_));
    state_.reset();
}

ConnectionPool::ConnectionPool(ConnectionPoolOptions options,
                               ConnectionFactory factory)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(factory))) {}

ConnectionPool::~ConnectionPool() {
    static_cast<void>(shutdown());
}

void ConnectionPool::start() {
    auto state = impl_->shared;
    {
        std::lock_guard lock(state->mutex);
        if (state->state != ConnectionPoolState::Created) {
            throw std::logic_error("connection pool can only be started once");
        }
        state->state = ConnectionPoolState::Starting;
        state->creating_connections = state->options.min_connections;
    }

    std::deque<std::unique_ptr<detail::ConnectionRecord>> initial;
    try {
        for (std::size_t index = 0;
             index < state->options.min_connections; ++index) {
            auto connection = state->factory();
            if (!connection) {
                throw DbError("connection factory returned null");
            }
            initial.push_back(std::make_unique<detail::ConnectionRecord>(
                std::move(connection)));
        }
    } catch (...) {
        std::lock_guard lock(state->mutex);
        ++state->creation_failures;
        state->creating_connections = 0;
        state->state = ConnectionPoolState::Stopped;
        throw;
    }

    {
        std::lock_guard lock(state->mutex);
        state->creating_connections = 0;
        state->total_connections = initial.size();
        state->peak_connections = state->total_connections;
        state->idle.swap(initial);
        state->state = ConnectionPoolState::Running;
    }

    try {
        state->maintenance_thread = std::jthread(
            [raw_state = state.get()](std::stop_token token) {
                detail::maintenanceLoop(raw_state, token);
            });
    } catch (...) {
        std::lock_guard lock(state->mutex);
        state->idle.clear();
        state->total_connections = 0;
        state->state = ConnectionPoolState::Stopped;
        throw;
    }
    state->available.notify_all();
}

ConnectionLease ConnectionPool::acquire() {
    return acquireFor(impl_->shared->options.acquire_timeout);
}

ConnectionLease ConnectionPool::acquireFor(
    std::chrono::milliseconds timeout) {
    return ConnectionLease(impl_->shared, impl_->acquireRecord(timeout));
}

bool ConnectionPool::shutdown() {
    return shutdownFor(impl_->shared->options.shutdown_timeout);
}

bool ConnectionPool::shutdownFor(std::chrono::milliseconds timeout) {
    if (timeout < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("shutdown timeout must not be negative");
    }

    auto state = impl_->shared;
    std::lock_guard shutdown_lock(state->shutdown_mutex);
    std::deque<std::unique_ptr<detail::ConnectionRecord>> idle;

    {
        std::lock_guard lock(state->mutex);
        if (state->state == ConnectionPoolState::Created) {
            state->state = ConnectionPoolState::Stopped;
            return true;
        }
        if (state->state == ConnectionPoolState::Stopped) {
            return state->borrowed_connections == 0 &&
                   state->creating_connections == 0;
        }

        state->state = ConnectionPoolState::Stopping;
        idle.swap(state->idle);
        state->total_connections -= idle.size();
        state->maintenance_thread.request_stop();
    }

    state->available.notify_all();
    state->maintenance_wakeup.notify_all();
    idle.clear();

    if (state->maintenance_thread.joinable()) {
        state->maintenance_thread.join();
    }

    std::unique_lock lock(state->mutex);
    const bool drained = state->lifecycle_changed.wait_for(
        lock, timeout, [&state] {
            return state->borrowed_connections == 0 &&
                   state->creating_connections == 0;
        });
    state->state = ConnectionPoolState::Stopped;
    return drained;
}

ConnectionPoolState ConnectionPool::state() const {
    std::lock_guard lock(impl_->shared->mutex);
    return impl_->shared->state;
}

ConnectionPoolStatistics ConnectionPool::statistics() const {
    auto state = impl_->shared;
    std::lock_guard lock(state->mutex);
    return ConnectionPoolStatistics{
        .state = state->state,
        .min_connections = state->options.min_connections,
        .max_connections = state->options.max_connections,
        .total_connections = state->total_connections,
        .idle_connections = state->idle.size(),
        .borrowed_connections = state->borrowed_connections,
        .creating_connections = state->creating_connections,
        .waiting_borrowers = state->waiting_borrowers,
        .peak_connections = state->peak_connections,
        .peak_borrowed_connections = state->peak_borrowed_connections,
        .successful_acquires = state->successful_acquires,
        .acquire_timeouts = state->acquire_timeouts,
        .creation_failures = state->creation_failures,
        .validation_failures = state->validation_failures,
        .discarded_connections = state->discarded_connections,
        .cumulative_wait_microseconds = state->cumulative_wait_microseconds,
        .max_wait_microseconds = state->max_wait_microseconds,
    };
}

}  // namespace water::db
