#include "water/db/db_executor.h"
#include "water/repository/water_repository.h"
#include "water/service/water_service.h"

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

DbRow companyRow() {
    return DbRow{
        {"company_id", "7"},
        {"company_name", "Example Water Company"},
        {"company_code", "WATER-007"},
        {"task_type", "classification"},
        {"location", "Zhejiang"},
        {"description", "test company"},
        {"created_at", "2026-09-16 10:00:00"},
    };
}

class ScriptedConnection final : public DbConnection {
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
        if (statement.find("COUNT(*) AS total_rows") != std::string::npos) {
            DbRow row{{"total_rows", "2"}};
            const std::vector<std::string> features{
                "temperature", "ph", "cod", "nh3n", "tp", "water_level",
                "orp", "conductivity", "dissolved_oxygen", "turbidity"};
            for (const auto& feature : features) {
                row.emplace(feature + "_count", "2");
                row.emplace(feature + "_mean", "2.5");
                row.emplace(feature + "_std", "0.5");
                row.emplace(feature + "_min", "2.0");
                row.emplace(feature + "_max", "3.0");
            }
            return {std::move(row)};
        }
        if (statement.find("FROM train_data WHERE") != std::string::npos) {
            return {{{"id", "1"},
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
                     {"company_id", "7"}}};
        }
        if (statement.find("FROM company_info") != std::string::npos) {
            return {companyRow()};
        }
        throw std::runtime_error("unexpected SQL in scripted connection");
    }

    void beginTransaction() override {}
    void commit() override {}
    void rollback() noexcept override {}
};

water::db::DbExecutorOptions options() {
    water::db::DbExecutorOptions value;
    value.connection_pool.min_connections = 1;
    value.connection_pool.max_connections = 1;
    value.connection_pool.acquire_timeout = 100ms;
    value.workers.core_threads = 1;
    value.workers.max_threads = 1;
    value.workers.queue_capacity = 8;
    return value;
}

void testCompanyAndOverviewFlow() {
    water::db::DbExecutor database(
        options(), [] { return std::make_unique<ScriptedConnection>(); });
    database.start();
    water::repository::WaterRepository repository;
    water::service::WaterService service(database, repository);

    std::promise<water::service::CompaniesResult> companies_promise;
    auto companies_future = companies_promise.get_future();
    service.listCompanies([&companies_promise](auto result) {
        companies_promise.set_value(std::move(result));
    });
    const auto companies_result = companies_future.get();
    CHECK(std::holds_alternative<std::vector<water::domain::Company>>(
        companies_result));
    const auto& companies =
        std::get<std::vector<water::domain::Company>>(companies_result);
    CHECK(companies.size() == 1);
    CHECK(companies.front().id == 7);

    std::promise<water::service::OverviewResult> overview_promise;
    auto overview_future = overview_promise.get_future();
    service.getOverview(7, "train_data", 10,
                        [&overview_promise](auto result) {
                            overview_promise.set_value(std::move(result));
                        });
    const auto overview_result = overview_future.get();
    CHECK(std::holds_alternative<water::domain::DataOverview>(overview_result));
    const auto& overview =
        std::get<water::domain::DataOverview>(overview_result);
    CHECK(overview.total_rows == 2);
    CHECK(overview.summary.size() == 10);
    CHECK(overview.preview_rows.size() == 1);
    CHECK(overview.preview_rows.front().ph.value() == 7.1);

    database.shutdown();
}

void testValidationDoesNotReachDatabase() {
    water::db::DbExecutor database(
        options(), [] { return std::make_unique<ScriptedConnection>(); });
    database.start();
    water::repository::WaterRepository repository;
    water::service::WaterService service(database, repository);

    bool called = false;
    service.getOverview(7, "untrusted_table", 10,
                        [&called](auto result) {
                            called = true;
                            CHECK(std::holds_alternative<water::service::ServiceError>(
                                result));
                            CHECK(std::get<water::service::ServiceError>(result)
                                      .http_status == 400);
                        });
    CHECK(called);
    database.shutdown();
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"company and overview flow", testCompanyAndOverviewFlow},
        {"validation", testValidationDoesNotReachDatabase},
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
