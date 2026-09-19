#pragma once

#include "water/analysis/classification_analysis.h"
#include "water/service/water_service.h"

#include <functional>
#include <memory>
#include <variant>

namespace water::service {
using ClassificationDataResult = std::variant<domain::ClassificationDataResult, ServiceError>;

class ClassificationDataService final {
public:
    ClassificationDataService(db::DbExecutor& database, concurrency::ThreadPool& workers,
                              const repository::WaterRepository& repository);
    ~ClassificationDataService();
    ClassificationDataService(const ClassificationDataService&) = delete;
    ClassificationDataService& operator=(const ClassificationDataService&) = delete;

    void execute(domain::ClassificationDataRequest request,
                 std::function<void(ClassificationDataResult)> completion);
    // Owner thread only. Close admission and drain DB -> CPU -> optional DB
    // save operations BEFORE shutting down either underlying executor.
    void shutdown();

private:
    struct State;
    struct Ticket;
    std::shared_ptr<State> state_;
    db::DbExecutor& database_;
    concurrency::ThreadPool& workers_;
    const repository::WaterRepository& repository_;
};
} // namespace water::service
