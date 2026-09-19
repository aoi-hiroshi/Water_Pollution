#pragma once

#include "water/db/db_executor.h"
#include "water/db/mysql_connection.h"
#include "water/net/muduo_http_server.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace water::config {

struct TraceInferenceConfig {
    std::string model_path;
    std::string model_name{"random_forest"};
    std::string model_version{"1.0"};
    std::int64_t label_offset{1};
    int onnx_intra_op_threads{1};
    concurrency::ThreadPoolOptions workers{
        .name = "inference",
        .core_threads = 1,
        .max_threads = 2,
        .queue_capacity = 128,
        .idle_timeout = std::chrono::seconds{30},
    };
};

struct ServerConfig {
    std::string listen_host{"0.0.0.0"};
    std::uint16_t listen_port{8080};
    db::MySqlConfig mysql;
    db::DbExecutorOptions database;
    net::MuduoHttpServerOptions http;
    TraceInferenceConfig trace;
    concurrency::ThreadPoolOptions analysis_workers{
        .name = "classification-analysis", .core_threads = 1,
        .max_threads = 1, .queue_capacity = 4,
        .idle_timeout = std::chrono::seconds{30},
    };
    std::string forecast_model_path;

    [[nodiscard]] static ServerConfig fromEnvironment();
};

}  // namespace water::config
