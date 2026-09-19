#include "water/service/trace_service.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace water::service {

namespace {

struct TraceInputContext {
    domain::WaterSample sample;
    std::vector<domain::Company> companies;
};

inference::TraceFeatureVector toFeatures(const domain::WaterSample& sample) {
    const std::array<std::optional<double>, inference::kTraceFeatureCount>
        source{
            sample.temperature,
            sample.ph,
            sample.cod,
            sample.nh3n,
            sample.tp,
            sample.water_level,
            sample.orp,
            sample.conductivity,
            sample.dissolved_oxygen,
            sample.turbidity,
        };

    inference::TraceFeatureVector features{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (!source[index] || !std::isfinite(*source[index]) ||
            std::abs(*source[index]) >
                static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::invalid_argument(
                "trace sample contains a missing or invalid feature");
        }
        features[index] = static_cast<float>(*source[index]);
    }
    return features;
}

domain::TraceResult buildResult(
    std::int64_t sample_id, std::string dataset,
    const std::vector<domain::Company>& companies,
    const inference::TraceModelPrediction& prediction,
    const inference::TraceClassifier& classifier) {
    if (prediction.probabilities.empty()) {
        throw inference::InferenceError(
            "trace model returned no class probabilities");
    }

    std::unordered_map<std::int64_t, const domain::Company*> company_by_id;
    for (const auto& company : companies) {
        company_by_id.emplace(company.id, &company);
    }

    domain::TraceResult result;
    result.sample_id = sample_id;
    result.dataset = std::move(dataset);
    result.model_name = classifier.modelName();
    result.model_version = classifier.modelVersion();
    result.candidates.reserve(prediction.probabilities.size());

    for (const auto& item : prediction.probabilities) {
        if (item.label <= 0 || !std::isfinite(item.probability) ||
            item.probability < 0.0 || item.probability > 1.0) {
            throw inference::InferenceError(
                "trace model returned an invalid class probability");
        }
        domain::TraceCandidate candidate;
        candidate.class_label = item.label;
        candidate.company_id = item.label;
        candidate.probability = item.probability;
        if (const auto found = company_by_id.find(item.label);
            found != company_by_id.end()) {
            candidate.company_name = found->second->name;
            candidate.company_code = found->second->code;
        } else {
            candidate.company_name = "公司" + std::to_string(item.label);
        }
        result.candidates.push_back(std::move(candidate));
    }

    std::stable_sort(
        result.candidates.begin(), result.candidates.end(),
        [](const auto& left, const auto& right) {
            return left.probability > right.probability;
        });
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        result.candidates[index].rank = index + 1;
    }
    result.predicted = result.candidates.front();
    return result;
}

}  // namespace

TraceService::TraceService(
    db::DbExecutor& database,
    concurrency::ThreadPool& inference_workers,
    inference::TraceClassifier& classifier,
    const repository::WaterRepository& repository)
    : database_(database),
      inference_workers_(inference_workers),
      classifier_(classifier),
      repository_(repository) {}

void TraceService::classifySample(
    std::int64_t sample_id, std::string dataset,
    std::function<void(TraceResult)> completion, std::int64_t cleaning_run_id) {
    if (!completion) {
        throw std::invalid_argument("trace completion must not be empty");
    }
    if (sample_id <= 0 || cleaning_run_id < 0 ||
        (dataset != "train_data" && dataset != "test_data")) {
        completion(ServiceError{400, "INVALID_ARGUMENT",
                                "invalid sample_id or dataset"});
        return;
    }

    if (!classifier_.available()) {
        completion(ServiceError{503,"INFERENCE_UNAVAILABLE","classification ONNX model is not loaded"});
        return;
    }
    auto callback = std::make_shared<decltype(completion)>(
        std::move(completion));
    const bool database_accepted = database_.post(
        [this, callback, sample_id, cleaning_run_id,
         dataset = std::move(dataset)](db::DbConnection& connection) mutable {
            TraceInputContext context;
            try {
                const auto sample = cleaning_run_id == 0
                    ? repository_.findSampleById(connection, dataset, sample_id)
                    : repository_.findCleanedSampleById(connection, dataset, sample_id, cleaning_run_id);
                if (!sample) {
                    (*callback)(ServiceError{
                        404, "TRACE_SAMPLE_NOT_FOUND",
                        "trace sample was not found in the selected dataset"});
                    return;
                }
                context.sample = *sample;
                context.companies = repository_.listCompanies(connection);
            } catch (const db::ConnectionPoolTimeout&) {
                (*callback)(ServiceError{
                    503, "DATABASE_UNAVAILABLE",
                    "database is temporarily unavailable"});
                return;
            } catch (...) {
                (*callback)(ServiceError{500, "DATABASE_ERROR",
                                         "database operation failed"});
                return;
            }

            const bool inference_accepted = inference_workers_.post(
                [this, callback, sample_id, cleaning_run_id, dataset = std::move(dataset),
                 context = std::move(context)]() mutable {
                    try {
                        const auto prediction = classifier_.classify(
                            toFeatures(context.sample));
                        auto result = buildResult(
                            sample_id, std::move(dataset), context.companies,
                            prediction, classifier_);
                        result.cleaning_run_id = cleaning_run_id;
                        (*callback)(std::move(result));
                    } catch (const inference::InferenceUnavailable& error) {
                        (*callback)(ServiceError{
                            503, "INFERENCE_UNAVAILABLE", error.what()});
                    } catch (const std::invalid_argument& error) {
                        (*callback)(ServiceError{
                            422, "INVALID_TRACE_SAMPLE", error.what()});
                    } catch (const std::exception& error) {
                        (*callback)(ServiceError{
                            500, "INFERENCE_ERROR", error.what()});
                    } catch (...) {
                        (*callback)(ServiceError{
                            500, "INFERENCE_ERROR", "model inference failed"});
                    }
                });
            if (!inference_accepted) {
                (*callback)(ServiceError{
                    503, "INFERENCE_BUSY",
                    "inference task queue is full"});
            }
        }, [callback](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const db::ConnectionPoolTimeout&) {
                (*callback)(ServiceError{503,"DATABASE_UNAVAILABLE","database acquisition timed out"});
            } catch (const db::ConnectionPoolStopped&) {
                (*callback)(ServiceError{503,"DATABASE_UNAVAILABLE","database is stopping"});
            } catch (...) {
                (*callback)(ServiceError{500,"DATABASE_ERROR","database operation failed"});
            }
        });

    if (!database_accepted) {
        (*callback)(ServiceError{503, "SERVER_BUSY",
                                 "database task queue is full"});
    }
}

bool TraceService::modelAvailable() const noexcept {
    return classifier_.available();
}

std::string TraceService::modelName() const {
    return classifier_.modelName();
}

std::string TraceService::modelVersion() const {
    return classifier_.modelVersion();
}

}  // namespace water::service
