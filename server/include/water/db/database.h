#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace water::db {

using DbBlob = std::vector<std::byte>;
using DbValue =
    std::variant<std::monostate, std::int64_t, std::uint64_t, double,
                 std::string, DbBlob>;
using DbParameters = std::vector<DbValue>;
using DbCell = std::optional<std::string>;
using DbRow = std::unordered_map<std::string, DbCell>;
using DbRows = std::vector<DbRow>;

struct ExecuteResult {
    std::uint64_t affected_rows{0};
    std::uint64_t last_insert_id{0};
};

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A connection is leased to exactly one thread at a time. Concrete drivers
// must provide parameterized execution; callers must not interpolate external
// input into SQL strings.
class DbConnection {
public:
    virtual ~DbConnection() = default;

    virtual bool ping() noexcept = 0;
    // Restore a safe reusable session: rollback an open transaction, enable
    // autocommit, and clear driver-local transient state.
    virtual bool resetSession() noexcept = 0;

    virtual ExecuteResult execute(
        std::string_view sql,
        std::span<const DbValue> parameters = {}) = 0;
    virtual DbRows query(std::string_view sql,
                         std::span<const DbValue> parameters = {}) = 0;

    virtual void beginTransaction() = 0;
    virtual void commit() = 0;
    virtual void rollback() noexcept = 0;
};

// Rolls a transaction back unless commit() or rollback() completed.
class Transaction final {
public:
    explicit Transaction(DbConnection& connection);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    void commit();
    void rollback() noexcept;

private:
    DbConnection* connection_;
    bool active_{true};
};

}  // namespace water::db
