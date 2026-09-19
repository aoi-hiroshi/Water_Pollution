#include "water/db/db_executor.h"

#include <stdexcept>
#include <utility>

namespace water::db {

namespace {

DbExecutorOptions validate(DbExecutorOptions options) {
    if (options.workers.max_threads >
        options.connection_pool.max_connections) {
        throw std::invalid_argument(
            "database worker max_threads must not exceed max_connections");
    }
    return options;
}

}  // namespace

DbExecutor::DbExecutor(DbExecutorOptions options,
                       ConnectionPool::ConnectionFactory factory)
    : DbExecutor(validate(std::move(options)), std::move(factory), 0) {}

// Private delegating-constructor implementation is expressed with a local tag
// parameter to ensure validation runs before members consume the options.
DbExecutor::DbExecutor(DbExecutorOptions options,
                       ConnectionPool::ConnectionFactory factory,
                       int)
    : acquire_timeout_(options.connection_pool.acquire_timeout),
      connection_pool_(options.connection_pool, std::move(factory)),
      workers_(std::move(options.workers)) {}

DbExecutor::~DbExecutor() {
    shutdown();
}

void DbExecutor::start() {
    connection_pool_.start();
    try {
        workers_.start();
    } catch (...) {
        static_cast<void>(connection_pool_.shutdown());
        throw;
    }
}

void DbExecutor::shutdown(concurrency::ShutdownMode mode) {
    workers_.shutdown(mode);
    static_cast<void>(connection_pool_.shutdown());
}

bool DbExecutor::post(FireAndForgetTask task) {
    return post(std::move(task), {});
}

bool DbExecutor::post(FireAndForgetTask task,
                       std::function<void(std::exception_ptr)> on_error) {
    if (!task) {
        throw std::invalid_argument("database task must not be empty");
    }
    return workers_.post([this, task = std::move(task), on_error = std::move(on_error)]() mutable {
        try {
            auto lease = connection_pool_.acquireFor(acquire_timeout_);
            task(*lease);
        } catch (...) {
            if (on_error) { on_error(std::current_exception()); }
            else { throw; }
        }
    });
}

DbExecutorStatistics DbExecutor::statistics() const {
    return DbExecutorStatistics{
        .connections = connection_pool_.statistics(),
        .workers = workers_.statistics(),
    };
}

}  // namespace water::db
