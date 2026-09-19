#include "water/service/classification_data_service.h"

#include <atomic>
#include <condition_variable>
#include <cmath>
#include <exception>
#include <mutex>
#include <limits>
#include <utility>

namespace water::service {
namespace {
ServiceError databaseError(std::exception_ptr exception) {
    try { std::rethrow_exception(exception); }
    catch (const db::ConnectionPoolTimeout&) { return {503,"DATABASE_UNAVAILABLE","database connection acquisition timed out"}; }
    catch (const db::ConnectionPoolStopped&) { return {503,"DATABASE_UNAVAILABLE","database executor is stopped"}; }
    catch (const std::length_error& error) { return {413,"DATASET_TOO_LARGE",error.what()}; }
    catch (const std::out_of_range& error) { return {404,"DATA_NOT_FOUND",error.what()}; }
    catch (...) { return {500,"DATABASE_ERROR","database operation failed; check schema and permissions"}; }
}
}

struct ClassificationDataService::State {
    std::mutex mutex;
    std::condition_variable idle;
    bool closing{false};
    std::size_t pending{0};
    std::size_t capacity{0};
};

struct ClassificationDataService::Ticket {
    explicit Ticket(std::shared_ptr<State> value) : state(std::move(value)) {}
    ~Ticket() {
        std::lock_guard lock(state->mutex);
        --state->pending;
        state->idle.notify_all();
    }
    std::shared_ptr<State> state;
};

ClassificationDataService::ClassificationDataService(db::DbExecutor& database,
    concurrency::ThreadPool& workers, const repository::WaterRepository& repository)
    : state_(std::make_shared<State>()), database_(database), workers_(workers), repository_(repository) {
    const auto statistics=workers.statistics();
    state_->capacity=statistics.queue_capacity+statistics.max_threads;
}

ClassificationDataService::~ClassificationDataService() { shutdown(); }

void ClassificationDataService::shutdown() {
    std::unique_lock lock(state_->mutex);
    state_->closing = true;
    state_->idle.wait(lock,[this] { return state_->pending == 0; });
}

void ClassificationDataService::execute(domain::ClassificationDataRequest request,
    std::function<void(ClassificationDataResult)> completion) {
    if (!completion) { throw std::invalid_argument("classification data completion must not be empty"); }
    try { analysis::validateClassificationRequest(request); }
    catch (const std::invalid_argument& error) { completion(ServiceError{400,"INVALID_ARGUMENT",error.what()}); return; }
    std::shared_ptr<Ticket> ticket;
    {
        std::unique_lock lock(state_->mutex);
        if (state_->closing) {
            lock.unlock(); completion(ServiceError{503,"SERVER_STOPPING","analysis service is stopping"}); return;
        }
        if (state_->pending >= state_->capacity) {
            lock.unlock(); completion(ServiceError{503,"ANALYSIS_BUSY","classification pipeline is full"}); return;
        }
        ticket = std::make_shared<Ticket>(state_);
        ++state_->pending;
    }
    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto finish = [ticket, delivered, completion=std::move(completion)](ClassificationDataResult result) {
        if (!delivered->exchange(true)) { completion(std::move(result)); }
    };
    const bool accepted = database_.post(
        [this,request,finish](db::DbConnection& connection) {
            auto companies = repository_.listCompanies(connection);
            const auto company = repository_.findCompany(connection,request.company_id);
            if (!company) { finish(ServiceError{404,"COMPANY_NOT_FOUND","company not found"}); return; }
            auto samples = repository_.loadClassificationSamples(connection,request);
            const bool queued = workers_.post(
                [this,request,finish,companies=std::move(companies),samples=std::move(samples)]() mutable {
                    std::shared_ptr<domain::ClassificationDataResult> result;
                    try {
                        result = std::make_shared<domain::ClassificationDataResult>(
                            analysis::analyzeClassificationData(request,std::move(samples),companies));
                    } catch (const std::invalid_argument& error) {
                        finish(ServiceError{422,"INVALID_CLASSIFICATION_DATA",error.what()}); return;
                    } catch (const std::length_error& error) {
                        finish(ServiceError{413,"DATASET_TOO_LARGE",error.what()}); return;
                    } catch (...) { finish(ServiceError{500,"ANALYSIS_ERROR","classification analysis failed"}); return; }
                    if (!request.persist) { finish(std::move(*result)); return; }
                    for (const auto& feature : result->features) {
                        if (feature.missing_after != 0) {
                            finish(ServiceError{422,"UNRESOLVED_MISSING_VALUES","cannot save an inference-ready version with all-null features"}); return;
                        }
                    }
                    for (const auto& row : result->processed_rows) {
                        for (const auto& feature : domain::kClassificationFeatures) {
                            const auto& value=row.*feature.member;
                            if (!value || !std::isfinite(*value) || std::abs(*value)>std::numeric_limits<float>::max()) {
                                finish(ServiceError{422,"INVALID_TRACE_SAMPLE","cleaned values cannot be represented as model float32 inputs"}); return;
                            }
                        }
                    }
                    // CPU work never keeps a connection leased. Saving is a
                    // separate DB task with one transaction and bounded batches.
                    const bool saving = database_.post(
                        [this,request,finish,result](db::DbConnection& save_connection) mutable {
                            result->cleaning_run_id = repository_.saveClassificationVersion(save_connection,request,*result);
                            result->persisted = true;
                            finish(std::move(*result));
                        }, [finish](std::exception_ptr error) { finish(databaseError(error)); });
                    if (!saving) { finish(ServiceError{503,"SERVER_BUSY","database save queue is full"}); }
                });
            if (!queued) { finish(ServiceError{503,"ANALYSIS_BUSY","analysis queue is full"}); }
        }, [finish](std::exception_ptr error) { finish(databaseError(error)); });
    if (!accepted) { finish(ServiceError{503,"SERVER_BUSY","database task queue is full"}); }
}
} // namespace water::service
