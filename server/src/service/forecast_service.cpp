#include "water/service/forecast_service.h"

#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace water::service {

namespace {

std::vector<float> toWindow(const std::vector<domain::WaterSample>& samples,
                            std::int64_t company_id) {
    std::vector<float> window;
    window.reserve(samples.size() * inference::kForecastFeatureCount);
    std::int64_t previous_id = 0;
    for (const auto& sample : samples) {
        if (sample.company_id != company_id || sample.id <= previous_id) {
            throw std::invalid_argument("forecast history must be an ordered single-company series");
        }
        previous_id = sample.id;
        // NOT classification order: pollutants first, then the six auxiliaries.
        const std::array<std::optional<double>, 10> values{
            sample.cod, sample.nh3n, sample.tp, sample.turbidity,
            sample.temperature, sample.ph, sample.water_level, sample.orp,
            sample.conductivity, sample.dissolved_oxygen};
        for (const auto& value : values) {
            if (!value || !std::isfinite(*value) ||
                std::abs(*value) > static_cast<double>(std::numeric_limits<float>::max())) {
                throw std::invalid_argument("forecast history contains NULL or an invalid feature");
            }
            window.push_back(static_cast<float>(*value));
        }
    }
    return window;
}

domain::ForecastResult buildResult(
    const domain::Company& company, std::string dataset,
    const std::vector<domain::WaterSample>& samples, const std::vector<float>& window,
    std::size_t horizon, const inference::ForecastModelPrediction& predictions,
    const inference::Forecaster& model) {
    if (predictions.size() != inference::kForecastHorizon) {
        throw inference::InferenceError("forecast model must return ten steps");
    }
    // Validate the complete model output even if the user displays a prefix.
    for (const auto& point : predictions) {
        for (const float value : point) {
            if (!std::isfinite(value)) { throw inference::InferenceError("invalid forecast output"); }
        }
    }
    domain::ForecastResult result;
    result.company_id = company.id;
    result.company_name = company.name;
    result.dataset = std::move(dataset);
    result.start_sample_id = samples.front().id;
    result.end_sample_id = samples.back().id;
    result.lookback = samples.size();
    result.model_name = model.modelName();
    result.model_version = model.modelVersion();
    for (std::size_t index = 0; index < samples.size(); ++index) {
        domain::ForecastPoint point;
        point.step = index + 1;
        for (std::size_t target = 0; target < 4; ++target) {
            point.values[target] = window[index * inference::kForecastFeatureCount + target];
        }
        result.history.push_back(point);
    }
    for (std::size_t index = 0; index < horizon; ++index) {
        domain::ForecastPoint point;
        point.step = index + 1;
        for (std::size_t target = 0; target < 4; ++target) { point.values[target] = predictions[index][target]; }
        result.predictions.push_back(point);
    }
    return result;
}

}  // namespace

ForecastService::ForecastService(db::DbExecutor& database, concurrency::ThreadPool& inference_workers,
                                 inference::Forecaster& forecaster, const repository::WaterRepository& repository)
    : database_(database), inference_workers_(inference_workers), forecaster_(forecaster), repository_(repository) {}

void ForecastService::forecast(std::int64_t company_id, std::string dataset,
                               std::int64_t end_sample_id, std::size_t horizon,
                               std::function<void(ForecastResult)> completion) {
    if (!completion) { throw std::invalid_argument("forecast completion must not be empty"); }
    if (company_id <= 0 || end_sample_id < 0 || horizon == 0 || horizon > inference::kForecastHorizon ||
        (dataset != "train_data" && dataset != "val_data" &&
         dataset != "test_data")) {
        completion(ServiceError{400, "INVALID_ARGUMENT", "invalid company_id, dataset, end_sample_id or horizon"});
        return;
    }
    if (!forecaster_.available()) {
        completion(ServiceError{503, "INFERENCE_UNAVAILABLE", "forecast ONNX model is not loaded"});
        return;
    }
    auto callback = std::make_shared<decltype(completion)>(std::move(completion));
    const bool accepted = database_.post(
        [this, callback, company_id, end_sample_id, horizon, dataset = std::move(dataset)](db::DbConnection& connection) mutable {
            const auto company = repository_.findCompany(connection, company_id);
            if (!company) {
                (*callback)(ServiceError{404, "COMPANY_NOT_FOUND", "forecast company was not found"});
                return;
            }
            auto samples = repository_.loadForecastWindow(connection, company_id, dataset, end_sample_id, inference::kForecastLookback);
            if (end_sample_id > 0 && (samples.empty() || samples.back().id != end_sample_id)) {
                (*callback)(ServiceError{404, "FORECAST_SAMPLE_NOT_FOUND", "end sample was not found for this company/dataset"});
                return;
            }
            if (samples.size() != inference::kForecastLookback) {
                (*callback)(ServiceError{422, "INSUFFICIENT_HISTORY", "forecast requires 120 history rows from the same company"});
                return;
            }
            const bool inference_accepted = inference_workers_.post(
                [this, callback, company = *company, horizon, dataset = std::move(dataset), samples = std::move(samples)]() mutable {
                    ForecastResult result;
                    try {
                        const auto window = toWindow(samples, company.id);
                        const auto predictions = forecaster_.forecast(window);
                        result = buildResult(company, std::move(dataset), samples, window, horizon, predictions, forecaster_);
                    } catch (const inference::InferenceUnavailable& error) {
                        result = ServiceError{503, "INFERENCE_UNAVAILABLE", error.what()};
                    } catch (const std::invalid_argument& error) {
                        result = ServiceError{422, "INVALID_FORECAST_HISTORY", error.what()};
                    } catch (...) {
                        result = ServiceError{500, "INFERENCE_ERROR", "forecast model inference failed"};
                    }
                    (*callback)(std::move(result));
                });
            if (!inference_accepted) { (*callback)(ServiceError{503, "INFERENCE_BUSY", "inference task queue is full"}); }
        },
        [callback](std::exception_ptr exception) {
            ServiceError error{500, "DATABASE_ERROR", "database operation failed"};
            try { std::rethrow_exception(exception); }
            catch (const db::ConnectionPoolTimeout&) { error = ServiceError{503, "DATABASE_UNAVAILABLE", "database connection acquisition timed out"}; }
            catch (...) {}
            (*callback)(std::move(error));
        });
    if (!accepted) { (*callback)(ServiceError{503, "SERVER_BUSY", "database task queue is full"}); }
}

bool ForecastService::modelAvailable() const noexcept { return forecaster_.available(); }
std::string ForecastService::modelName() const { return forecaster_.modelName(); }
std::string ForecastService::modelVersion() const { return forecaster_.modelVersion(); }

}  // namespace water::service
