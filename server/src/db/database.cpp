#include "water/db/database.h"

namespace water::db {

Transaction::Transaction(DbConnection& connection) : connection_(&connection) {
    connection_->beginTransaction();
}

Transaction::~Transaction() {
    rollback();
}

void Transaction::commit() {
    if (!active_) {
        throw DbError("transaction is no longer active");
    }
    connection_->commit();
    active_ = false;
}

void Transaction::rollback() noexcept {
    if (!active_) {
        return;
    }
    connection_->rollback();
    active_ = false;
}

}  // namespace water::db
