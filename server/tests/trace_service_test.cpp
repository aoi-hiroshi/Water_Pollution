#include "water/concurrency/thread_pool.h"
#include "water/db/db_executor.h"
#include "water/inference/trace_classifier.h"
#include "water/repository/water_repository.h"
#include "water/service/trace_service.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using water::db::DbConnection;
using water::db::DbRow;
using water::db::DbRows;
using water::db::ExecuteResult;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            throw std::runtime_error(std::string{"check failed: "} +         \
                                     #expression);                            \
        }                                                                     \
    } while (false)

DbRow companyRow(std::int64_t id, std::string name, std::string code) {
    return DbRow{
        {"company_id", std::to_string(id)},
        {"company_name", std::move(name)},
        {"company_code", std::move(code)},
        {"task_type", "classification"},
        {"location", std::nullopt},
        {"description", std::nullopt},
        {"created_at", std::nullopt},
    };
}

class TraceConnection final : public DbConnection {
public:
    bool ping() noexcept override { return true; }
    bool resetSession() noexcept override { return true; }
    ExecuteResult execute(std::string_view,
                          std::span<const water::db::DbValue>) override {
        return {};
    }

    DbRows query(std::string_view sql,
                 std::span<const water::db::DbValue>) override {
        const std::string statement{sql};
        if (statement.find("FROM test_data WHERE id = ?") !=
            std::string::npos) {
            return {{{"id", "42"},
                     {"temperature", "20.5"},
                     {"ph", "7.1"},
                     {"cod", "22.0"},
                     {"nh3n", "1.2"},
                     {"tp", "0.3"},
                     {"water_level", "3.4"},
                     {"orp", "120.0"},
                     {"conductivity", "540.0"},
                     {"dissolved_oxygen", "6.7"},
                     {"turbidity", "2.1"},
                     {"company_id", "6"}}};
        }
        if (statement.find("FROM company_info") != std::string::npos) {
            return {companyRow(2, "Factory Two", "F-002"),
                    companyRow(3, "Factory Three", "F-003")};
        }
        throw std::runtime_error("unexpected SQL in trace test");
    }

    void beginTransaction() override {}
    void commit() override {}
    void rollback() noexcept override {}
};

class FakeClassifier final : public water::inference::TraceClassifier {
public:
    water::inference::TraceModelPrediction classify(
        const water::inference::TraceFeatureVector& features) override {
        received = features;
        return {
            .predicted_label = 3,
            .probabilities = {{1, 0.04}, {2, 0.21}, {3, 0.62},
                              {4, 0.05}, {5, 0.03}, {6, 0.05}},
        };
    }

    std::string modelName() const override { return "fake-random-forest"; }
    std::string modelVersion() const override { return "test-1"; }
    bool available() const noexcept override { return true; }

    water::inference::TraceFeatureVector received{};
};

water::db::DbExecutorOptions databaseOptions() {
    water::db::DbExecutorOptions options;
    options.connection_pool.min_connections = 1;
    options.connection_pool.max_connections = 1;
    options.connection_pool.acquire_timeout = 100ms;
    options.workers.core_threads = 1;
    options.workers.max_threads = 1;
    options.workers.queue_capacity = 8;
    return options;
}

water::concurrency::ThreadPoolOptions inferenceOptions() {
    return {
        .name = "trace-test",
        .core_threads = 1,
        .max_threads = 1,
        .queue_capacity = 8,
        .idle_timeout = 1s,
    };
}

void testTraceFlowAndFeatureOrder() {
    water::db::DbExecutor database(
        databaseOptions(), [] { return std::make_unique<TraceConnection>(); });
    water::concurrency::ThreadPool inference_workers(inferenceOptions());
    database.start();
    inference_workers.start();

    water::repository::WaterRepository repository;
    FakeClassifier classifier;
    water::service::TraceService service(
        database, inference_workers, classifier, repository);

    std::promise<water::service::TraceResult> promise;
    auto future = promise.get_future();
    service.classifySample(42, "test_data", [&promise](auto result) {
        promise.set_value(std::move(result));
    });

    const auto result = future.get();
    CHECK(std::holds_alternative<water::domain::TraceResult>(result));
    const auto& trace = std::get<water::domain::TraceResult>(result);
    CHECK(trace.sample_id == 42);
    CHECK(trace.model_name == "fake-random-forest");
    CHECK(trace.candidates.size() == 6);
    CHECK(trace.predicted.company_id == 3);
    CHECK(trace.predicted.company_name == "Factory Three");
    CHECK(trace.predicted.probability == 0.62);
    CHECK(trace.candidates[1].company_id == 2);
    CHECK(trace.candidates.back().company_name == "公司5");

    CHECK(classifier.received[0] == 20.5F);
    CHECK(classifier.received[1] == 7.1F);
    CHECK(classifier.received[2] == 22.0F);
    CHECK(classifier.received[7] == 540.0F);
    CHECK(classifier.received[9] == 2.1F);

    database.shutdown();
    inference_workers.shutdown();
}

void testUnavailableModelIsServiceUnavailable() {
    water::db::DbExecutor database(
        databaseOptions(), [] { return std::make_unique<TraceConnection>(); });
    water::concurrency::ThreadPool inference_workers(inferenceOptions());
    database.start();
    inference_workers.start();

    water::repository::WaterRepository repository;
    water::inference::UnavailableTraceClassifier classifier(
        "test model is absent");
    water::service::TraceService service(
        database, inference_workers, classifier, repository);

    std::promise<water::service::TraceResult> promise;
    auto future = promise.get_future();
    service.classifySample(42, "test_data", [&promise](auto result) {
        promise.set_value(std::move(result));
    });
    const auto result = future.get();
    CHECK(std::holds_alternative<water::service::ServiceError>(result));
    const auto& error = std::get<water::service::ServiceError>(result);
    CHECK(error.http_status == 503);
    CHECK(error.code == "INFERENCE_UNAVAILABLE");

    database.shutdown();
    inference_workers.shutdown();
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"trace flow and feature order", testTraceFlowAndFeatureOrder},
        {"unavailable model", testUnavailableModelIsServiceUnavailable},
    };
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
