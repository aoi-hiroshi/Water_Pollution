# Database Connection Pool

The database layer is split into four parts:

```text
Muduo HTTP handler
    -> DbExecutor (bounded database worker queue)
        -> ConnectionPool (bounded database connections)
            -> DbConnection / MySqlConnection
                -> MySQL
```

Muduo I/O threads only enqueue database work. They never call `future::get()`,
wait for a connection, or execute blocking SQL.

## Pool behavior

1. `start()` creates `min_connections`.
2. `acquire()` reuses idle connections and creates on demand up to
   `max_connections`.
3. Borrowers wait no longer than `acquire_timeout` when capacity is exhausted.
4. A `ConnectionLease` gives one connection exclusively to one thread.
5. Lease destruction resets the session and returns it. Failed reset discards
   the connection.
6. Idle elastic connections retire down to `min_connections`.
7. `shutdown()` closes idle connections and waits a bounded time for leases.

Connections are validated with `ping()` after `validation_interval`. Blocking
driver operations run without holding the pool mutex.

## Startup

```cpp
using namespace std::chrono_literals;
using namespace water::db;

MySqlConfig mysql{
    .host = config.db_host,
    .port = config.db_port,
    .user = config.db_user,
    .password = config.db_password,
    .database = config.db_name,
};

DbExecutorOptions options;
options.connection_pool.min_connections = 2;
options.connection_pool.max_connections = 8;
options.connection_pool.acquire_timeout = 300ms;
options.workers.core_threads = 2;
options.workers.max_threads = 8;
options.workers.queue_capacity = 512;

DbExecutor database(options, makeMySqlConnectionFactory(std::move(mysql)));
database.start();
```

Passwords must come from environment variables or protected deployment
secrets. They must not be committed to source code or written to logs.

## Repository operation

Repository code depends on `DbConnection`, not on `MYSQL*`:

```cpp
auto future = database.submit([](DbConnection& connection) {
    DbParameters parameters{std::int64_t{42}};
    return connection.query(
        "SELECT company_id, company_name "
        "FROM company_info WHERE company_id = ?",
        parameters);
});
```

This form is appropriate for non-I/O callers. Never call `future.get()` from a
Muduo EventLoop. An HTTP handler should use `database.post()`, return from the
callback, and later call `EventLoop::queueInLoop()` to send the serialized
result. Queue rejection maps to HTTP 503; acquisition timeout also maps to a
temporary database-unavailable response.

## Transactions

```cpp
database.submit([](DbConnection& connection) {
    Transaction transaction(connection);
    DbParameters task_parameters{
        std::string{"classification"}, std::string{"QUEUED"}};
    connection.execute(
        "INSERT INTO tasks(type, status) VALUES(?, ?)",
        task_parameters);
    DbParameters audit_parameters{std::string{"task.created"}};
    connection.execute(
        "INSERT INTO audit_events(action) VALUES(?)",
        audit_parameters);
    transaction.commit();
});
```

An exception before `commit()` causes automatic rollback. The lease performs a
second defensive session reset before a connection can be reused.

## MySQL build

The generic pool and tests do not require MySQL. Enable the real adapter with:

```text
cmake -S server -B build/server -DWATER_WITH_MYSQL=ON
cmake --build build/server
ctest --test-dir build/server
```

The server must link the MySQL client library and make its runtime shared
library available. Network connect/read/write timeouts are configured in
`MySqlConfig`; automatic driver reconnect is disabled because reconnecting in
the middle of a transaction can silently change session state.

The real integration test reads `WATER_DB_USER`, `WATER_DB_PASSWORD`, and
`WATER_DB_NAME`; `WATER_DB_HOST` is optional. When these variables are absent,
CTest marks the test as skipped instead of embedding credentials in source.

## Metrics

`statistics()` exposes total, idle, borrowed and creating connections, waiting
borrowers, peak usage, successful acquisitions, timeouts, creation failures,
validation failures, discarded connections, and acquisition wait time. These
values can later be exported through `/metrics`.
