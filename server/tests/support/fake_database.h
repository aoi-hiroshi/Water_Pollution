#pragma once

#include "water/db/database.h"

#include <atomic>
#include <memory>
#include <string>

namespace water::db::test {

struct FakeDatabaseState {
    std::atomic_int next_id{1};
    std::atomic_int created{0};
    std::atomic_int destroyed{0};
    std::atomic_int pings{0};
    std::atomic_int resets{0};
    std::atomic_int executions{0};
    std::atomic_int queries{0};
    std::atomic_int begins{0};
    std::atomic_int commits{0};
    std::atomic_int rollbacks{0};
};

class FakeConnection final : public DbConnection {
public:
    explicit FakeConnection(std::shared_ptr<FakeDatabaseState> state)
        : state_(std::move(state)), id_(state_->next_id.fetch_add(1)) {
        state_->created.fetch_add(1);
    }

    ~FakeConnection() override { state_->destroyed.fetch_add(1); }

    bool ping() noexcept override {
        state_->pings.fetch_add(1);
        return ping_healthy_;
    }

    bool resetSession() noexcept override {
        state_->resets.fetch_add(1);
        if (in_transaction_) {
            rollback();
        }
        return reset_healthy_;
    }

    ExecuteResult execute(std::string_view,
                          std::span<const DbValue>) override {
        state_->executions.fetch_add(1);
        return {.affected_rows = 1,
                .last_insert_id = static_cast<std::uint64_t>(id_)};
    }

    DbRows query(std::string_view sql,
                 std::span<const DbValue>) override {
        state_->queries.fetch_add(1);
        return {{{"connection_id", std::to_string(id_)},
                 {"sql", std::string{sql}}}};
    }

    void beginTransaction() override {
        if (in_transaction_) {
            throw DbError("transaction already active");
        }
        in_transaction_ = true;
        state_->begins.fetch_add(1);
    }

    void commit() override {
        if (!in_transaction_) {
            throw DbError("no active transaction");
        }
        in_transaction_ = false;
        state_->commits.fetch_add(1);
    }

    void rollback() noexcept override {
        if (in_transaction_) {
            in_transaction_ = false;
            state_->rollbacks.fetch_add(1);
        }
    }

    [[nodiscard]] int id() const noexcept { return id_; }
    void setPingHealthy(bool healthy) noexcept { ping_healthy_ = healthy; }
    void setResetHealthy(bool healthy) noexcept { reset_healthy_ = healthy; }

private:
    std::shared_ptr<FakeDatabaseState> state_;
    int id_;
    bool ping_healthy_{true};
    bool reset_healthy_{true};
    bool in_transaction_{false};
};

inline auto makeFakeFactory(std::shared_ptr<FakeDatabaseState> state) {
    return [state = std::move(state)]() -> std::unique_ptr<DbConnection> {
        return std::make_unique<FakeConnection>(state);
    };
}

}  // namespace water::db::test
