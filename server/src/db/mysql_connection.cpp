#include "water/db/mysql_connection.h"

#include <mysql.h>

#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>

namespace water::db {

namespace {

void initializeMySqlLibrary() {
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        result = mysql_library_init(0, nullptr, nullptr);
    });
    if (result != 0) {
        throw DbError("mysql_library_init failed");
    }
}

void initializeMySqlThread() {
    struct ThreadContext {
        ThreadContext() {
            if (mysql_thread_init() != 0) {
                throw DbError("mysql_thread_init failed");
            }
        }
        ~ThreadContext() { mysql_thread_end(); }
    };
    thread_local ThreadContext context;
    static_cast<void>(context);
}

std::string connectionError(MYSQL* connection,
                            std::string_view operation) {
    return std::string{operation} + " failed [" +
           std::to_string(mysql_errno(connection)) + "]: " +
           mysql_error(connection);
}

std::string statementError(MYSQL_STMT* statement,
                           std::string_view operation) {
    return std::string{operation} + " failed [" +
           std::to_string(mysql_stmt_errno(statement)) + "]: " +
           mysql_stmt_error(statement);
}

struct StatementDeleter {
    void operator()(MYSQL_STMT* statement) const noexcept {
        if (statement) {
            mysql_stmt_close(statement);
        }
    }
};

struct MetadataDeleter {
    void operator()(MYSQL_RES* metadata) const noexcept {
        if (metadata) {
            mysql_free_result(metadata);
        }
    }
};

using StatementPtr = std::unique_ptr<MYSQL_STMT, StatementDeleter>;
using MetadataPtr = std::unique_ptr<MYSQL_RES, MetadataDeleter>;

struct ParameterStorage {
    std::int64_t signed_value{0};
    std::uint64_t unsigned_value{0};
    double double_value{0};
    std::string string_value;
    DbBlob blob_value;
    unsigned long length{0};
    bool is_null{false};
};

struct PreparedStatement {
    StatementPtr statement;
    std::vector<MYSQL_BIND> bindings;
    std::vector<ParameterStorage> storage;
};

struct ResultStorage {
    std::vector<char> buffer;
    unsigned long length{0};
    bool is_null{false};
    bool error{false};
};

PreparedStatement prepareStatement(MYSQL* connection,
                                   std::string_view sql,
                                   std::span<const DbValue> parameters) {
    if (sql.size() > std::numeric_limits<unsigned long>::max()) {
        throw DbError("SQL statement is too large");
    }

    StatementPtr statement(mysql_stmt_init(connection));
    if (!statement) {
        throw DbError(connectionError(connection, "mysql_stmt_init"));
    }
    if (mysql_stmt_prepare(statement.get(), sql.data(),
                           static_cast<unsigned long>(sql.size())) != 0) {
        throw DbError(statementError(statement.get(), "mysql_stmt_prepare"));
    }
    if (mysql_stmt_param_count(statement.get()) != parameters.size()) {
        throw DbError("SQL placeholder count does not match parameter count");
    }

    PreparedStatement prepared;
    prepared.statement = std::move(statement);
    prepared.bindings.resize(parameters.size());
    prepared.storage.resize(parameters.size());

    for (std::size_t index = 0; index < parameters.size(); ++index) {
        auto& binding = prepared.bindings[index];
        auto& value = prepared.storage[index];
        std::memset(&binding, 0, sizeof(binding));

        std::visit(
            [&binding, &value](const auto& parameter) {
                using Type = std::decay_t<decltype(parameter)>;
                if constexpr (std::is_same_v<Type, std::monostate>) {
                    value.is_null = true;
                    binding.buffer_type = MYSQL_TYPE_NULL;
                    binding.is_null = &value.is_null;
                } else if constexpr (std::is_same_v<Type, std::int64_t>) {
                    value.signed_value = parameter;
                    binding.buffer_type = MYSQL_TYPE_LONGLONG;
                    binding.buffer = &value.signed_value;
                    binding.is_unsigned = false;
                } else if constexpr (std::is_same_v<Type, std::uint64_t>) {
                    value.unsigned_value = parameter;
                    binding.buffer_type = MYSQL_TYPE_LONGLONG;
                    binding.buffer = &value.unsigned_value;
                    binding.is_unsigned = true;
                } else if constexpr (std::is_same_v<Type, double>) {
                    value.double_value = parameter;
                    binding.buffer_type = MYSQL_TYPE_DOUBLE;
                    binding.buffer = &value.double_value;
                } else if constexpr (std::is_same_v<Type, std::string>) {
                    value.string_value = parameter;
                    if (value.string_value.size() > ULONG_MAX) {
                        throw DbError("string parameter is too large");
                    }
                    value.length = static_cast<unsigned long>(
                        value.string_value.size());
                    binding.buffer_type = MYSQL_TYPE_STRING;
                    binding.buffer = value.string_value.data();
                    binding.buffer_length = value.length;
                    binding.length = &value.length;
                } else if constexpr (std::is_same_v<Type, DbBlob>) {
                    value.blob_value = parameter;
                    if (value.blob_value.size() > ULONG_MAX) {
                        throw DbError("blob parameter is too large");
                    }
                    value.length = static_cast<unsigned long>(
                        value.blob_value.size());
                    binding.buffer_type = MYSQL_TYPE_BLOB;
                    binding.buffer = value.blob_value.data();
                    binding.buffer_length = value.length;
                    binding.length = &value.length;
                }
            },
            parameters[index]);
    }

    if (!prepared.bindings.empty() &&
        mysql_stmt_bind_param(prepared.statement.get(),
                              prepared.bindings.data()) != 0) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_bind_param"));
    }
    return prepared;
}

void executePrepared(PreparedStatement& prepared) {
    if (mysql_stmt_execute(prepared.statement.get()) != 0) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_execute"));
    }
}

}  // namespace

struct MySqlConnection::Impl {
    explicit Impl(MySqlConfig connection_config)
        : config(std::move(connection_config)) {
        if (config.user.empty() || config.database.empty()) {
            throw std::invalid_argument("MySQL user and database are required");
        }
        if (config.max_result_cell_bytes == 0) {
            throw std::invalid_argument("max_result_cell_bytes must be positive");
        }

        initializeMySqlLibrary();
        initializeMySqlThread();
        connection = mysql_init(nullptr);
        if (!connection) {
            throw DbError("mysql_init failed");
        }

        try {
            configure();
            connect();
        } catch (...) {
            mysql_close(connection);
            connection = nullptr;
            throw;
        }
    }

    ~Impl() {
        if (connection) {
            try {
                initializeMySqlThread();
            } catch (...) {
                // mysql_close is still the only safe resource release here.
            }
            mysql_close(connection);
        }
    }

    void configure() {
        auto set_option = [this](mysql_option option,
                                 const void* value,
                                 std::string_view name) {
            if (mysql_options(connection, option, value) != 0) {
                throw DbError(connectionError(connection, name));
            }
        };

        unsigned int connect_timeout = config.connect_timeout_seconds;
        unsigned int read_timeout = config.read_timeout_seconds;
        unsigned int write_timeout = config.write_timeout_seconds;
        bool reconnect = false;
        set_option(MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout,
                   "MYSQL_OPT_CONNECT_TIMEOUT");
        set_option(MYSQL_OPT_READ_TIMEOUT, &read_timeout,
                   "MYSQL_OPT_READ_TIMEOUT");
        set_option(MYSQL_OPT_WRITE_TIMEOUT, &write_timeout,
                   "MYSQL_OPT_WRITE_TIMEOUT");
        set_option(MYSQL_OPT_RECONNECT, &reconnect,
                   "MYSQL_OPT_RECONNECT");
        set_option(MYSQL_SET_CHARSET_NAME, config.charset.c_str(),
                   "MYSQL_SET_CHARSET_NAME");
    }

    void connect() {
        if (!mysql_real_connect(connection, config.host.c_str(),
                                config.user.c_str(), config.password.c_str(),
                                config.database.c_str(), config.port, nullptr,
                                CLIENT_MULTI_RESULTS)) {
            throw DbError(connectionError(connection, "mysql_real_connect"));
        }
    }

    MySqlConfig config;
    MYSQL* connection{nullptr};
    bool transaction_active{false};
};

MySqlConnection::MySqlConnection(MySqlConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MySqlConnection::~MySqlConnection() = default;

bool MySqlConnection::ping() noexcept {
    try {
        initializeMySqlThread();
        return mysql_ping(impl_->connection) == 0;
    } catch (...) {
        return false;
    }
}

bool MySqlConnection::resetSession() noexcept {
    try {
        initializeMySqlThread();
        if (!impl_->transaction_active) {
            return true;
        }
        const bool rolled_back = mysql_rollback(impl_->connection) == 0;
        const bool autocommit_enabled =
            mysql_autocommit(impl_->connection, true) == 0;
        impl_->transaction_active = false;
        return rolled_back && autocommit_enabled;
    } catch (...) {
        return false;
    }
}

ExecuteResult MySqlConnection::execute(
    std::string_view sql,
    std::span<const DbValue> parameters) {
    initializeMySqlThread();
    auto prepared = prepareStatement(impl_->connection, sql, parameters);
    executePrepared(prepared);

    const auto affected = mysql_stmt_affected_rows(prepared.statement.get());
    if (affected == MYSQL_COUNT_ERROR) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_affected_rows"));
    }
    return {
        .affected_rows = static_cast<std::uint64_t>(affected),
        .last_insert_id = static_cast<std::uint64_t>(
            mysql_stmt_insert_id(prepared.statement.get())),
    };
}

DbRows MySqlConnection::query(
    std::string_view sql,
    std::span<const DbValue> parameters) {
    initializeMySqlThread();
    auto prepared = prepareStatement(impl_->connection, sql, parameters);

    bool update_max_length = true;
    if (mysql_stmt_attr_set(prepared.statement.get(),
                            STMT_ATTR_UPDATE_MAX_LENGTH,
                            &update_max_length) != 0) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_attr_set"));
    }
    executePrepared(prepared);

    MetadataPtr metadata(mysql_stmt_result_metadata(prepared.statement.get()));
    if (!metadata) {
        if (mysql_stmt_field_count(prepared.statement.get()) == 0) {
            return {};
        }
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_result_metadata"));
    }
    if (mysql_stmt_store_result(prepared.statement.get()) != 0) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_store_result"));
    }

    const auto field_count = mysql_num_fields(metadata.get());
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata.get());
    std::vector<MYSQL_BIND> bindings(field_count);
    std::vector<ResultStorage> storage(field_count);

    for (unsigned int index = 0; index < field_count; ++index) {
        if (fields[index].max_length > impl_->config.max_result_cell_bytes) {
            throw DbError("query result cell exceeds max_result_cell_bytes");
        }

        auto& value = storage[index];
        value.buffer.resize(static_cast<std::size_t>(fields[index].max_length) +
                            1U);

        auto& binding = bindings[index];
        std::memset(&binding, 0, sizeof(binding));
        binding.buffer_type = MYSQL_TYPE_STRING;
        binding.buffer = value.buffer.data();
        binding.buffer_length = static_cast<unsigned long>(value.buffer.size());
        binding.length = &value.length;
        binding.is_null = &value.is_null;
        binding.error = &value.error;
    }

    if (field_count > 0 &&
        mysql_stmt_bind_result(prepared.statement.get(), bindings.data()) != 0) {
        throw DbError(statementError(prepared.statement.get(),
                                     "mysql_stmt_bind_result"));
    }

    DbRows rows;
    const auto row_count = mysql_stmt_num_rows(prepared.statement.get());
    if (row_count <= std::numeric_limits<std::size_t>::max()) {
        rows.reserve(static_cast<std::size_t>(row_count));
    }

    for (;;) {
        const int fetch_result = mysql_stmt_fetch(prepared.statement.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            throw DbError(statementError(prepared.statement.get(),
                                         "mysql_stmt_fetch"));
        }

        DbRow row;
        row.reserve(field_count);
        for (unsigned int index = 0; index < field_count; ++index) {
            const auto& value = storage[index];
            if (value.error || value.length > value.buffer.size()) {
                throw DbError("query result was truncated");
            }

            std::string name(fields[index].name, fields[index].name_length);
            if (value.is_null) {
                row.emplace(std::move(name), std::nullopt);
            } else {
                row.emplace(
                    std::move(name),
                    std::string(value.buffer.data(), value.length));
            }
        }
        rows.push_back(std::move(row));
    }

    return rows;
}

void MySqlConnection::beginTransaction() {
    initializeMySqlThread();
    if (impl_->transaction_active) {
        throw DbError("transaction is already active");
    }
    if (mysql_autocommit(impl_->connection, false) != 0) {
        throw DbError(connectionError(impl_->connection, "mysql_autocommit(false)"));
    }
    impl_->transaction_active = true;
}

void MySqlConnection::commit() {
    initializeMySqlThread();
    if (!impl_->transaction_active) {
        throw DbError("no active transaction to commit");
    }
    if (mysql_commit(impl_->connection) != 0) {
        throw DbError(connectionError(impl_->connection, "mysql_commit"));
    }
    if (mysql_autocommit(impl_->connection, true) != 0) {
        throw DbError(connectionError(impl_->connection, "mysql_autocommit(true)"));
    }
    impl_->transaction_active = false;
}

void MySqlConnection::rollback() noexcept {
    if (!impl_->transaction_active) {
        return;
    }
    try {
        initializeMySqlThread();
        mysql_rollback(impl_->connection);
        mysql_autocommit(impl_->connection, true);
    } catch (...) {
    }
    impl_->transaction_active = false;
}

DbConnectionFactory makeMySqlConnectionFactory(MySqlConfig config) {
    return [config = std::move(config)]() mutable {
        return std::make_unique<MySqlConnection>(config);
    };
}

}  // namespace water::db
