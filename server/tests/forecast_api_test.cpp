#include "water/controller/api_controller.h"
#include "support/fake_database.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace water;
using namespace std::chrono_literals;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)
}

int main() {
    try {
        db::DbExecutorOptions options;
        options.connection_pool.min_connections=options.connection_pool.max_connections=1;
        options.workers.core_threads=options.workers.max_threads=1;
        auto state=std::make_shared<db::test::FakeDatabaseState>();
        db::DbExecutor database(options,db::test::makeFakeFactory(state));
        concurrency::ThreadPool workers;
        repository::WaterRepository repository;
        service::WaterService water_service(database,repository);
        inference::UnavailableTraceClassifier classifier("test");
        inference::UnavailableForecaster forecaster("test");
        service::TraceService trace(database,workers,classifier,repository);
        service::ForecastService forecast(database,workers,forecaster,repository);
        controller::ApiController controller(water_service,trace,forecast);
        net::HttpRouter router;
        controller.registerRoutes(router);
        const auto invoke=[&router](std::string method,std::string path,std::string body="") {
            std::promise<net::HttpResponse> promise;
            auto future=promise.get_future();
            net::HttpRequest request;
            request.method=method=="GET" ? net::HttpMethod::Get : net::HttpMethod::Post;
            request.path=std::move(path); request.body=std::move(body);
            router.dispatch(std::move(request),[&promise](auto response) { promise.set_value(std::move(response)); });
            CHECK(future.wait_for(1s)==std::future_status::ready);
            return future.get();
        };
        const auto metadata=invoke("GET","/api/v1/inference/forecast/model");
        const auto model=nlohmann::json::parse(metadata.body).at("data");
        CHECK(metadata.status==200 && !model.at("available").get<bool>());
        CHECK(model.at("lookback")==120 && model.at("max_horizon")==10);
        CHECK(model.at("input_features").at(0)=="cod" && model.at("input_features").at(9)=="dissolved_oxygen");
        CHECK(model.at("sampling_interval_seconds").is_null());
        std::cout << "[PASS] forecast model metadata HTTP JSON\n";
        const std::vector<std::string> invalid{
            "not json", "[]", "{}", R"({"company_id":1.2})", R"({"company_id":true})",
            R"({"company_id":18446744073709551615})", R"({"company_id":1,"horizon":11})",
            R"({"company_id":1,"end_sample_id":-1})", R"({"company_id":1,"dataset":123})",
            R"({"company_id":1,"dataset":"test_data; DROP TABLE test_data"})"};
        for (const auto& body : invalid) { CHECK(invoke("POST","/api/v1/inference/forecast",body).status==400); }
        std::cout << "[PASS] forecast JSON validation and integer overflow\n";
        const auto unavailable=invoke("POST","/api/v1/inference/forecast",R"({"company_id":7})");
        CHECK(unavailable.status==503 && nlohmann::json::parse(unavailable.body).at("error")=="INFERENCE_UNAVAILABLE");
        std::cout << "[PASS] forecast unavailable-model HTTP envelope\n";
        const auto health=nlohmann::json::parse(invoke("GET","/api/v1/health").body).at("data");
        CHECK(health.at("forecast_inference").at("available")==false);
        std::cout << "[PASS] health exposes forecast availability\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
