#include "water/service/forecast_service.h"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace water;
#define CHECK(expression) do { if (!(expression)) { throw std::runtime_error(#expression); } } while (false)

struct Scenario {
    std::size_t rows{120};
    bool null_feature{}, mixed_company{}, no_company{}, db_failure{};
    std::atomic_bool ping_failure{};
    db::DbParameters parameters;
};

class ForecastConnection final : public db::DbConnection {
public:
    explicit ForecastConnection(std::shared_ptr<Scenario> scenario) : scenario_(std::move(scenario)) {}
    bool ping() noexcept override { return !scenario_->ping_failure; }
    bool resetSession() noexcept override { return true; }
    db::ExecuteResult execute(std::string_view, std::span<const db::DbValue>) override { return {}; }
    db::DbRows query(std::string_view sql, std::span<const db::DbValue> parameters) override {
        if (scenario_->db_failure) { throw db::ConnectionPoolTimeout("test timeout"); }
        if (sql.find("FROM company_info") != std::string_view::npos) {
            if (scenario_->no_company) { return {}; }
            return {{{"company_id", "7"}, {"company_name", "Forecast Station"}, {"company_code", "P-007"},
                     {"task_type", "prediction"}, {"location", std::nullopt}, {"description", std::nullopt}, {"created_at", std::nullopt}}};
        }
        CHECK(sql.find("FROM test_data WHERE company_id = ?") != std::string_view::npos);
        CHECK(sql.find("ORDER BY id DESC LIMIT 120") != std::string_view::npos);
        scenario_->parameters.assign(parameters.begin(), parameters.end());
        db::DbRows rows;
        for (std::size_t index = 0; index < scenario_->rows; ++index) {
            rows.push_back({{"id", std::to_string(240-index)}, {"company_id", scenario_->mixed_company ? "8" : "7"},
                {"cod", std::to_string(240-index)}, {"nh3n", "2"}, {"tp", "3"}, {"turbidity", "4"},
                {"temperature", "5"}, {"ph", "6"}, {"water_level", "7"}, {"orp", "8"},
                {"conductivity", "9"}, {"dissolved_oxygen", scenario_->null_feature ? db::DbCell{} : db::DbCell{"10"}}});
        }
        return rows;
    }
    void beginTransaction() override {}
    void commit() override {}
    void rollback() noexcept override {}
private:
    std::shared_ptr<Scenario> scenario_;
};

class FakeForecaster final : public inference::Forecaster {
public:
    bool loaded{true}, invalid_output{};
    std::vector<float> received;
    inference::ForecastModelPrediction forecast(std::span<const float> window) override {
        received.assign(window.begin(), window.end());
        inference::ForecastModelPrediction result(10, {11.F, 12.F, 13.F, 14.F});
        if (invalid_output) { result.pop_back(); }
        return result;
    }
    std::string modelName() const override { return "test-attention-lstm"; }
    std::string modelVersion() const override { return "test-1"; }
    bool available() const noexcept override { return loaded; }
};

db::DbExecutorOptions databaseOptions() {
    db::DbExecutorOptions options;
    options.connection_pool.min_connections = options.connection_pool.max_connections = 1;
    options.workers.core_threads = options.workers.max_threads = 1;
    options.workers.queue_capacity = 8;
    options.connection_pool.acquire_timeout = 100ms;
    options.connection_pool.validation_interval = 0ms;
    return options;
}

struct Fixture {
    std::shared_ptr<Scenario> scenario{std::make_shared<Scenario>()};
    db::DbExecutor database{databaseOptions(), [this] {
        if (scenario->ping_failure) { throw db::ConnectionPoolTimeout("test connection creation timeout"); }
        return std::make_unique<ForecastConnection>(scenario);
    }};
    concurrency::ThreadPool workers{{.name="forecast-test", .core_threads=1, .max_threads=1, .queue_capacity=8, .idle_timeout=1s}};
    repository::WaterRepository repository;
    FakeForecaster model;
    service::ForecastService service{database, workers, model, repository};
    Fixture() { database.start(); workers.start(); }
    ~Fixture() { database.shutdown(); workers.shutdown(); }
    service::ForecastResult run(std::int64_t end=0, std::size_t horizon=10, std::string dataset="test_data") {
        std::promise<service::ForecastResult> promise;
        auto future = promise.get_future();
        service.forecast(7, std::move(dataset), end, horizon, [&promise](auto result) { promise.set_value(std::move(result)); });
        CHECK(future.wait_for(3s) == std::future_status::ready);
        return future.get();
    }
};

void checkError(const service::ForecastResult& result, int status, const std::string& code) {
    const auto& error = std::get<service::ServiceError>(result);
    CHECK(error.http_status == status); CHECK(error.code == code);
}
void flow() {
    Fixture f;
    const auto result = f.run(240, 5);
    const auto& forecast = std::get<domain::ForecastResult>(result);
    CHECK(forecast.lookback == 120 && forecast.history.size() == 120);
    CHECK(forecast.start_sample_id == 121 && forecast.end_sample_id == 240);
    CHECK(forecast.company_id == 7 && forecast.model_name == "test-attention-lstm");
    CHECK(forecast.predictions.size() == 5 && forecast.predictions.back().step == 5);
    CHECK(forecast.predictions.front().values[0] == 11.0);
    CHECK(f.model.received.size() == 1200);
    CHECK(f.model.received[0] == 121.F && f.model.received[1190] == 240.F);
    for (std::size_t index = 1; index < 10; ++index) { CHECK(f.model.received[index] == static_cast<float>(index+1)); }
    CHECK(f.scenario->parameters.size() == 2);
    CHECK(std::get<std::int64_t>(f.scenario->parameters[0]) == 7);
    CHECK(std::get<std::int64_t>(f.scenario->parameters[1]) == 240);
}
void latest() { Fixture f; CHECK(std::holds_alternative<domain::ForecastResult>(f.run())); CHECK(f.scenario->parameters.size() == 1); }
void invalidArguments() { Fixture f; checkError(f.run(0,11),400,"INVALID_ARGUMENT"); checkError(f.run(0,0),400,"INVALID_ARGUMENT"); checkError(f.run(-1),400,"INVALID_ARGUMENT"); checkError(f.run(0,10,"test_data; DROP TABLE test_data"),400,"INVALID_ARGUMENT"); }
void missingModel() { Fixture f; f.model.loaded=false; checkError(f.run(),503,"INFERENCE_UNAVAILABLE"); }
void shortHistory() { Fixture f; f.scenario->rows=119; checkError(f.run(),422,"INSUFFICIENT_HISTORY"); CHECK(f.model.received.empty()); }
void invalidHistory() { Fixture f; f.scenario->null_feature=true; checkError(f.run(),422,"INVALID_FORECAST_HISTORY"); f.scenario->null_feature=false; f.scenario->mixed_company=true; checkError(f.run(),422,"INVALID_FORECAST_HISTORY"); }
void missingEntities() { Fixture f; checkError(f.run(241),404,"FORECAST_SAMPLE_NOT_FOUND"); f.scenario->no_company=true; checkError(f.run(),404,"COMPANY_NOT_FOUND"); }
void databaseFailure() { Fixture f; f.scenario->db_failure=true; checkError(f.run(),503,"DATABASE_UNAVAILABLE"); }
void acquisitionFailure() { Fixture f; f.scenario->ping_failure=true; checkError(f.run(),503,"DATABASE_UNAVAILABLE"); }
void busyQueues() { Fixture f; f.workers.shutdown(); checkError(f.run(),503,"INFERENCE_BUSY"); f.database.shutdown(); checkError(f.run(),503,"SERVER_BUSY"); }
void invalidOutput() { Fixture f; f.model.invalid_output=true; checkError(f.run(),500,"INFERENCE_ERROR"); }
}

int main() {
    const std::vector<std::pair<std::string, void(*)()>> tests{
        {"forecast flow/order/horizon",flow}, {"latest history",latest}, {"invalid arguments",invalidArguments},
        {"unavailable model",missingModel}, {"insufficient history",shortHistory}, {"invalid history",invalidHistory},
        {"missing company/end sample",missingEntities}, {"database error callback",databaseFailure},
        {"connection acquisition failure callback",acquisitionFailure},
        {"queue rejection",busyQueues}, {"invalid model output",invalidOutput}};
    for (const auto& [name,test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; return EXIT_FAILURE; }
    }
    return EXIT_SUCCESS;
}
