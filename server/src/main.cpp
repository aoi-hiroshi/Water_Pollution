#include "water/config/server_config.h"
#include "water/controller/api_controller.h"
#include "water/db/db_executor.h"
#include "water/db/mysql_connection.h"
#include "water/inference/onnx_trace_classifier.h"
#include "water/inference/trace_classifier.h"
#include "water/inference/onnx_forecaster.h"
#include "water/service/forecast_service.h"
#include "water/net/http_router.h"
#include "water/net/muduo_http_server.h"
#include "water/repository/water_repository.h"
#include "water/service/water_service.h"
#include "water/service/trace_service.h"
#include "water/service/classification_data_service.h"
#include "water/service/forecast_data_service.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include <csignal>
#include <exception>
#include <memory>
#include <utility>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void requestStop(int) {
    stop_requested = 1;
}

}  // namespace

int main() {
    try {
        const auto config = water::config::ServerConfig::fromEnvironment();

        water::db::DbExecutor database(
            config.database,
            water::db::makeMySqlConnectionFactory(config.mysql));
        database.start();

        water::repository::WaterRepository repository;
        water::service::WaterService service(database, repository);

        water::concurrency::ThreadPool inference_workers(
            config.trace.workers);
        inference_workers.start();

        std::unique_ptr<water::inference::TraceClassifier> classifier;
#ifdef WATER_WITH_ONNX
        if (!config.trace.model_path.empty()) {
            classifier =
                std::make_unique<water::inference::OnnxTraceClassifier>(
                    water::inference::OnnxTraceClassifierOptions{
                        .model_path = config.trace.model_path,
                        .model_name = config.trace.model_name,
                        .model_version = config.trace.model_version,
                        .label_offset = config.trace.label_offset,
                        .intra_op_threads =
                            config.trace.onnx_intra_op_threads,
                    });
        } else {
            classifier = std::make_unique<
                water::inference::UnavailableTraceClassifier>(
                "WATER_TRACE_MODEL_PATH is not configured");
        }
#else
        classifier = std::make_unique<
            water::inference::UnavailableTraceClassifier>(
            "server was built without WATER_WITH_ONNX=ON");
#endif

        water::concurrency::ThreadPool analysis_workers(config.analysis_workers);
        analysis_workers.start();
        water::service::ClassificationDataService classification_service(
            database, analysis_workers, repository);
        water::service::ForecastDataService forecast_data_service(
            database, analysis_workers, repository);
        water::service::TraceService trace_service(
            database, inference_workers, *classifier, repository);
        std::unique_ptr<water::inference::Forecaster> forecaster;
#ifdef WATER_WITH_ONNX
        if (!config.forecast_model_path.empty()) {
            forecaster = std::make_unique<water::inference::OnnxForecaster>(
                water::inference::OnnxForecasterOptions{
                    .model_path = config.forecast_model_path,
                    .intra_op_threads = config.trace.onnx_intra_op_threads});
        } else {
            forecaster = std::make_unique<water::inference::UnavailableForecaster>(
                "WATER_FORECAST_MODEL_PATH is not configured");
        }
#else
        forecaster = std::make_unique<water::inference::UnavailableForecaster>(
            "server was built without WATER_WITH_ONNX=ON");
#endif
        water::service::ForecastService forecast_service(
            database, inference_workers, *forecaster, repository);
        water::controller::ApiController controller(
            service, trace_service, forecast_service,
            &classification_service, &forecast_data_service);
        auto router = std::make_shared<water::net::HttpRouter>();
        controller.registerRoutes(*router);

        muduo::net::EventLoop loop;
        const muduo::net::InetAddress address(config.listen_host,
                                               config.listen_port);
        water::net::MuduoHttpServer server(
            &loop, address, "water-api", router, config.http);

        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        loop.runEvery(0.2, [&loop] {
            if (stop_requested != 0) {
                loop.quit();
            }
        });

        LOG_INFO << "water-api listening on " << config.listen_host << ':'
                 << config.listen_port;
        if (!classifier->available()) {
            LOG_WARN << "trace inference is unavailable: model not loaded";
        } else {
            LOG_INFO << "trace model loaded: " << classifier->modelName()
                     << " version " << classifier->modelVersion();
        }
        LOG_INFO << "forecast model " << (forecaster->available() ? "loaded: " : "unavailable: ")
                 << forecaster->modelName() << " version " << forecaster->modelVersion();
        server.start();
        loop.loop();

        controller.stopAccepting();
        classification_service.shutdown();
        forecast_data_service.shutdown();
        database.shutdown(water::concurrency::ShutdownMode::Drain);
        analysis_workers.shutdown(water::concurrency::ShutdownMode::Drain);
        inference_workers.shutdown(
            water::concurrency::ShutdownMode::Drain);
        LOG_INFO << "water-api stopped";
        return 0;
    } catch (const std::exception& error) {
        LOG_ERROR << "water-api failed: " << error.what();
        return 1;
    }
}
