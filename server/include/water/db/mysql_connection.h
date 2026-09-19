#pragma once

#include "water/db/database.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace water::db {

struct MySqlConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string user;
    std::string password;
    std::string database;
    std::string charset{"utf8mb4"};
    std::uint32_t connect_timeout_seconds{3};
    std::uint32_t read_timeout_seconds{5};
    std::uint32_t write_timeout_seconds{5};
    std::size_t max_result_cell_bytes{4U * 1024U * 1024U};
};

class MySqlConnection final : public DbConnection {
public:
    explicit MySqlConnection(MySqlConfig config);
    ~MySqlConnection() override;

    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;
    MySqlConnection(MySqlConnection&&) = delete;
    MySqlConnection& operator=(MySqlConnection&&) = delete;

    bool ping() noexcept override;
    bool resetSession() noexcept override;

    ExecuteResult execute(
        std::string_view sql,
        std::span<const DbValue> parameters = {}) override;
    DbRows query(std::string_view sql,
                 std::span<const DbValue> parameters = {}) override;

    void beginTransaction() override;
    void commit() override;
    void rollback() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using DbConnectionFactory =
    std::function<std::unique_ptr<DbConnection>()>;

[[nodiscard]] DbConnectionFactory makeMySqlConnectionFactory(
    MySqlConfig config);

}  // namespace water::db
