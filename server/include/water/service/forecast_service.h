#pragma once

#include "water/concurrency/thread_pool.h"
#include "water/db/db_executor.h"
#include "water/domain/forecast_data.h"
#include "water/inference/forecaster.h"
#include "water/repository/water_repository.h"
#include "water/service/water_service.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace water::service {

using ForecastResult = std::variant<domain::ForecastResult, ServiceError>;

class ForecastService final {
public:
    ForecastService(db::DbExecutor& database, concurrency::ThreadPool& inference_workers,
                    inference::Forecaster& forecaster, const repository::WaterRepository& repository);
    void forecast(std::int64_t company_id, std::string dataset,
                  std::int64_t end_sample_id, std::size_t horizon,
                  std::function<void(ForecastResult)> completion);
    [[nodiscard]] bool modelAvailable() const noexcept;
    [[nodiscard]] std::string modelName() const;
    [[nodiscard]] std::string modelVersion() const;
private:
    db::DbExecutor& database_;
    concurrency::ThreadPool& inference_workers_;
    inference::Forecaster& forecaster_;
    const repository::WaterRepository& repository_;
};

}  // namespace water::service
