#pragma once

#include "water/analysis/forecast_data_analysis.h"
#include "water/service/water_service.h"

#include <functional>
#include <memory>
#include <variant>

namespace water::service {

using ForecastDataResult = std::variant<domain::ForecastDataResult, ServiceError>;

class ForecastDataService final {
public:
    ForecastDataService(db::DbExecutor& database,
                        concurrency::ThreadPool& workers,
                        const repository::WaterRepository& repository);
    ~ForecastDataService();
    ForecastDataService(const ForecastDataService&) = delete;
    ForecastDataService& operator=(const ForecastDataService&) = delete;

    void execute(domain::ForecastDataRequest request,
                 std::function<void(ForecastDataResult)> completion);
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
