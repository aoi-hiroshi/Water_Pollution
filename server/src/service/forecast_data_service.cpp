#include "water/service/forecast_data_service.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace water::service {
namespace {
ServiceError databaseError(std::exception_ptr exception) {
    try {
        std::rethrow_exception(exception);
    } catch (const db::ConnectionPoolTimeout&) {
        return {503, "DATABASE_UNAVAILABLE", "database connection acquisition timed out"};
    } catch (const db::ConnectionPoolStopped&) {
        return {503, "DATABASE_UNAVAILABLE", "database executor is stopped"};
    } catch (const std::length_error& error) {
        return {413, "DATASET_TOO_LARGE", error.what()};
    } catch (const std::out_of_range& error) {
        return {404, "DATA_NOT_FOUND", error.what()};
    } catch (...) {
        return {500, "DATABASE_ERROR", "database operation failed; check val_data view and permissions"};
    }
}
} // namespace

struct ForecastDataService::State {
    std::mutex mutex;
    std::condition_variable idle;
    bool closing{false};
    std::size_t pending{0};
    std::size_t capacity{0};
};

struct ForecastDataService::Ticket {
    explicit Ticket(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~Ticket() {
        std::lock_guard lock(state->mutex);
        --state->pending;
        state->idle.notify_all();
    }
    std::shared_ptr<State> state;
};

ForecastDataService::ForecastDataService(
    db::DbExecutor& database, concurrency::ThreadPool& workers,
    const repository::WaterRepository& repository)
    : state_(std::make_shared<State>()),
      database_(database), workers_(workers), repository_(repository) {
    const auto statistics = workers.statistics();
    state_->capacity = statistics.queue_capacity + statistics.max_threads;
}

ForecastDataService::~ForecastDataService() { shutdown(); }

void ForecastDataService::shutdown() {
    std::unique_lock lock(state_->mutex);
    state_->closing = true;
    state_->idle.wait(lock, [this] { return state_->pending == 0; });
}

void ForecastDataService::execute(
    domain::ForecastDataRequest request,
    std::function<void(ForecastDataResult)> completion) {
    if (!completion) {
        throw std::invalid_argument("forecast data completion must not be empty");
    }
    try {
        analysis::validateForecastDataRequest(request);
    } catch (const std::invalid_argument& error) {
        completion(ServiceError{400, "INVALID_ARGUMENT", error.what()});
        return;
    }

    std::shared_ptr<Ticket> ticket;
    {
        std::unique_lock lock(state_->mutex);
        if (state_->closing) {
            lock.unlock();
            completion(ServiceError{503, "SERVER_STOPPING", "forecast data service is stopping"});
            return;
        }
        if (state_->pending >= state_->capacity) {
            lock.unlock();
            completion(ServiceError{503, "ANALYSIS_BUSY", "forecast preprocessing pipeline is full"});
            return;
        }
        ticket = std::make_shared<Ticket>(state_);
        ++state_->pending;
    }

    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto finish = [ticket, delivered, completion = std::move(completion)](
                      ForecastDataResult result) {
        if (!delivered->exchange(true)) {
            completion(std::move(result));
        }
    };
    const bool accepted = database_.post(
        [this, request, finish](db::DbConnection& connection) mutable {
            const auto company = repository_.findCompany(connection, request.company_id);
            if (!company) {
                finish(ServiceError{404, "COMPANY_NOT_FOUND", "forecast company was not found"});
                return;
            }
            auto samples = repository_.loadForecastSamples(
                connection, request.company_id, request.dataset,
                domain::kForecastDataRowLimit + 1);
            if (samples.size() > domain::kForecastDataRowLimit) {
                finish(ServiceError{413, "DATASET_TOO_LARGE", "forecast preprocessing exceeds 100000 rows"});
                return;
            }
            const bool queued = workers_.post(
                [request, finish, company = *company,
                 samples = std::move(samples)]() mutable {
                    try {
                        finish(analysis::analyzeForecastData(
                            request, std::move(company), std::move(samples)));
                    } catch (const std::invalid_argument& error) {
                        finish(ServiceError{422, "INVALID_FORECAST_DATA", error.what()});
                    } catch (const std::length_error& error) {
                        finish(ServiceError{413, "DATASET_TOO_LARGE", error.what()});
                    } catch (...) {
                        finish(ServiceError{500, "ANALYSIS_ERROR", "forecast preprocessing failed"});
                    }
                });
            if (!queued) {
                finish(ServiceError{503, "ANALYSIS_BUSY", "analysis queue is full"});
            }
        },
        [finish](std::exception_ptr error) { finish(databaseError(error)); });
    if (!accepted) {
        finish(ServiceError{503, "SERVER_BUSY", "database task queue is full"});
    }
}

} // namespace water::service
