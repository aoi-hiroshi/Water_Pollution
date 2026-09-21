#include "water/service/water_service.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace water::service {

namespace {

ServiceError databaseUnavailable() {
    return ServiceError{503, "DATABASE_UNAVAILABLE",
                        "database is temporarily unavailable"};
}

ServiceError internalError() {
    return ServiceError{500, "DATABASE_ERROR",
                        "database operation failed"};
}

ServiceError acquisitionError(std::exception_ptr error) {
    try { std::rethrow_exception(error); }
    catch (const db::ConnectionPoolTimeout&) { return databaseUnavailable(); }
    catch (const db::ConnectionPoolStopped&) { return databaseUnavailable(); }
    catch (...) { return internalError(); }
}

}  // namespace

WaterService::WaterService(
    db::DbExecutor& database,
    const repository::WaterRepository& repository)
    : database_(database), repository_(repository) {}

void WaterService::listCompanies(
    std::function<void(CompaniesResult)> completion) {
    if (!completion) {
        throw std::invalid_argument("service completion must not be empty");
    }
    auto callback = std::make_shared<decltype(completion)>(
        std::move(completion));
    const bool accepted = database_.post(
        [this, callback](db::DbConnection& connection) {
            CompaniesResult result;
            try {
                result = repository_.listCompanies(connection);
            } catch (const db::ConnectionPoolTimeout&) {
                result = databaseUnavailable();
            } catch (...) {
                result = internalError();
            }
            (*callback)(std::move(result));
        }, [callback](std::exception_ptr error) { (*callback)(acquisitionError(error)); });
    if (!accepted) {
        (*callback)(ServiceError{503, "SERVER_BUSY",
                                 "database task queue is full"});
    }
}

void WaterService::getOverview(
    std::int64_t company_id, std::string dataset,
    std::size_t preview_limit,
    std::function<void(OverviewResult)> completion) {
    if (!completion) {
        throw std::invalid_argument("service completion must not be empty");
    }
    if (company_id <= 0 ||
        (dataset != "train_data" && dataset != "val_data" &&
         dataset != "test_data")) {
        completion(ServiceError{400, "INVALID_ARGUMENT",
                                "invalid company_id or dataset"});
        return;
    }

    auto callback = std::make_shared<decltype(completion)>(
        std::move(completion));
    const bool accepted = database_.post(
        [this, callback, company_id, dataset = std::move(dataset),
         preview_limit](db::DbConnection& connection) {
            OverviewResult result;
            try {
                result = repository_.loadOverview(
                    connection, company_id, dataset, preview_limit);
            } catch (const std::out_of_range& error) {
                result = ServiceError{404, "DATA_NOT_FOUND", error.what()};
            } catch (const db::ConnectionPoolTimeout&) {
                result = databaseUnavailable();
            } catch (...) {
                result = internalError();
            }
            (*callback)(std::move(result));
        }, [callback](std::exception_ptr error) { (*callback)(acquisitionError(error)); });
    if (!accepted) {
        (*callback)(ServiceError{503, "SERVER_BUSY",
                                 "database task queue is full"});
    }
}

}  // namespace water::service
