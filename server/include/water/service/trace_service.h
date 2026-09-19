#pragma once

#include "water/concurrency/thread_pool.h"
#include "water/db/db_executor.h"
#include "water/domain/trace_data.h"
#include "water/inference/trace_classifier.h"
#include "water/repository/water_repository.h"
#include "water/service/water_service.h"

#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace water::service {

using TraceResult = std::variant<domain::TraceResult, ServiceError>;

// Orchestrates the two blocking stages without occupying a Muduo IO thread:
// database workers load a sample, then inference workers execute ONNX Runtime.
class TraceService final {
public:
    TraceService(db::DbExecutor& database,
                 concurrency::ThreadPool& inference_workers,
                 inference::TraceClassifier& classifier,
                 const repository::WaterRepository& repository);

    void classifySample(std::int64_t sample_id, std::string dataset,
                        std::function<void(TraceResult)> completion,
                        std::int64_t cleaning_run_id = 0);

    [[nodiscard]] bool modelAvailable() const noexcept;
    [[nodiscard]] std::string modelName() const;
    [[nodiscard]] std::string modelVersion() const;

private:
    db::DbExecutor& database_;
    concurrency::ThreadPool& inference_workers_;
    inference::TraceClassifier& classifier_;
    const repository::WaterRepository& repository_;
};

}  // namespace water::service
